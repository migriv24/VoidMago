#include <string>

#include "mago/registry.hpp"
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

// Two projects can each be internally consistent and still contradict each
// other. Nobody notices this from inside one repository, which is exactly why
// the family needs something looking from above.
void contradicting_seam_names_are_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"b","seam":"c-abi"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"a","seam":"json"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "one of them is wrong"));
}

void a_missing_dependency_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "requires":[{"name":"ghost","link":"static"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "no manifest in this registry"));
}

void an_unsatisfiable_floor_is_an_error() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0")");
    t.project("app", R"("name":"app","display_name":"App","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":">=2.0.0","link":"shared"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "but core is at 1.0.0"));
}

void a_cycle_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"b","link":"static"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"a","link":"static"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "dependency cycle"));
}

void an_unparseable_manifest_is_reported_not_skipped_silently() {
    TempDir t;
    mago::test::write_toy(t);
    t.project("broken", R"("name":"broken","kind":"runtime","version":"not-a-version")");
    const Registry reg = Registry::scan(t.path());
    CHECK(!reg.contains("broken"));
    CHECK(has_finding(reg.check(), "error", "not a version"));
    CHECK(reg.contains("core"));  // one bad file does not sink the registry
}

void an_unknown_link_kind_is_refused() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"b","link":"telepathy"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "telepathy"));
}

void an_unknown_manifest_format_is_refused_rather_than_guessed_at() {
    TempDir t;
    const auto d = t.path() / "future";
    std::filesystem::create_directories(d);
    std::ofstream(d / "void.json")
        << R"({"manifest":"void/9.9","name":"future","kind":"runtime","version":"1.0.0"})";
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "manifest format"));
}

// `*` means every Void project, so a wildcard entry on the other side already
// mentions us and there is nothing one-sided about it.
void a_wildcard_interaction_counts_as_mentioning_everything() {
    TempDir t;
    t.project("mago", R"("name":"mago","display_name":"Mago","kind":"application",
        "version":"1.0.0",
        "interacts_with":[{"name":"*","seam":"void.json","direction":"inbound"}])");
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"mago","seam":"void.json","direction":"outbound"}])");
    CHECK(!has_finding(Registry::scan(t.path()).check(), "note", "does not mention"));
}

void a_void_project_without_a_manifest_is_visible() {
    TempDir t;
    mago::test::write_toy(t);
    std::filesystem::create_directories(t.path() / "VoidGhost");
    CHECK(has_finding(Registry::scan(t.path()).check(), "note", "carries no void.json"));
}

void an_application_that_is_linked_rather_than_installed_is_a_warning() {
    TempDir t;
    t.project("app", R"("name":"app","display_name":"App","kind":"application","version":"1.0.0")");
    t.project("other", R"("name":"other","display_name":"Other","kind":"application",
        "version":"1.0.0","requires":[{"name":"app","version":"*","link":"static"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "warn", "installed, not linked"));
}

// THE CHECK THAT WOULD HAVE CAUGHT A REAL ONE.
//
// Void Core shipped 0.2.14 with a `void.json` still reading 0.2.12 -- stale by
// two releases, and nothing caught it, because until now nothing read it
// against anything. Every verdict Mago computes is arithmetic over that
// number, so a stale one does not produce a missing answer; it produces a
// confident wrong one.
void a_repository_that_contradicts_its_own_manifest_is_an_error() {
    TempDir t;
    t.project("py", R"("name":"py","display_name":"Py","kind":"application","version":"0.2.12")");
    t.file("py", "pyproject.toml", "[project]\nname = \"py\"\nversion = \"0.2.14\"\n");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "pyproject.toml says 0.2.14"));
}

void a_repository_that_agrees_with_itself_says_nothing() {
    TempDir t;
    t.project("py", R"("name":"py","display_name":"Py","kind":"application","version":"0.2.14")");
    t.file("py", "pyproject.toml", "[project]\nversion = \"0.2.14\"\n");
    t.file("py", "CMakeLists.txt", "project(py)\nset(PY_VERSION \"0.2.14\")\n");
    CHECK(Registry::scan(t.path()).check().empty());
}

// A project that versions a subcomponent separately gets to say so, rather
// than being told off on every run until somebody turns the check off for
// everybody.
void version_sources_can_narrow_or_silence_the_check() {
    TempDir t;
    t.project("py", R"("name":"py","display_name":"Py","kind":"application","version":"0.2.12",
        "version_sources":[])");
    t.file("py", "pyproject.toml", "[project]\nversion = \"9.9.9\"\n");
    CHECK(Registry::scan(t.path()).check().empty());
}

void a_release_block_that_describes_another_version_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.2.0",
        "release":{"version":"1.3.0","previous":"1.1.0","change":"compatible"})");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error",
                      "describes the version beside it"));
}

void a_release_that_moved_backwards_is_an_error() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.2.0",
        "release":{"previous":"1.3.0","change":"compatible"})");
    CHECK(has_finding(Registry::scan(t.path()).check(), "error", "not before release.version"));
}

// A partner nobody can read is not a partner in agreement; it is a verdict
// that cannot be computed, and saying nothing about it reads as "unaffected".
void a_partner_with_no_manifest_is_reported_rather_than_skipped() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"reyna","seam":"transcript"}])");
    CHECK(has_finding(Registry::scan(t.path()).check(), "note", "cannot be computed"));
}

void a_healthy_family_reports_nothing() {
    TempDir t;
    mago::test::write_toy(t);
    CHECK(Registry::scan(t.path()).check().empty());
}

}  // namespace

int main() {
    contradicting_seam_names_are_an_error();
    a_missing_dependency_is_an_error();
    an_unsatisfiable_floor_is_an_error();
    a_cycle_is_an_error();
    an_unparseable_manifest_is_reported_not_skipped_silently();
    an_unknown_link_kind_is_refused();
    an_unknown_manifest_format_is_refused_rather_than_guessed_at();
    a_wildcard_interaction_counts_as_mentioning_everything();
    a_void_project_without_a_manifest_is_visible();
    an_application_that_is_linked_rather_than_installed_is_a_warning();
    a_repository_that_contradicts_its_own_manifest_is_an_error();
    a_repository_that_agrees_with_itself_says_nothing();
    version_sources_can_narrow_or_silence_the_check();
    a_release_block_that_describes_another_version_is_an_error();
    a_release_that_moved_backwards_is_an_error();
    a_partner_with_no_manifest_is_reported_rather_than_skipped();
    a_healthy_family_reports_nothing();
    mago::test::pass("doctor");
    return 0;
}
