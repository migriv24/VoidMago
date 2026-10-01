// Third-party code: what is in these binaries that the family did not write.

#include "mago/vendors.hpp"

#include <fstream>
#include <string>

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

bool has_finding(const std::vector<Finding>& fs, const std::string& level,
                 const std::string& needle) {
    for (const auto& f : fs) {
        if (f.level == level && f.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

const Library* library(const Inventory& inv, const std::string& name) {
    for (const auto& lib : inv.libraries) {
        if (lib.name == name) return &lib;
    }
    return nullptr;
}

// THE FINDING THIS EXISTS FOR. cJSON is vendored in six repositories; they
// agree today by discipline and nothing checks it. Two of those copies are
// compiled into one executable.
void one_library_at_two_versions_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "vendors":[{"name":"cJSON","version":"1.7.18","license":"MIT","link":"source"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"application","version":"1.0.0",
        "requires":[{"name":"a","version":"*","link":"static"}],
        "vendors":[{"name":"cJSON","version":"1.7.15","license":"MIT","link":"source"}])");

    const Registry reg = Registry::scan(t.path());
    const Inventory inv = take_inventory(reg);

    const Library* lib = library(inv, "cJSON");
    CHECK(lib != nullptr);
    CHECK(lib->drifts());
    CHECK(lib->versions.size() == 2);
    CHECK(inv.drifting().size() == 1);
    CHECK(has_finding(reg.check(), "error", "vendored at more than one version"));
}

void one_library_at_one_version_is_silent() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "vendors":[{"name":"cJSON","version":"1.7.18","license":"MIT","link":"source"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"application","version":"1.0.0",
        "vendors":[{"name":"CJSON","version":"1.7.18","license":"MIT","link":"source"}])");

    const Registry reg = Registry::scan(t.path());
    const Inventory inv = take_inventory(reg);
    // Named by their authors, not by a schema: "cJSON" and "CJSON" are one
    // library and must group as one, or drift hides behind spelling.
    CHECK(inv.libraries.size() == 1);
    CHECK(!inv.libraries.front().drifts());
    CHECK(!has_finding(reg.check(), "error", "vendored at more than one version"));
}

// Code on disk that no manifest accounts for: no version to compare, no
// licence to publish, and nothing to notice when it changes.
void vendored_code_nobody_declared_is_reported() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0")");
    t.file("a/vendor/sqlite", "sqlite3.c", "-- not really sqlite");
    t.file("a/vendor", "README.md", "why these are here");

    const Inventory inv = take_inventory(Registry::scan(t.path()));
    CHECK(inv.undeclared.size() == 1);  // README.md is not a library
    CHECK(inv.undeclared.front().path == "vendor/sqlite");
    CHECK(has_finding(Registry::scan(t.path()).check(), "note", "declares no vendors[] entry"));
}

// One declaration covers a library's several files: `vendor/cJSON.c` accounts
// for `vendor/cJSON.h` beside it.
void one_declaration_covers_a_librarys_files() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"cJSON","version":"1.7.18","license":"MIT",
                    "path":"vendor/cJSON.c","link":"source"}])");
    t.file("a/vendor", "cJSON.c", "x");
    t.file("a/vendor", "cJSON.h", "x");
    CHECK(take_inventory(Registry::scan(t.path())).undeclared.empty());
}

void a_vendored_path_that_is_not_there_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"Ghost","version":"1.0","license":"MIT",
                    "path":"vendor/ghost.c","link":"source"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "not in the repository"));
}

// A licence that is not recorded cannot appear in a notices file, and a
// notices file is a legal obligation rather than a nicety.
void a_library_with_no_licence_is_a_warning() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"Mystery","version":"1.0","link":"source"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "warn", "cannot appear in a notices"));
}

// The class of thing that broke Void Hormiga's first package: a file from the
// compiler's own directory, present on the build machine and nowhere else.
void a_toolchain_library_says_so() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"libwinpthread","version":"12","license":"MIT",
                    "link":"toolchain"}])");
    const Registry reg = Registry::scan(t.path());
    CHECK(has_finding(reg.check(), "note", "from the toolchain"));
    CHECK(take_inventory(reg).must_ship().size() == 1);
}

// The declared version is checked against the vendored copy, so a manifest
// cannot claim one thing while the source says another.
void the_declared_version_is_checked_against_the_source() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"cJSON","version":"1.7.18","license":"MIT",
                    "path":"vendor/cJSON.h","version_source":"vendor/cJSON.h",
                    "link":"source"}])");
    // Split across macros, the way cJSON really writes it.
    t.file("a/vendor", "cJSON.h",
           "#define CJSON_VERSION_MAJOR 1\n#define CJSON_VERSION_MINOR 7\n"
           "#define CJSON_VERSION_PATCH 18\n");
    CHECK(!has_finding(Registry::scan(t.path()).check(), "error", "does not carry that version"));

    TempDir t2;
    t2.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"SQLite","version":"3.45.1","license":"blessing",
                    "path":"vendor/sqlite3.h","version_source":"vendor/sqlite3.h",
                    "link":"source"}])");
    t2.file("a/vendor", "sqlite3.h", "#define SQLITE_VERSION \"3.41.0\"\n");
    CHECK(has_finding(Registry::scan(t2.path()).check(), "error", "does not carry that version"));
}

void the_version_matcher_takes_both_spellings() {
    TempDir t;
    t.file("x", "literal.h", "#define SQLITE_VERSION \"3.45.1\"");
    t.file("x", "split.h", "#define V_MAJOR 1\n#define V_MINOR 7\n#define V_PATCH 18\n");
    t.file("x", "wrong.h", "#define V_MAJOR 2\n#define V_MINOR 0\n#define V_PATCH 0\n");

    CHECK(version_appears_in(t.path() / "x" / "literal.h", "3.45.1"));
    CHECK(version_appears_in(t.path() / "x" / "split.h", "1.7.18"));
    CHECK(!version_appears_in(t.path() / "x" / "wrong.h", "1.7.18"));
    CHECK(!version_appears_in(t.path() / "x" / "missing.h", "1.7.18"));
}

// An unknown link kind is refused rather than stored, the same way a
// requires[] link is.
void an_unknown_vendor_link_is_refused() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "vendors":[{"name":"X","link":"osmosis"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "osmosis"));
}

}  // namespace

int main() {
    one_library_at_two_versions_is_an_error();
    one_library_at_one_version_is_silent();
    vendored_code_nobody_declared_is_reported();
    one_declaration_covers_a_librarys_files();
    a_vendored_path_that_is_not_there_is_an_error();
    a_library_with_no_licence_is_a_warning();
    a_toolchain_library_says_so();
    the_declared_version_is_checked_against_the_source();
    the_version_matcher_takes_both_spellings();
    an_unknown_vendor_link_is_refused();
    mago::test::pass("vendors");
    return 0;
}
