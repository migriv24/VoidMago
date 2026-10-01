// Getting an application onto somebody else's machine: staging, hashing, and
// the feed that tells a client a newer version exists.

#include <fstream>
#include <string>

#include "mago/feed.hpp"
#include "mago/imports.hpp"
#include "mago/sha256.hpp"
#include "mago/stage.hpp"
#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

const StagedFile* staged_for(const StageReport& r, const std::string& declared) {
    for (const auto& f : r.files) {
        if (f.declared == declared) return &f;
    }
    return nullptr;
}

// The FIPS 180-4 examples, so a wrong implementation cannot pass by agreeing
// with itself.
void sha256_matches_the_standard() {
    CHECK(sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // The padding boundary: 55, 56 and 64 bytes exercise the three cases where
    // the length either fits in the last block, forces another, or fills one
    // exactly.
    CHECK(sha256_hex(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256_hex(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256_hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

void a_hash_of_a_file_is_the_hash_of_its_bytes() {
    TempDir t;
    const auto path = t.path() / "payload.bin";
    std::ofstream(path, std::ios::binary) << "abc";
    const auto hash = sha256_file(path);
    CHECK(hash.has_value());
    CHECK(*hash == sha256_hex("abc"));
    CHECK(!sha256_file(t.path() / "not-here").has_value());
}

// THE THREE NAMES. A release page needs the platform in the filename to keep
// three builds apart; a machine needs it gone, because the consumer's binary
// asks the loader for `libcore.dll`.
void the_platform_tag_does_not_survive_onto_the_machine() {
    CHECK(installed_name("libcore-win-x64.dll", "windows-x64") == "libcore.dll");
    CHECK(installed_name("libcore.dll", "windows-x64") == "libcore.dll");
    CHECK(installed_name("libcore-win-x64.dll", "") == "libcore-win-x64.dll");
}

void staging_finds_the_files_a_plan_asks_for() {
    TempDir t;
    mago::test::write_toy(t);
    // Core builds `libcore.dll` and its manifest ships `libcore-win-x64.dll`.
    t.file("core/build/bin", "libcore.dll", "not really a dll");
    t.file("app/build/bin", "app.exe", "not really an exe");
    t.file("app/build/bin", "app-cli.exe", "not really an exe");
    t.file("app/docs", "readme.md", "hello");
    t.file("app/vendor/fonts", "icons.woff2", "not really a font");

    const Registry reg = Registry::scan(t.path());
    const Plan plan = resolve(reg, "app", "windows-x64");

    StageOptions opt;
    opt.out = t.path() / "out";
    const StageReport report = stage(reg, plan, opt);

    const StagedFile* dll = staged_for(report, "libcore-win-x64.dll");
    CHECK(dll != nullptr);
    CHECK(dll->problem.empty());
    CHECK(dll->source.filename().string() == "libcore.dll");
    CHECK(dll->destination.filename().string() == "libcore.dll");
    CHECK(std::filesystem::exists(opt.out / "core" / "libcore.dll"));

    // The application's own executables keep the extension the build gave
    // them: a file called `app` is not something Windows will run.
    CHECK(std::filesystem::exists(opt.out / "app" / "app.exe"));
    CHECK(std::filesystem::exists(opt.out / "app" / "docs" / "readme.md"));
    // A nested declaration keeps its shape: `fs::path` forgives the forward
    // slash here, which is exactly why the emitter's failure to was invisible
    // to anything that only inspected this tree.
    CHECK(std::filesystem::exists(opt.out / "app" / "vendor" / "fonts" / "icons.woff2"));
    CHECK(report.complete());
}

// A manifest that promises a file nobody built is the failure this catches --
// and Void Hormiga's really does promise a `fonts/` that is not there.
void a_promised_file_that_is_not_there_fails_the_stage() {
    TempDir t;
    mago::test::write_toy(t);
    t.file("core/build/bin", "libcore.dll", "x");
    t.file("app/build/bin", "app.exe", "x");
    t.file("app/build/bin", "app-cli.exe", "x");
    // `docs/` and `vendor/fonts/` are declared in the toy family's manifest
    // and deliberately absent.

    const Registry reg = Registry::scan(t.path());
    StageOptions opt;
    opt.out = t.path() / "out";
    const StageReport report = stage(reg, resolve(reg, "app", "windows-x64"), opt);

    CHECK(!report.complete());
    CHECK(report.missing() == 2);
    const StagedFile* docs = staged_for(report, "docs/");
    CHECK(docs != nullptr);
    CHECK(contains(docs->problem, "docs/"));
    const StagedFile* fonts = staged_for(report, "vendor/fonts/");
    CHECK(fonts != nullptr);
    CHECK(contains(fonts->problem, "vendor/fonts/"));
}

void a_dry_run_finds_everything_and_copies_nothing() {
    TempDir t;
    mago::test::write_toy(t);
    t.file("core/build/bin", "libcore.dll", "x");
    t.file("app/build/bin", "app.exe", "x");
    t.file("app/build/bin", "app-cli.exe", "x");
    t.file("app/docs", "readme.md", "x");
    t.file("app/vendor/fonts", "icons.woff2", "x");

    const Registry reg = Registry::scan(t.path());
    StageOptions opt;
    opt.out = t.path() / "out";
    opt.dry_run = true;
    const StageReport report = stage(reg, resolve(reg, "app", "windows-x64"), opt);
    CHECK(report.complete());
    CHECK(!std::filesystem::exists(opt.out));
}

// Windows provides these; shipping them would be wrong. Everything else a
// binary imports has to be in the package.
void system_libraries_are_known_from_the_rest() {
    CHECK(is_system_library("KERNEL32.dll"));
    CHECK(is_system_library("kernel32.dll"));
    CHECK(is_system_library("OPENGL32.dll"));
    CHECK(is_system_library("api-ms-win-crt-runtime-l1-1-0.dll"));
    CHECK(!is_system_library("libwinpthread-1.dll"));
    CHECK(!is_system_library("libvoidcore.dll"));
}

// Not a PE file is not an error: an ELF binary simply has nothing to say here.
void a_file_that_is_not_a_pe_image_reports_no_imports() {
    TempDir t;
    const auto path = t.path() / "text.txt";
    std::ofstream(path, std::ios::binary) << "this is not an executable";
    CHECK(pe_imports(path).empty());
    CHECK(pe_imports(t.path() / "missing.exe").empty());
}

// The feed is what a client reads to learn a newer version exists -- and the
// reason a "Void Hub" application is unnecessary: the hub is this file.
void the_feed_carries_what_a_version_number_cannot() {
    TempDir t;
    t.project("app", R"("name":"app","display_name":"The App","kind":"application",
        "version":"1.1.0","platforms":["windows-x64"],
        "release":{"previous":"1.0.0","change":"compatible","date":"2026-09-04",
          "summary":"faster, and one thing moved",
          "adds":["export"],
          "behavior_changes":[{"what":"--weight refuses a non-number",
                               "who_is_affected":"anything relying on the silent zero"}]})");

    const Registry reg = Registry::scan(t.path());
    FeedOptions opt;
    opt.base_url = "https://example.invalid/releases";
    opt.publisher = "Void";

    const std::vector<FeedEntry> entries = build_feed(reg, {"app"}, opt);
    CHECK(entries.size() == 1);
    CHECK(entries.front().latest.str() == "1.1.0");
    CHECK(entries.front().releases.front().behavior_changes.size() == 1);

    const std::string json = feed_json(entries, opt);
    CHECK(contains(json, "\"feed\": \"void-updates/0.1\""));
    CHECK(contains(json, "\"latest\": \"1.1.0\""));
    // The whole argument for the feed: the prompt can say what changed.
    CHECK(contains(json, "refuses a non-number"));
    CHECK(contains(json, "anything relying on the silent zero"));
    CHECK(contains(json, "https://example.invalid/releases/TheApp-1.1.0-windows-x64-setup.exe"));
    // The name a download page links to, stated rather than conventional.
    CHECK(contains(json, "\"stable_file\": \"TheApp-windows-x64-setup.exe\""));
    // Present and null, not absent: signing is intended and not yet done.
    CHECK(contains(json, "\"signature\": null"));
}

void the_installer_is_named_by_one_rule() {
    TempDir t;
    t.project("app", R"("name":"app","display_name":"Void Hormiga","kind":"application",
        "version":"0.1.0","platforms":["windows-x64"])");
    const Registry reg = Registry::scan(t.path());
    CHECK(installer_name(reg.at("app"), "windows-x64") ==
          "VoidHormiga-0.1.0-windows-x64-setup.exe");
    // The same file under the name a website can link to forever. An updater
    // wants the version in the name; a download page needs it gone, or every
    // release means redeploying the site.
    CHECK(stable_installer_name(reg.at("app"), "windows-x64") ==
          "VoidHormiga-windows-x64-setup.exe");
    // Off Windows there is no installer, only the archive the application's
    // release runner packs. The feed must name THAT file, or `--artifacts`
    // hashes nothing and the client refuses the update for want of a sha256.
    CHECK(installer_name(reg.at("app"), "linux-x64") ==
          "VoidHormiga-0.1.0-linux-x64.tar.gz");
    CHECK(stable_installer_name(reg.at("app"), "linux-x64") ==
          "VoidHormiga-linux-x64.tar.gz");
    CHECK(installer_name(reg.at("app"), "macos-arm64") ==
          "VoidHormiga-0.1.0-macos-arm64.tar.gz");
}

// The failure this exists to make visible: the version-free copy is the one
// step a person still does by hand, and forgetting it breaks nothing an
// updater can see. So `feed` has to look at the files, not just name them.
void the_stable_copy_is_checked_against_the_versioned_one() {
    TempDir t;
    t.project("app", R"("name":"app","display_name":"Void Hormiga","kind":"application",
        "version":"0.1.0","platforms":["windows-x64"])");
    const Registry reg = Registry::scan(t.path());

    FeedOptions opt;
    opt.artifacts_dir = t.path() / "dist";
    t.file("dist", "VoidHormiga-0.1.0-windows-x64-setup.exe", "the installer");

    // Nobody made the second copy.
    const FeedArtifact& absent = build_feed(reg, {"app"}, opt).front()
                                     .releases.front().artifacts.front();
    CHECK(!absent.sha256.empty());
    CHECK(!absent.stable_present);

    // A copy left over from the previous release: present, plausible, wrong.
    t.file("dist", "VoidHormiga-windows-x64-setup.exe", "the PREVIOUS installer");
    const FeedArtifact& stale = build_feed(reg, {"app"}, opt).front()
                                    .releases.front().artifacts.front();
    CHECK(stale.stable_present);
    CHECK(stale.stable_sha256 != stale.sha256);

    // The same bytes, attached twice, which is the whole design.
    t.file("dist", "VoidHormiga-windows-x64-setup.exe", "the installer");
    const FeedArtifact& good = build_feed(reg, {"app"}, opt).front()
                                   .releases.front().artifacts.front();
    CHECK(good.stable_present);
    CHECK(good.stable_sha256 == good.sha256);
}

}  // namespace

int main() {
    sha256_matches_the_standard();
    a_hash_of_a_file_is_the_hash_of_its_bytes();
    the_platform_tag_does_not_survive_onto_the_machine();
    staging_finds_the_files_a_plan_asks_for();
    a_promised_file_that_is_not_there_fails_the_stage();
    a_dry_run_finds_everything_and_copies_nothing();
    system_libraries_are_known_from_the_rest();
    a_file_that_is_not_a_pe_image_reports_no_imports();
    the_feed_carries_what_a_version_number_cannot();
    the_installer_is_named_by_one_rule();
    the_stable_copy_is_checked_against_the_versioned_one();
    mago::test::pass("ship");
    return 0;
}
