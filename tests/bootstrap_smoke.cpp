// bootstrap_smoke -- `mago bootstrap` over a fake family, with no network.
//
// A FakeGit serves tags per URL and "clones" by writing the manifest that
// release of the project would carry, so the walk, the version rule and the
// never-touch-what-is-there rule are all exercised exactly as they run.
#include "mago/bootstrap.hpp"

#include <map>

#include "toy_family.hpp"

using namespace mago;
using mago::test::pass;
using mago::test::TempDir;

namespace fs = std::filesystem;

namespace {

struct Release {
    std::string body;  // the manifest's contents at that tag, without braces
};

class FakeGit final : public Git {
public:
    // url -> tag -> manifest body
    std::map<std::string, std::map<std::string, Release>> remotes;
    std::vector<std::string> cloned;  // "url@ref"

    std::vector<std::string> tags(const std::string& url, std::string& error) override {
        const auto it = remotes.find(url);
        if (it == remotes.end()) { error = "repository not found"; return {}; }
        std::vector<std::string> out;
        for (const auto& [tag, _] : it->second) out.push_back(tag);
        return out;
    }
    bool clone(const std::string& url, const std::string& ref, const fs::path& dir, bool,
               std::string& error) override {
        const auto it = remotes.find(url);
        if (it == remotes.end()) { error = "repository not found"; return false; }
        const auto rel = it->second.find(ref.empty() ? "main" : ref);
        if (rel == it->second.end()) { error = "no such ref " + ref; return false; }
        fs::create_directories(dir);
        std::ofstream(dir / "void.json", std::ios::binary)
            << "{\"manifest\":\"void/0.1\"," << rel->second.body << "}";
        cloned.push_back(url + "@" + ref);
        return true;
    }
};

std::string core_at(const char* v) {
    return std::string(R"("name":"core","kind":"runtime","version":")") + v + "\"";
}

Range R(const char* s) { return *Range::parse(s); }

void the_greatest_admissible_tag() {
    const std::vector<std::string> tags = {"v0.2.7", "v0.2.14", "v0.3.0", "nightly", "0.2.9"};
    CHECK_EQ(*choose_tag(tags, {R(">=0.2.7")}), std::string("v0.3.0"));
    CHECK_EQ(*choose_tag(tags, {R(">=0.2.7"), R("^0.2.9")}), std::string("v0.2.14"));
    CHECK(!choose_tag(tags, {R(">=0.4.0")}));
    CHECK(!choose_tag({"latest"}, {R("*")}));  // not a version: ignored
    CHECK_EQ(tag_version("refs/tags/v1.0.0^{}")->str(), std::string("1.0.0"));
    pass("the greatest tag every range allows");
}

// app -> view -> core, and app -> core directly with a stricter floor.
void the_family_is_laid_out_beside_the_target() {
    TempDir t;
    t.project("app", R"("name":"app","kind":"application","version":"1.0.0",
        "requires":[
          {"name":"view","version":"*","sibling":"../view","repository":"fake://view"},
          {"name":"core","version":">=0.2.12","sibling":"../core","repository":"fake://core"},
          {"name":"net","version":"*","sibling":"../net","optional":true}])");
    FakeGit git;
    git.remotes["fake://view"]["v0.4.0"] = {R"("name":"view","kind":"runtime","version":"0.4.0",
        "requires":[{"name":"core","version":">=0.2.7","sibling":"../core","repository":"fake://core"}])"};
    for (const char* v : {"0.2.7", "0.2.12", "0.2.14"})
        git.remotes["fake://core"][std::string("v") + v] = {core_at(v)};

    const auto r = bootstrap(t.path() / "app", git, {});
    CHECK(r.ok);
    CHECK_EQ(r.steps.size(), size_t(3));
    CHECK_EQ(r.steps[0].name, std::string("view"));
    CHECK(r.steps[0].action == BootstrapStep::Action::Clone);
    CHECK(fs::exists(t.path() / "view" / "void.json"));
    // core: the newest release both app (>=0.2.12) and view (>=0.2.7) accept
    CHECK_EQ(r.steps[1].name, std::string("core"));
    CHECK_EQ(r.steps[1].ref, std::string("v0.2.14"));
    CHECK(fs::exists(t.path() / "core" / "void.json"));
    // an optional dependency with no repository is left out, not a failure
    CHECK_EQ(r.steps[2].name, std::string("net"));
    CHECK(r.steps[2].action == BootstrapStep::Action::Absent);
    // core is cloned once, though two projects require it
    CHECK_EQ(git.cloned.size(), size_t(2));
    pass("the family is laid out beside the target, each project once");
}

void what_is_on_disk_is_never_touched() {
    TempDir t;
    t.project("app", R"("name":"app","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":">=0.2.12","sibling":"../core","repository":"fake://core"}])");
    t.project("core", core_at("0.2.7"));  // somebody's older checkout
    FakeGit git;
    git.remotes["fake://core"]["v0.2.14"] = {core_at("0.2.14")};

    const auto r = bootstrap(t.path() / "app", git, {});
    CHECK(!r.ok);  // too old for the app, and said so
    CHECK(r.steps[0].action == BootstrapStep::Action::Present);
    CHECK(git.cloned.empty());
    std::ifstream in(t.path() / "core" / "void.json");
    const std::string body((std::istreambuf_iterator<char>(in)), {});
    CHECK(body.find("0.2.7") != std::string::npos);  // still the person's version
    pass("a checkout already on disk is read and judged, never replaced");
}

void a_hand_made_checkout_is_used_as_it_is() {
    TempDir t;
    t.project("app", R"("name":"app","kind":"application","version":"1.0.0",
        "requires":[{"name":"net","version":"*","sibling":"../net","optional":true}])");
    t.file("net", "README.md", "no manifest yet");
    FakeGit git;
    const auto r = bootstrap(t.path() / "app", git, {});
    CHECK(r.ok);
    CHECK(r.steps[0].action == BootstrapStep::Action::Present);
    pass("a checkout with no void.json is used as it is, its version unchecked");
}

void a_dry_run_clones_nothing() {
    TempDir t;
    t.project("app", R"("name":"app","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":"*","sibling":"../core","repository":"fake://core"}])");
    FakeGit git;
    git.remotes["fake://core"]["v0.2.14"] = {core_at("0.2.14")};
    BootstrapOptions o;
    o.dry_run = true;
    const auto r = bootstrap(t.path() / "app", git, o);
    CHECK(r.ok);
    CHECK_EQ(r.steps[0].ref, std::string("v0.2.14"));
    CHECK(git.cloned.empty());
    CHECK(!fs::exists(t.path() / "core"));
    pass("a dry run decides every version and clones nothing");
}

void a_required_project_with_no_address_fails() {
    TempDir t;
    t.project("app", R"("name":"app","kind":"application","version":"1.0.0",
        "requires":[{"name":"core","version":"*","sibling":"../core"}])");
    FakeGit git;
    const auto r = bootstrap(t.path() / "app", git, {});
    CHECK(!r.ok);
    CHECK(r.steps[0].action == BootstrapStep::Action::Failed);
    pass("a required dependency that names no repository fails, and says which field");
}

}  // namespace

int main() {
    the_greatest_admissible_tag();
    the_family_is_laid_out_beside_the_target();
    what_is_on_disk_is_never_touched();
    a_hand_made_checkout_is_used_as_it_is();
    a_dry_run_clones_nothing();
    a_required_project_with_no_address_fails();
    return 0;
}
