// The table, the triangle, and the one asymmetry the family really has.

#include "mago/matrix.hpp"

#include <string>

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

bool contains(const std::vector<std::string>& xs, const std::string& needle) {
    for (const auto& x : xs) {
        if (x.find(needle) != std::string::npos) return true;
    }
    return false;
}

// AB = BA. The pair is ONE record, read from either side.
void a_cell_is_the_same_object_from_both_sides() {
    TempDir t;
    mago::test::write_toy(t);
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});

    const std::size_t core = m.index_of("core");
    const std::size_t view = m.index_of("view");
    CHECK(m.at(core, view).pairing == m.at(view, core).pairing);
    CHECK(m.at(core, view).pairing->bond == Bond::Requires);
}

// ...and the direction is the half that flips. M = S + A: the relationship is
// the symmetric part, the arrow is the antisymmetric part.
void the_direction_is_the_antisymmetric_part() {
    TempDir t;
    mago::test::write_toy(t);
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});

    const std::size_t core = m.index_of("core");
    const std::size_t view = m.index_of("view");  // view requires core
    CHECK(m.at(view, core).direction == +1);
    CHECK(m.at(core, view).direction == -1);
    CHECK(m.at(view, core).direction == -m.at(core, view).direction);
}

// Only the strict upper triangle is stored, which is the point of noticing the
// symmetry in the first place.
void only_the_upper_triangle_exists() {
    TempDir t;
    mago::test::write_toy(t);
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});
    const std::size_t n = m.size();
    CHECK(n == 3);
    CHECK(m.pairings().size() == n * (n - 1) / 2);
    CHECK(m.cells() == 3);
}

void the_diagonal_is_not_an_interaction() {
    TempDir t;
    mago::test::write_toy(t);
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});
    const std::size_t core = m.index_of("core");
    CHECK(m.at(core, core).empty());
    CHECK(m.diagonal()[core].name == "core");
    CHECK(m.diagonal()[core].degree == 2);  // view and app both reach it
}

// The bond is a join over evidence, on a chain. A pair described BOTH ways --
// which `mago doctor` reports as drift-prone -- takes the tighter one.
void requires_outranks_a_seam_and_the_duplication_is_recorded() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"app","seam":"c-abi"}])");
    t.project("app", R"("name":"app","display_name":"App","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":"*","link":"shared"}])");
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});
    const Cell c = m.find("app", "core");
    CHECK(c.pairing->bond == Bond::Requires);
    CHECK(c.pairing->seam == "c-abi");
    CHECK(contains(c.pairing->why, "described twice"));
}

// The cost chain, from the one table both this and `mago blast` read.
void the_cost_comes_from_the_link_kind() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime","version":"1.0.0")");
    t.project("shared_one", R"("name":"shared_one","display_name":"Shared","kind":"application",
        "version":"1.0.0","requires":[{"name":"core","version":"*","link":"shared"}])");
    t.project("static_one", R"("name":"static_one","display_name":"Static","kind":"application",
        "version":"1.0.0","requires":[{"name":"core","version":"*","link":"static"}])");
    t.project("import_one", R"("name":"import_one","display_name":"Import","kind":"application",
        "version":"1.0.0","requires":[{"name":"core","version":"*","link":"import"}])");
    const Matrix m = Matrix::build(Registry::scan(t.path()), Family{});
    CHECK(m.find("shared_one", "core").pairing->cost == Verdict::Reship);
    CHECK(m.find("static_one", "core").pairing->cost == Verdict::Rebuild);
    CHECK(m.find("import_one", "core").pairing->cost == Verdict::Retest);
    CHECK(cost_of_link("shared", Change::Breaking) == Verdict::Rebuild);
}

// A pair nobody's manifest mentions, that a person knows something about. The
// authored document is the only place this relationship can exist.
void an_authored_pair_is_a_bond_of_its_own() {
    TempDir t;
    mago::test::write_toy(t);
    Family f;
    AuthoredPairing p;
    p.a = "app";
    p.b = "core";
    p.note = "they share an asset folder nobody declares";
    f.set_pairing(p);

    const Matrix m = Matrix::build(Registry::scan(t.path()), f);
    const Cell c = m.find("core", "app");
    CHECK(c.pairing->authored());
    CHECK(c.pairing->note == "they share an asset folder nobody declares");
    CHECK(m.authored() == 1);
}

// Two manifests naming one seam differently is the contradiction `mago doctor`
// has reported since its first run. Authoring the agreed name settles it.
void an_authored_seam_settles_a_disputed_one() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"b","seam":"c-abi"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"runtime","version":"1.0.0",
        "interacts_with":[{"name":"a","seam":"state document"}])");
    const Registry reg = Registry::scan(t.path());

    CHECK(Matrix::build(reg, Family{}).find("a", "b").pairing->disputed);

    Family f;
    AuthoredPairing p;
    p.a = "a";
    p.b = "b";
    p.seam = "state document";
    f.set_pairing(p);
    const Matrix matrix = Matrix::build(reg, f);  // a Cell points into its Matrix
    const Cell settled = matrix.find("a", "b");
    CHECK(!settled.pairing->disputed);
    CHECK(settled.pairing->seam == "state document");
    CHECK(contains(settled.pairing->why, "settles the name"));

    // doctor still calls the manifests wrong -- they are -- but now carries
    // the answer instead of only the argument.
    bool has_fix = false;
    for (const auto& finding : reg.check(&f)) {
        if (finding.level == "error" && finding.message.find("so the fix is known") !=
                                            std::string::npos) {
            has_fix = true;
        }
    }
    CHECK(has_fix);
}

// {b,a} and {a,b} are one record, in the document as well as in the matrix.
void the_authored_document_round_trips_in_canonical_order() {
    TempDir t;
    Family f;
    AuthoredPairing p;
    p.a = "zeta";
    p.b = "alpha";  // written backwards on purpose
    p.note = "one record";
    f.set_pairing(p);
    CHECK(f.pairings().size() == 1);
    CHECK(f.pairings().front().a == "alpha");
    CHECK(f.pairing("zeta", "alpha") != nullptr);
    CHECK(f.pairing("alpha", "zeta") != nullptr);

    const auto path = t.path() / "family.json";
    f.save(path);
    const Family back = Family::load(path);
    CHECK(back.pairings().size() == 1);
    CHECK(back.pairing("zeta", "alpha")->note == "one record");

    // Writing the same pair again replaces it rather than growing the file.
    Family again = back;
    AuthoredPairing edited = *back.pairing("alpha", "zeta");
    edited.note = "edited";
    again.set_pairing(edited);
    CHECK(again.pairings().size() == 1);

    // And clearing it removes the record: an empty entry and no entry mean
    // the same thing.
    edited.note.clear();
    edited.seam.clear();
    edited.shares.clear();
    edited.status.clear();
    again.set_pairing(edited);
    CHECK(again.pairings().empty());
}

void a_missing_document_is_an_empty_family_not_an_error() {
    TempDir t;
    const Family f = Family::load(t.path() / "nothing-here.json");
    CHECK(f.pairings().empty());
    CHECK(f.projects().empty());
}

// The GUI's contract: axis order, upper triangle only, and the stats it shows.
void the_json_is_the_desktop_contract() {
    TempDir t;
    mago::test::write_toy(t);
    const std::string json = Matrix::build(Registry::scan(t.path()), Family{}).to_json();
    CHECK(json.find("\"report\": \"mago-matrix/0.1\"") != std::string::npos);
    CHECK(json.find("\"axis\"") != std::string::npos);
    CHECK(json.find("\"direction\"") != std::string::npos);
    CHECK(json.find("\"cells\": 3") != std::string::npos);
}

}  // namespace

int main() {
    a_cell_is_the_same_object_from_both_sides();
    the_direction_is_the_antisymmetric_part();
    only_the_upper_triangle_exists();
    the_diagonal_is_not_an_interaction();
    requires_outranks_a_seam_and_the_duplication_is_recorded();
    the_cost_comes_from_the_link_kind();
    an_authored_pair_is_a_bond_of_its_own();
    an_authored_seam_settles_a_disputed_one();
    the_authored_document_round_trips_in_canonical_order();
    a_missing_document_is_an_empty_family_not_an_error();
    the_json_is_the_desktop_contract();
    mago::test::pass("matrix");
    return 0;
}
