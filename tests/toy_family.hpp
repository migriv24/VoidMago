// Test scaffolding: a miniature Void family in a temp directory.
//
// Most tests build one of these rather than reading the real repositories, so
// a version bump in Void Core cannot make the suite go red. family_smoke is
// the deliberate exception.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond       \
                      << "\n";                                                         \
            std::exit(1);                                                              \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        const auto lhs_ = (a);                                                         \
        const auto rhs_ = (b);                                                         \
        if (!(lhs_ == rhs_)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a          \
                      << "\n  expected: " << rhs_ << "\n  actual:   " << lhs_ << "\n"; \
            std::exit(1);                                                              \
        }                                                                              \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType, needle)                                       \
    do {                                                                                \
        bool threw_ = false;                                                            \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const ExceptionType& e) {                                              \
            threw_ = true;                                                              \
            if (std::string(e.what()).find(needle) == std::string::npos) {              \
                std::cerr << "FAIL " << __FILE__ << ":" << __LINE__                     \
                          << "  message lacks \"" << needle << "\": " << e.what()       \
                          << "\n";                                                      \
                std::exit(1);                                                           \
            }                                                                           \
        }                                                                               \
        if (!threw_) {                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #expr        \
                      << " did not throw " << #ExceptionType << "\n";                   \
            std::exit(1);                                                               \
        }                                                                               \
    } while (0)

namespace mago::test {

// A temp directory that removes itself, so a failing test leaves no litter.
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 32; ++attempt) {
            path_ = base / ("mago-test-" + std::to_string(rd()));
            std::error_code ec;
            if (std::filesystem::create_directory(path_, ec)) return;
        }
        std::cerr << "could not create a temp directory\n";
        std::exit(1);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    // Write one project's manifest. `body` is the JSON object's contents,
    // without the braces or the `manifest` field.
    void project(std::string_view dir, std::string_view body) const {
        const auto d = path_ / dir;
        std::filesystem::create_directories(d);
        std::ofstream out(d / "void.json", std::ios::binary);
        out << "{\"manifest\":\"void/0.1\"," << body << "}";
    }

    // Any other file in a project's root -- a pyproject.toml or a
    // CMakeLists.txt, for the checks that read a repository's own idea of
    // what version it is.
    void file(std::string_view dir, std::string_view name, std::string_view content) const {
        const auto d = path_ / dir;
        std::filesystem::create_directories(d);
        std::ofstream out(d / name, std::ios::binary);
        out << content;
    }

private:
    std::filesystem::path path_;
};

// The shape of the real family in miniature: a C runtime, a static library
// over it, and an application that links both -- one shared, one static.
//
// The app ships a NESTED folder on purpose. Every real declaration in the
// family was one top-level segment until Void Hormiga declared
// `vendor/fonts/`, which has an interior separator to get wrong -- and the
// NSIS emitter got it wrong, silently, all the way onto a stranger's
// machine. The toy carries one now so nothing downstream can regress to
// only ever seeing the easy shape.
inline void write_toy(const TempDir& t) {
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0",
        "provides":{"artifacts":["libcore-win-x64.dll"]},
        "platforms":["windows-x64"])");
    t.project("view", R"("name":"view","display_name":"View","kind":"runtime",
        "version":"0.4.0",
        "requires":[{"name":"core","version":">=1.1.0","link":"shared"}])");
    t.project("app", R"("name":"app","display_name":"App","kind":"application",
        "version":"2.0.0",
        "requires":[
          {"name":"core","version":">=1.0.0","link":"shared"},
          {"name":"view","version":"*","link":"static"}],
        "provides":{"gui":"app","cli":"app-cli",
          "ships_beside_binary":["docs/","vendor/fonts/"]},
        "platforms":["windows-x64"])");
}

inline void pass(const char* name) {
    std::cout << "ok  " << name << "\n";
}

}  // namespace mago::test
