#include "mago/feed.hpp"

#include <algorithm>
#include <chrono>
#include <format>

#include "mago/json_out.hpp"
#include "mago/sha256.hpp"

namespace fs = std::filesystem;

namespace mago {
namespace {

std::string now_iso8601() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}+00:00", now);
}

// "Void Hormiga" -> "VoidHormiga". The display name without its spaces, so a
// filename a person reads matches the name a person knows.
std::string compact(const std::string& display) {
    std::string out;
    for (char c : display) {
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    }
    return out;
}

// The two names differ in exactly one segment, so they are one rule.
//
// Windows gets an installer, because `wizard` emits NSIS. Every other platform
// gets an ARCHIVE -- `<base>-<version>-<platform>.tar.gz` -- because that is what
// exists: nothing here emits a .dmg or an AppImage, and the application's own
// release runner packs a tarball. This used to answer `...-linux-x64-setup`, a
// file nobody ever built, so `feed --artifacts` found nothing to hash and wrote
// `"sha256": null`, and the Linux client correctly refused every update
// (Void Hormiga 0.1.3, reported 2026-09-18). The name has to be the file.
std::string asset_name(const Manifest& m, const std::string& platform,
                       const std::string& version) {
    const std::string base = compact(m.display_name.empty() ? m.name : m.display_name);
    const std::string stem = base + (version.empty() ? "" : "-" + version) + "-" + platform;
    if (platform.rfind("windows", 0) == 0) return stem + "-setup.exe";
    return stem + ".tar.gz";
}

}  // namespace

std::string installer_name(const Manifest& m, const std::string& platform) {
    return asset_name(m, platform, m.version.str());
}

std::string stable_installer_name(const Manifest& m, const std::string& platform) {
    return asset_name(m, platform, "");
}

std::vector<FeedEntry> build_feed(const Registry& reg, const std::vector<std::string>& apps,
                                  const FeedOptions& opt) {
    std::vector<FeedEntry> entries;

    for (const auto& name : apps) {
        const Manifest* m = reg.get(name);
        if (!m) continue;

        FeedEntry entry;
        entry.name = m->name;
        entry.display_name = m->display_name;
        entry.description = m->description;
        entry.kind = m->kind;
        entry.latest = m->version;

        FeedRelease release;
        release.version = m->version;
        if (m->release.declared) {
            release.date = m->release.date;
            release.change = m->release.change;
            release.summary = m->release.summary;
            release.behavior_changes = m->release.behavior_changes;
            release.adds = m->release.adds;
        }

        for (const auto& platform : m->platforms) {
            FeedArtifact artifact;
            artifact.platform = platform;
            artifact.file = installer_name(*m, platform);
            artifact.stable_file = stable_installer_name(*m, platform);
            if (!opt.base_url.empty()) {
                artifact.url = opt.base_url;
                if (artifact.url.back() != '/') artifact.url += '/';
                artifact.url += artifact.file;
            }

            // Hash whatever is actually on disk. A feed that claims a hash it
            // did not compute is worse than one that admits it has none: the
            // first is trusted, and the second is fixed.
            if (!opt.artifacts_dir.empty()) {
                const fs::path built = opt.artifacts_dir / artifact.file;
                std::error_code ec;
                if (fs::is_regular_file(built, ec)) {
                    artifact.bytes = fs::file_size(built, ec);
                    if (const auto hash = sha256_file(built)) artifact.sha256 = *hash;
                }

                // And the version-free copy, if somebody made one. Hashed
                // rather than merely looked for: a stale copy left over from
                // the previous release is present, plausible, and hands every
                // new visitor the old application.
                const fs::path stable = opt.artifacts_dir / artifact.stable_file;
                artifact.stable_present = fs::is_regular_file(stable, ec);
                if (artifact.stable_present) {
                    if (const auto hash = sha256_file(stable)) artifact.stable_sha256 = *hash;
                }
            }
            release.artifacts.push_back(std::move(artifact));
        }

        entry.releases.push_back(std::move(release));
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const FeedEntry& a, const FeedEntry& b) { return a.name < b.name; });
    return entries;
}

std::string feed_json(const std::vector<FeedEntry>& entries, const FeedOptions& opt, int indent) {
    JsonWriter w(indent);
    w.begin_object();
    w.field("feed", kFeedFormat);
    w.field("generated", now_iso8601());
    w.field("publisher", opt.publisher);
    w.field("note",
            "What exists, for a client that already has one of these installed. Read it, "
            "compare `latest` with your own version, and ASK -- no Void application updates "
            "itself without being told to. `behavior_changes` is what makes the asking worth "
            "anything: it is the part a version number cannot carry.");

    w.key("applications").begin_object();
    for (const auto& entry : entries) {
        w.key(entry.name).begin_object();
        w.field("display_name", entry.display_name);
        w.field("description", entry.description);
        w.field("kind", entry.kind);
        w.key("latest").value(entry.latest.str());

        w.key("releases").begin_array();
        for (const auto& release : entry.releases) {
            w.begin_object();
            w.key("version").value(release.version.str());
            w.field("date", release.date);
            w.field("change", release.change);
            w.field("summary", release.summary);
            w.string_array("adds", release.adds);

            w.key("behavior_changes").begin_array();
            for (const auto& bc : release.behavior_changes) {
                w.begin_object();
                w.field("what", bc.what);
                w.field("who_is_affected", bc.who_is_affected);
                w.end_object();
            }
            w.end_array();

            w.key("artifacts").begin_object();
            for (const auto& artifact : release.artifacts) {
                w.key(artifact.platform).begin_object();
                w.field("file", artifact.file);
                // The name the download page links to, which is the same bytes
                // attached a second time. Stated so it is a fact somebody can
                // check rather than a convention living in one project's prose
                // -- no client has to read it, and the updater does not.
                w.field("stable_file", artifact.stable_file);
                w.field("url", artifact.url);
                w.key("bytes").value(static_cast<long long>(artifact.bytes));
                w.field("sha256", artifact.sha256);
                // Present and null on purpose. A client that reads this field
                // and finds it absent might conclude signing was not intended;
                // finding it null says it is intended and not yet done.
                w.field("signature", artifact.signature);
                w.end_object();
            }
            w.end_object();
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_object();
    w.end_object();
    return w.str();
}

}  // namespace mago
