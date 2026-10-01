#include "mago/vendors.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#include "mago/json_out.hpp"

namespace fs = std::filesystem;

namespace mago {
namespace {

// Libraries are named by their authors, not by a schema: "cJSON", "cjson" and
// "CJSON" are one thing and must group as one.
std::string fold(std::string s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

void add_unique(std::vector<std::string>& xs, const std::string& value) {
    if (value.empty()) return;
    if (std::find(xs.begin(), xs.end(), value) == xs.end()) xs.push_back(value);
}

}  // namespace

const std::vector<std::string>& vendor_directories() {
    static const std::vector<std::string> dirs = {"vendor", "vendors", "third_party",
                                                  "third-party", "external", "extern", "deps"};
    return dirs;
}

bool version_appears_in(const fs::path& file, const std::string& version) {
    std::error_code ec;
    if (version.empty() || !fs::is_regular_file(file, ec)) return false;
    if (fs::file_size(file, ec) > 4u * 1024 * 1024 || ec) return false;

    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    if (text.find(version) != std::string::npos) return true;

    // Split across macros: `#define CJSON_VERSION_MAJOR 1` … `_PATCH 18`. Any
    // non-digit run may separate the parts, which is loose on purpose -- see
    // the header.
    std::string pattern;
    for (char c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            pattern += c;
        } else if (!pattern.empty() && pattern.back() != '@') {
            pattern += '@';
        }
    }
    if (pattern.empty()) return false;

    std::string re_text;
    for (char c : pattern) {
        re_text += (c == '@') ? std::string(R"([^0-9]{1,40})") : std::string(1, c);
    }
    try {
        return std::regex_search(text, std::regex(re_text));
    } catch (const std::regex_error&) {
        return false;
    }
}

std::vector<const Library*> Inventory::drifting() const {
    std::vector<const Library*> out;
    for (const auto& lib : libraries) {
        if (lib.drifts()) out.push_back(&lib);
    }
    return out;
}

std::vector<const Library*> Inventory::must_ship() const {
    std::vector<const Library*> out;
    for (const auto& lib : libraries) {
        const bool ships = std::any_of(lib.uses.begin(), lib.uses.end(), [](const VendorUse& u) {
            return u.declared.link == "shared" || u.declared.link == "toolchain";
        });
        if (ships) out.push_back(&lib);
    }
    return out;
}

std::size_t Inventory::declarations() const {
    std::size_t n = 0;
    for (const auto& lib : libraries) n += lib.uses.size();
    return n;
}

Inventory take_inventory(const Registry& reg) {
    std::map<std::string, Library> by_key;  // ordered, so output is stable

    for (const auto& [name, m] : reg.projects()) {
        for (const auto& v : m.vendors) {
            Library& lib = by_key[fold(v.name)];
            if (lib.name.empty()) lib.name = v.name;
            lib.uses.push_back({name, v});
            add_unique(lib.versions, v.version);
            add_unique(lib.licenses, v.license);
        }
    }

    Inventory inventory;
    for (auto& [_, lib] : by_key) {
        std::sort(lib.versions.begin(), lib.versions.end());
        std::sort(lib.licenses.begin(), lib.licenses.end());
        inventory.libraries.push_back(std::move(lib));
    }

    // What is on disk that nobody declared. One level deep inside the
    // conventional directories: `vendor/glfw` is a library, `vendor/glfw/src`
    // is that library's business.
    std::error_code ec;
    for (const auto& [name, m] : reg.projects()) {
        for (const auto& dir : vendor_directories()) {
            const fs::path root = m.path / dir;
            if (!fs::is_directory(root, ec)) continue;

            for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                           ec),
                 end;
                 it != end; it.increment(ec)) {
                if (ec) break;
                const std::string entry = it->path().filename().string();
                if (entry == "README.md" || entry == "LICENSE" || entry == "LICENSE.md") continue;

                const std::string relative = dir + "/" + entry;
                const bool declared =
                    std::any_of(m.vendors.begin(), m.vendors.end(), [&](const Vendor& v) {
                        if (v.path.empty()) return false;
                        // A declared `vendor/cJSON.c` accounts for
                        // `vendor/cJSON.h` too: one library, several files.
                        const std::string declared_stem =
                            fs::path(v.path).stem().string();
                        const std::string entry_stem = fs::path(entry).stem().string();
                        return v.path == relative || fold(declared_stem) == fold(entry_stem);
                    });
                if (!declared) inventory.undeclared.push_back({name, relative});
            }
        }
    }
    return inventory;
}

std::string Inventory::to_json(int indent) const {
    JsonWriter w(indent);
    w.begin_object();
    w.field("report", "mago-vendors/0.1");
    w.key("libraries").begin_array();
    for (const auto& lib : libraries) {
        w.begin_object();
        w.field("name", lib.name);
        w.string_array("versions", lib.versions);
        w.string_array("licenses", lib.licenses);
        w.key("drifts").value(lib.drifts());
        w.key("uses").begin_array();
        for (const auto& use : lib.uses) {
            w.begin_object();
            w.field("project", use.project);
            w.field("version", use.declared.version);
            w.field("license", use.declared.license);
            w.field("link", use.declared.link);
            w.field("path", use.declared.path);
            w.field("upstream", use.declared.upstream);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();

    w.key("undeclared").begin_array();
    for (const auto& u : undeclared) {
        w.begin_object();
        w.field("project", u.project);
        w.field("path", u.path);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

}  // namespace mago
