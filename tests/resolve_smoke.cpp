#include "mago/resolve.hpp"

#include <algorithm>
#include <set>

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

std::set<std::string> names(const std::vector<Resolved>& xs) {
    std::set<std::string> out;
    for (const auto& x : xs) out.insert(x.name);
    return out;
}

std::size_t index_of(const std::vector<Resolved>& xs, const std::string& name) {
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (xs[i].name == name) return i;
    }
    CHECK(false);
    return 0;
}

void static_dependencies_are_absorbed_not_shipped() {
    TempDir t;
    mago::test::write_toy(t);
    const Plan plan = resolve(Registry::scan(t.path()), "app", "windows-x64");

    // `view` is compiled into `app`, so it is not a file anybody installs.
    CHECK(names(plan.payload) == (std::set<std::string>{"core", "app"}));
    CHECK(names(plan.absorbed) == (std::set<std::string>{"view"}));
}

void build_order_puts_dependencies_first() {
    TempDir t;
    mago::test::write_toy(t);
    const Plan plan = resolve(Registry::scan(t.path()), "app");
    CHECK(index_of(plan.build_order, "core") < index_of(plan.build_order, "view"));
    CHECK_EQ(plan.build_order.back().name, std::string("app"));
}

void the_platform_picks_the_artifact() {
    TempDir t;
    mago::test::write_toy(t);
    const Plan plan = resolve(Registry::scan(t.path()), "app", "windows-x64");
    for (const auto& r : plan.payload) {
        if (r.name != "core") continue;
        CHECK_EQ(r.files.size(), std::size_t{1});
        CHECK_EQ(r.files.front(), std::string("libcore-win-x64.dll"));
    }
    CHECK_EQ(plan.install_dir_name(), std::string("app-2.0.0"));
}

void the_strictest_floor_is_reported() {
    TempDir t;
    mago::test::write_toy(t);
    const Plan plan = resolve(Registry::scan(t.path()), "app");
    // app asks core >=1.0.0; view asks >=1.1.0. The real floor is 1.1.0, and
    // nothing in app's own manifest says so.
    bool found = false;
    for (const auto& n : plan.notes) {
        if (n.find(">=1.1.0") != std::string::npos) found = true;
    }
    CHECK(found);
}

void a_runtime_is_not_installable_by_name() {
    TempDir t;
    mago::test::write_toy(t);
    const Registry reg = Registry::scan(t.path());
    CHECK_THROWS(resolve(reg, "core"), ResolveError, "not an application");
}

void an_unsatisfiable_floor_stops_the_plan() {
    TempDir t;
    mago::test::write_toy(t);
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.0.5")");  // below view's >=1.1.0
    const Registry reg = Registry::scan(t.path());
    CHECK_THROWS(resolve(reg, "app"), ResolveError, "installed honestly");
}

void a_platform_the_project_does_not_build_for_is_refused() {
    TempDir t;
    mago::test::write_toy(t);
    const Registry reg = Registry::scan(t.path());
    CHECK_THROWS(resolve(reg, "app", "macos-universal"), ResolveError, "does not declare");
}

void an_unknown_application_names_the_ones_that_exist() {
    TempDir t;
    mago::test::write_toy(t);
    const Registry reg = Registry::scan(t.path());
    CHECK_THROWS(resolve(reg, "ghost"), ResolveError, "app");
}

// One consumer links it statically, another takes the shared library. The file
// has to be there, so the shared reading wins.
void a_shared_edge_promotes_an_absorbed_dependency() {
    TempDir t;
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.0.0","provides":{"artifacts":["libcore-win-x64.dll"]})");
    t.project("mid", R"("name":"mid","display_name":"Mid","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"core","version":"*","link":"static"}])");
    t.project("app", R"("name":"app","display_name":"App","kind":"application",
        "version":"1.0.0","platforms":["windows-x64"],
        "requires":[
          {"name":"mid","version":"*","link":"static"},
          {"name":"core","version":"*","link":"shared"}],
        "provides":{"gui":"app"})");
    const Plan plan = resolve(Registry::scan(t.path()), "app", "windows-x64");
    CHECK(names(plan.payload).count("core") == 1);
    CHECK(names(plan.absorbed).count("core") == 0);
}

void a_cycle_is_refused_rather_than_looped() {
    TempDir t;
    t.project("a", R"("name":"a","display_name":"A","kind":"application","version":"1.0.0",
        "requires":[{"name":"b","version":"*","link":"static"}])");
    t.project("b", R"("name":"b","display_name":"B","kind":"runtime","version":"1.0.0",
        "requires":[{"name":"a","version":"*","link":"static"}])");
    const Registry reg = Registry::scan(t.path());
    CHECK_THROWS(resolve(reg, "a"), ResolveError, "cycle");
}

void the_plan_serialises_to_json() {
    TempDir t;
    mago::test::write_toy(t);
    const std::string json = resolve(Registry::scan(t.path()), "app", "windows-x64").to_json();
    CHECK(json.find("\"plan\": \"mago/0.1\"") != std::string::npos);
    CHECK(json.find("\"install_dir\": \"app-2.0.0\"") != std::string::npos);
    CHECK(json.find("libcore-win-x64.dll") != std::string::npos);
    // Balanced, which a hand-written writer is exactly where you find out.
    CHECK_EQ(std::count(json.begin(), json.end(), '{'),
             std::count(json.begin(), json.end(), '}'));
    CHECK_EQ(std::count(json.begin(), json.end(), '['),
             std::count(json.begin(), json.end(), ']'));
}

}  // namespace

int main() {
    static_dependencies_are_absorbed_not_shipped();
    build_order_puts_dependencies_first();
    the_platform_picks_the_artifact();
    the_strictest_floor_is_reported();
    a_runtime_is_not_installable_by_name();
    an_unsatisfiable_floor_stops_the_plan();
    a_platform_the_project_does_not_build_for_is_refused();
    an_unknown_application_names_the_ones_that_exist();
    a_shared_edge_promotes_an_absorbed_dependency();
    a_cycle_is_refused_rather_than_looped();
    the_plan_serialises_to_json();
    mago::test::pass("resolve");
    return 0;
}
