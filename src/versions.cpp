#include "mago/versions.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace mago {
namespace {

// A version literal is small and always near the top of these files, so a
// whole-file read is bounded by choice rather than by hope.
constexpr std::size_t kMaxBytes = 512 * 1024;

std::optional<std::string> read_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt;
    if (fs::file_size(p, ec) > kMaxBytes || ec) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// The first match of `re`, with the version in group 1 and the whole matched
// line reported as evidence.
std::optional<VersionClaim> first_match(const std::string& text, const std::regex& re,
                                        const std::string& file) {
    std::smatch m;
    if (!std::regex_search(text, m, re)) return std::nullopt;
    VersionClaim claim;
    claim.file = file;
    claim.where = trim(m.str(0));
    claim.version = Version::parse(m.str(1));
    return claim;
}

// TOML: the project's own `version = "x.y.z"`. Deliberately the FIRST one --
// a later `version` inside `[tool.…]` or a dependency table belongs to
// something else, and guessing which would be worse than reading one.
std::optional<VersionClaim> toml_claim(const fs::path& dir, const std::string& file) {
    const auto text = read_file(dir / file);
    if (!text) return std::nullopt;
    static const std::regex re(R"RX((?:^|\n)[ \t]*version[ \t]*=[ \t]*"([^"]+)")RX");
    return first_match(*text, re, file);
}

// package.json's own `"version"`, taken from the top of the file so a
// dependency's pin cannot be mistaken for the package's version.
std::optional<VersionClaim> package_json_claim(const fs::path& dir) {
    const auto text = read_file(dir / "package.json");
    if (!text) return std::nullopt;
    static const std::regex re(R"RX("version"[ \t]*:[ \t]*"([^"]+)")RX");
    return first_match(*text, re, "package.json");
}

// CMake writes a version two ways, and the family uses both: `project(name
// VERSION x.y.z)` in Void Palabra, `set(VOIDALLOMONE_VERSION "0.1.0")` in
// Void Allomone and here.
std::optional<VersionClaim> cmake_claim(const fs::path& dir) {
    const auto text = read_file(dir / "CMakeLists.txt");
    if (!text) return std::nullopt;
    static const std::regex set_re(
        R"RX((?:^|\n)[ \t]*set[ \t]*\([ \t]*[A-Za-z_][A-Za-z0-9_]*_VERSION[ \t]+"?([0-9]+\.[0-9]+\.[0-9]+))RX");
    if (auto claim = first_match(*text, set_re, "CMakeLists.txt")) return claim;
    static const std::regex project_re(
        R"RX((?:^|\n)[ \t]*project[ \t]*\([^)]*?VERSION[ \t]+([0-9]+\.[0-9]+\.[0-9]+))RX");
    return first_match(*text, project_re, "CMakeLists.txt");
}

}  // namespace

const std::vector<std::string>& default_version_sources() {
    static const std::vector<std::string> files = {"CMakeLists.txt", "pyproject.toml",
                                                   "package.json", "Cargo.toml"};
    return files;
}

std::vector<VersionClaim> version_claims(const fs::path& dir) {
    std::vector<VersionClaim> out;
    for (const auto& file : default_version_sources()) {
        std::optional<VersionClaim> claim;
        if (file == "CMakeLists.txt") {
            claim = cmake_claim(dir);
        } else if (file == "package.json") {
            claim = package_json_claim(dir);
        } else {
            claim = toml_claim(dir, file);
        }
        if (claim) out.push_back(std::move(*claim));
    }
    return out;
}

}  // namespace mago
