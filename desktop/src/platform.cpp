#include "platform.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace magod {
namespace {

#ifdef _WIN32

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr,
                                      0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// The shell's folder browser. BIF_NEWDIALOGSTYLE is what makes it the modern
// resizable one with a text field, rather than the 1995 tree.
fs::path browse_for_folder(const std::string& title, const fs::path& start_in) {
    const std::wstring wtitle = widen(title);
    const std::wstring wstart = start_in.empty() ? std::wstring() : start_in.wstring();

    BROWSEINFOW bi{};
    bi.lpszTitle = wtitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    if (!wstart.empty()) {
        // Open where the person already is. The callback is the only way the
        // shell lets you preselect a plain path.
        bi.lParam = reinterpret_cast<LPARAM>(wstart.c_str());
        bi.lpfn = [](HWND hwnd, UINT msg, LPARAM, LPARAM data) -> int {
            if (msg == BFFM_INITIALIZED && data) {
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
            }
            return 0;
        };
    }

    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return {};  // cancelled

    std::array<wchar_t, MAX_PATH + 1> buffer{};
    const bool ok = SHGetPathFromIDListW(id, buffer.data());
    CoTaskMemFree(id);
    if (!ok) return {};
    return fs::path(buffer.data());
}

CommandResult run_captured(const std::wstring& command_line) {
    CommandResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return result;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;   // one stream: the order things happened in
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring mutable_line = command_line;  // CreateProcessW may write to it
    const BOOL started =
        CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        return result;
    }
    result.ran = true;

    std::array<char, 4096> chunk{};
    DWORD got = 0;
    while (ReadFile(read_end, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) &&
           got > 0) {
        result.output.append(chunk.data(), got);
    }
    CloseHandle(read_end);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exit_code = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

fs::path this_executable() {
    std::array<wchar_t, MAX_PATH + 1> buffer{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH);
    if (n == 0) return {};
    return fs::path(std::wstring(buffer.data(), n));
}

#endif  // _WIN32

}  // namespace

fs::path pick_folder(const std::string& title, const fs::path& start_in) {
#ifdef _WIN32
    // The shell dialog wants an apartment-threaded COM context, and asking
    // twice is harmless -- RPC_E_CHANGED_MODE just means somebody already set
    // one up, which is fine because we then leave it alone.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const fs::path chosen = browse_for_folder(title, start_in);
    if (SUCCEEDED(init)) CoUninitialize();
    return chosen;
#else
    (void)title;
    (void)start_in;
    return {};  // no native picker; the text field is the way in
#endif
}

fs::path find_mago() {
    std::error_code ec;
#ifdef _WIN32
    const fs::path here = this_executable().parent_path();
    const char* name = "mago.exe";
#else
    const fs::path here = fs::current_path(ec);
    const char* name = "mago";
#endif
    // Beside the desktop binary first -- one build tree builds both, so this
    // is the normal case. Then the CLI-only tree, for somebody who configured
    // the desktop separately: this executable is at <repo>/<tree>/bin/, so two
    // steps up is the repository.
    const fs::path repo = here.parent_path().parent_path();
    const std::array<fs::path, 4> candidates = {
        here / name,
        repo / "build" / "bin" / name,
        repo / "build-desktop" / "bin" / name,
        fs::current_path(ec) / "build" / "bin" / name,
    };
    for (const auto& candidate : candidates) {
        if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal();
    }
    return {};
}

CommandResult run_mago(const std::string& args) {
    const fs::path binary = find_mago();
    CommandResult result;
    if (binary.empty()) {
        result.output =
            "mago: the CLI binary was not found beside this window.\n"
            "Build it with:  cmake -S . -B build && cmake --build build\n";
        return result;
    }
#ifdef _WIN32
    std::wstring line = L"\"" + binary.wstring() + L"\" " + widen(args);
    CommandResult ran = run_captured(line);
    if (!ran.ran) ran.output = "mago: could not start " + binary.string() + "\n";
    return ran;
#else
    const std::string command = "\"" + binary.string() + "\" " + args + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "mago: could not start " + binary.string() + "\n";
        return result;
    }
    result.ran = true;
    std::array<char, 4096> chunk{};
    while (std::fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) {
        result.output += chunk.data();
    }
    result.exit_code = pclose(pipe);
    return result;
#endif
}

}  // namespace magod
