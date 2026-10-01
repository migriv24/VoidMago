#include "mago/blast.hpp"

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

Version V(const char* s) {
    const auto v = Version::parse(s);
    CHECK(v.has_value());
    return *v;
}

Verdict verdict_for(const BlastReport& r, const std::string& project) {
    for (const auto& i : r.impacts) {
        if (i.project == project) return i.verdict;
    }
    CHECK(false);
    return Verdict::Untouched;
}

// THE RULE THAT KEEPS THE NUMBER SMALL.
//
// core -> view is shared, view -> app is static. An additive core change means
// view's own build output is byte-identical, so app inherits nothing through
// view; it takes the new DLL on its own direct edge and is not rebuilt.
void an_additive_shared_change_costs_a_reship_and_stops_there() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.3.0"));
    CHECK(r.change == Change::Compatible);
    CHECK(verdict_for(r, "view") == Verdict::Reship);
    CHECK(verdict_for(r, "app") == Verdict::Reship);
}

void a_breaking_shared_change_costs_a_rebuild() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("2.0.0"));
    CHECK(r.change == Change::Breaking);
    CHECK(verdict_for(r, "view") == Verdict::Rebuild);
    CHECK(verdict_for(r, "app") == Verdict::Rebuild);
}

void a_static_change_always_costs_a_rebuild_transitively() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r = analyse(Registry::scan(t.path()), "view", V("0.4.1"));
    CHECK(verdict_for(r, "app") == Verdict::Rebuild);
    CHECK(verdict_for(r, "core") == Verdict::Untouched);  // upstream is never affected
}

void a_change_can_be_declared_breaking_against_the_version_numbers() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r =
        analyse(Registry::scan(t.path()), "core", V("1.2.1"), Change::Breaking);
    CHECK(verdict_for(r, "app") == Verdict::Rebuild);
}

void a_version_a_consumer_forbids_is_blocked_not_merely_breaking() {
    TempDir t;
    mago::test::write_toy(t);
    // view pins >=1.1.0, so 1.0.0 is not a downgrade to reason about -- it is
    // a constraint violation, and saying "reship" would be wrong.
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.0.0"));
    CHECK(verdict_for(r, "view") == Verdict::Blocked);
}

void the_changed_project_never_appears_in_its_own_report() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.3.0"));
    for (const auto& i : r.impacts) CHECK(i.project != "core");
}

void a_leaf_application_has_no_blast_radius() {
    TempDir t;
    mago::test::write_toy(t);
    const BlastReport r = analyse(Registry::scan(t.path()), "app", V("3.0.0"));
    CHECK(r.needs_action().empty());
}

void the_worst_verdict_wins_when_two_paths_meet() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0")");
    t.project("mid", R"("name":"mid","display_name":"Mid","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"core","version":"*","link":"static"}])");
    // app reaches core twice: shared directly (a reship) and static through mid
    // (a rebuild). The harsher verdict has to survive.
    t.project("app", R"("name":"app","display_name":"App","kind":"application","version":"1.0.0",
        "requires":[
          {"name":"core","version":"*","link":"shared"},
          {"name":"mid","version":"*","link":"static"}])");
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.0.1"));
    CHECK(verdict_for(r, "app") == Verdict::Rebuild);
}

// A seam is a contract with two ends, and only one side usually writes it down.
void a_seam_is_symmetric_and_costs_only_a_retest() {
    TempDir t;
    t.project("emitter", R"("name":"emitter","display_name":"Emitter","kind":"application",
        "version":"1.0.0",
        "interacts_with":[{"name":"replayer","seam":"transcript","direction":"outbound"}])");
    t.project("replayer", R"("name":"replayer","display_name":"Replayer","kind":"application",
        "version":"1.0.0")");
    const Registry reg = Registry::scan(t.path());

    CHECK_EQ(reg.seam_neighbours("replayer").size(), std::size_t{1});
    CHECK_EQ(reg.seam_neighbours("emitter").size(), std::size_t{1});

    const BlastReport r = analyse(reg, "emitter", V("2.0.0"));
    CHECK_EQ(r.needs_action().size(), std::size_t{1});
    CHECK(r.needs_action().front().verdict == Verdict::Retest);
}

// One edge described twice must not be counted as both a dependency and a seam.
void a_pair_that_also_has_a_requires_edge_is_not_also_a_seam() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"app","seam":"c-abi","direction":"inbound"}])");
    t.project("app", R"("name":"app","display_name":"App","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":"*","link":"shared"}])");
    const Registry reg = Registry::scan(t.path());
    CHECK(reg.seam_neighbours("core").empty());

    const BlastReport r = analyse(reg, "core", V("1.0.1"));
    CHECK(verdict_for(r, "app") == Verdict::Reship);  // not also a retest
}

void the_report_serialises_to_json() {
    TempDir t;
    mago::test::write_toy(t);
    const std::string json = analyse(Registry::scan(t.path()), "core", V("2.0.0")).to_json();
    CHECK(json.find("\"report\": \"mago-blast/0.1\"") != std::string::npos);
    CHECK(json.find("\"verdict\": \"rebuild\"") != std::string::npos);
    CHECK(json.find("\"change\": \"breaking\"") != std::string::npos);
}

// AN INTERPRETED CONSUMER HAS NO BINARY TO REBUILD.
//
// FaultSack declared `link: "source"` on Void Core and Mago answered "compiles
// voidcore into its own binary; a changed voidcore is a changed faultsack" --
// about a pure-Python project that never loads libvoidcore at all. `source`
// means compiled in. `import` means the API is the contract and there is no
// build output on either side of it.
void an_imported_dependency_is_a_retest_and_not_a_rebuild() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0")");
    t.project("study", R"("name":"study","display_name":"Study","kind":"application",
        "version":"1.0.0",
        "requires":[{"name":"core","version":">=1.0.0","link":"import"}])");
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.3.0"));
    CHECK(verdict_for(r, "study") == Verdict::Retest);
}

// A breaking change to something you import is worse than a rebuild, not
// better: nothing recompiles, and the code has to be edited before it runs.
void a_breaking_import_still_costs_the_consumer_a_change() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0")");
    t.project("study", R"("name":"study","display_name":"Study","kind":"application",
        "version":"1.0.0",
        "requires":[{"name":"core","version":">=1.0.0","link":"import"}])");
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("2.0.0"));
    CHECK(r.change == Change::Breaking);
    CHECK(verdict_for(r, "study") == Verdict::Rebuild);
}

// The one fact a resolver cannot derive, declared where it travels.
void a_release_block_supplies_the_move_with_no_flags() {
    TempDir t;
    mago::test::write_toy(t);
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0",
        "provides":{"artifacts":["libcore-win-x64.dll"]},
        "release":{"previous":"1.1.0","change":"compatible"})");
    const BlastReport r = analyse(Registry::scan(t.path()), "core");
    CHECK(r.from_release);
    CHECK(r.from_version.str() == "1.1.0");
    CHECK(r.to_version && r.to_version->str() == "1.2.0");
    CHECK(verdict_for(r, "view") == Verdict::Reship);
}

// A declared `breaking` outranks what the numbers imply, because a version is
// a promise and not a proof.
void a_declared_change_kind_outranks_the_version_arithmetic() {
    TempDir t;
    mago::test::write_toy(t);
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0",
        "provides":{"artifacts":["libcore-win-x64.dll"]},
        "release":{"previous":"1.1.0","change":"breaking"})");
    const BlastReport r = analyse(Registry::scan(t.path()), "core");
    CHECK(r.change == Change::Breaking);
    CHECK(verdict_for(r, "view") == Verdict::Rebuild);
}

// Void Core's hand-written 0.2.14 broadcast gave verdicts to three
// repositories nobody had re-read. Absent is not the same as unaffected, and
// the difference has to be on the page.
void a_partner_with_no_manifest_is_unverified_rather_than_absent() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0",
        "interacts_with":[{"name":"reyna","seam":"transcript"}])");
    const BlastReport r = analyse(Registry::scan(t.path()), "core", V("1.3.0"));
    CHECK(r.unverified.size() == 1);
    CHECK(r.unverified.front().name == "reyna");
    CHECK(r.unverified.front().how == "transcript");
}

}  // namespace

int main() {
    an_additive_shared_change_costs_a_reship_and_stops_there();
    a_breaking_shared_change_costs_a_rebuild();
    a_static_change_always_costs_a_rebuild_transitively();
    a_change_can_be_declared_breaking_against_the_version_numbers();
    a_version_a_consumer_forbids_is_blocked_not_merely_breaking();
    the_changed_project_never_appears_in_its_own_report();
    a_leaf_application_has_no_blast_radius();
    the_worst_verdict_wins_when_two_paths_meet();
    a_seam_is_symmetric_and_costs_only_a_retest();
    a_pair_that_also_has_a_requires_edge_is_not_also_a_seam();
    the_report_serialises_to_json();
    an_imported_dependency_is_a_retest_and_not_a_rebuild();
    a_breaking_import_still_costs_the_consumer_a_change();
    a_release_block_supplies_the_move_with_no_flags();
    a_declared_change_kind_outranks_the_version_arithmetic();
    a_partner_with_no_manifest_is_unverified_rather_than_absent();
    mago::test::pass("blast");
    return 0;
}
