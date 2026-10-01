#include "mago/imports.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace mago {
namespace {

// Little-endian reads with a bounds check, because a PE file is untrusted
// input like any other and a malformed one must produce an empty list rather
// than a crash in the tool that was supposed to diagnose it.
template <typename T>
bool read_at(const std::string& data, std::size_t offset, T& out) {
    if (offset + sizeof(T) > data.size()) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[offset + i]))
                 << (8 * i);
    }
    out = static_cast<T>(value);
    return true;
}

struct Section {
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t raw_pointer = 0;
};

// An import table addresses everything by RVA -- where the byte will be once
// the image is mapped -- and the file on disk is laid out differently. Walking
// the section headers is the translation.
std::size_t rva_to_offset(const std::vector<Section>& sections, std::uint32_t rva) {
    for (const auto& s : sections) {
        const std::uint32_t span = std::max(s.virtual_size, s.raw_size);
        if (rva >= s.virtual_address && rva < s.virtual_address + span) {
            return static_cast<std::size_t>(rva - s.virtual_address + s.raw_pointer);
        }
    }
    return 0;  // 0 is never a valid target here: it is the DOS header
}

std::string read_cstring(const std::string& data, std::size_t offset) {
    if (offset == 0 || offset >= data.size()) return {};
    const std::size_t end = data.find('\0', offset);
    return data.substr(offset, (end == std::string::npos ? data.size() : end) - offset);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

bool is_system_library(const std::string& dll) {
    const std::string name = lower(dll);

    // The Universal CRT and the API sets: `api-ms-win-*` and `ext-ms-win-*`
    // are resolved by the loader from the system, never shipped.
    if (name.rfind("api-ms-win-", 0) == 0 || name.rfind("ext-ms-win-", 0) == 0) return true;

    static const std::set<std::string> system_dlls = {
        "kernel32.dll",  "user32.dll",    "gdi32.dll",     "gdi32full.dll", "shell32.dll",
        "advapi32.dll",  "ole32.dll",     "oleaut32.dll",  "comdlg32.dll",  "comctl32.dll",
        "shlwapi.dll",   "version.dll",   "ws2_32.dll",    "wsock32.dll",   "crypt32.dll",
        "bcrypt.dll",    "ncrypt.dll",    "secur32.dll",   "wininet.dll",   "winhttp.dll",
        "opengl32.dll",  "glu32.dll",     "gdiplus.dll",   "dwmapi.dll",    "uxtheme.dll",
        "imm32.dll",     "winmm.dll",     "setupapi.dll",  "cfgmgr32.dll",  "userenv.dll",
        "psapi.dll",     "dbghelp.dll",   "rpcrt4.dll",    "ntdll.dll",     "msvcrt.dll",
        "ucrtbase.dll",  "d3d11.dll",     "dxgi.dll",      "d3dcompiler_47.dll",
        "hid.dll",       "dinput8.dll",   "xinput1_4.dll", "iphlpapi.dll",  "netapi32.dll",
        "powrprof.dll",  "propsys.dll",   "shcore.dll",    "combase.dll",   "kernelbase.dll",
    };
    return system_dlls.count(name) > 0;
}

std::vector<std::string> pe_imports(const fs::path& binary) {
    std::vector<std::string> out;

    std::ifstream in(binary, std::ios::binary);
    if (!in) return out;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string data = buf.str();

    if (data.size() < 0x40 || data[0] != 'M' || data[1] != 'Z') return out;

    std::uint32_t pe_offset = 0;
    if (!read_at(data, 0x3C, pe_offset)) return out;
    if (pe_offset + 24 > data.size()) return out;
    if (data.compare(pe_offset, 4, "PE\0\0", 4) != 0) return out;

    // COFF header: 20 bytes after the signature.
    std::uint16_t section_count = 0, optional_size = 0;
    if (!read_at(data, pe_offset + 6, section_count)) return out;
    if (!read_at(data, pe_offset + 20, optional_size)) return out;

    const std::size_t optional_offset = pe_offset + 24;
    std::uint16_t magic = 0;
    if (!read_at(data, optional_offset, magic)) return out;

    // PE32 keeps BaseOfData where PE32+ widened ImageBase, so the data
    // directories start 16 bytes later in the 64-bit form.
    const std::size_t directories =
        optional_offset + (magic == 0x20B ? 112 : 96);
    std::uint32_t import_rva = 0;
    if (!read_at(data, directories + 8, import_rva)) return out;
    if (import_rva == 0) return out;  // no imports at all

    std::vector<Section> sections;
    const std::size_t table = optional_offset + optional_size;
    for (std::uint16_t i = 0; i < section_count; ++i) {
        const std::size_t at = table + static_cast<std::size_t>(i) * 40;
        Section s;
        if (!read_at(data, at + 8, s.virtual_size)) break;
        if (!read_at(data, at + 12, s.virtual_address)) break;
        if (!read_at(data, at + 16, s.raw_size)) break;
        if (!read_at(data, at + 20, s.raw_pointer)) break;
        sections.push_back(s);
    }
    if (sections.empty()) return out;

    // Import descriptors are 20 bytes each and the list ends with an all-zero
    // one. The DLL's name is an RVA at offset 12.
    std::size_t at = rva_to_offset(sections, import_rva);
    if (at == 0) return out;
    for (int guard = 0; guard < 4096; ++guard, at += 20) {
        std::uint32_t name_rva = 0;
        std::uint32_t first_thunk = 0;
        if (!read_at(data, at + 12, name_rva)) break;
        if (!read_at(data, at + 16, first_thunk)) break;
        if (name_rva == 0 && first_thunk == 0) break;  // the terminator
        const std::string name = read_cstring(data, rva_to_offset(sections, name_rva));
        if (!name.empty() &&
            std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
    }
    return out;
}

}  // namespace mago
