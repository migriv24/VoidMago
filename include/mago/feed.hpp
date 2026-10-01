// The update feed: one document that says what exists.
//
// The question this answers is the author's:
//
//     i also wanna be able to update void hormiga, and have those other
//     devices receive updates somehow. maybe not automatic updating, but some
//     notification when they open the app
//
// and the one underneath it -- whether the family needs a "Void Hub" for
// people to download things from.
//
// IT DOES NOT, AND THE FEED IS WHY. A hub is an application you must install
// before the application you actually wanted, which is the same bootstrap Void
// Mago refuses for itself. Everything a hub would give a *user* is either
// unnecessary (a shared runtime cache: side-by-side installs each carry their
// own copy, and shared runtimes are what DLL hell is made of) or is really a
// document (a single place that knows which versions exist).
//
// So: **the hub is a file.** One `void-updates.json`, served as a static
// object from wherever the releases live, read by every Void application at
// startup and by nothing else. Downloads stay standalone installers.
//
// # What the feed carries that a version number cannot
//
// A release already declares `behavior_changes` -- the things that moved
// without a signature moving, the category a resolver structurally cannot see.
// Those travel in the feed, so the prompt a person sees can be
//
//     Void Hormiga 0.1.1 is available.
//     - `link --weight` now refuses a non-number: any code that relied on the
//       old silent 0.0 is affected.
//
// rather than "a new version is available". That is what "never silently"
// means in practice, and it is only possible because the release block put the
// sentence somewhere a machine could carry it.
//
// # What this module does NOT do
//
// It does not download, verify, install, or decide. It writes a document.
// The client half -- fetch, compare, ask the person, run the installer -- is
// the application's, and for Void Hormiga it is Hormiga's code. Mago's half is
// knowing what exists, which is the half that needs every manifest.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mago/manifest.hpp"
#include "mago/registry.hpp"

namespace mago {

inline constexpr const char* kFeedName = "void-updates.json";
inline constexpr const char* kFeedFormat = "void-updates/0.1";

// One downloadable file, for one application on one platform.
struct FeedArtifact {
    std::string platform;
    std::string file;      // the installer's filename
    std::string url;       // base_url + file, when a base was given
    std::uintmax_t bytes = 0;
    std::string sha256;    // empty when the file was not on disk to hash
    std::string signature; // ed25519, when signing exists. Always empty today.

    // The same installer under a name with no version in it. See
    // `stable_installer_name`. Stated, never fetched by this tool.
    std::string stable_file;
    // The hash of the copy actually attached under that name, when one was
    // found beside the versioned file. NOT written into the feed -- a feed
    // states one hash per artifact, and two copies of one release that hash
    // differently is a fact about the release process, not about the release.
    // `mago feed` reports the mismatch and the feed stays a document about
    // one file.
    std::string stable_sha256;
    bool stable_present = false;
};

// One version of one application, as a person would be told about it.
struct FeedRelease {
    Version version;
    std::string date;
    std::string change;     // compatible | breaking | ""
    std::string summary;
    std::vector<BehaviorChange> behavior_changes;
    std::vector<std::string> adds;
    std::vector<FeedArtifact> artifacts;
};

struct FeedEntry {
    std::string name;
    std::string display_name;
    std::string description;
    std::string kind;
    Version latest;
    std::vector<FeedRelease> releases;  // newest first
};

struct FeedOptions {
    std::string base_url;   // where the installers are served from
    std::string publisher;
    std::filesystem::path artifacts_dir;  // where built installers are, to hash
    std::string installer_pattern;        // see `installer_name`
};

// The conventional installer filename: `VoidHormiga-0.1.0-windows-x64-setup.exe`.
// One rule, so the feed, the emitter and whoever uploads the file all agree
// without anybody writing it down twice.
std::string installer_name(const Manifest& m, const std::string& platform);

// The same file under a name with no version in it:
// `VoidHormiga-windows-x64-setup.exe`.
//
// An UPDATER wants the versioned name -- it is asking for a specific release
// and the feed tells it which. A DOWNLOAD PAGE wants the opposite: a URL that
// is still correct after the next release, so that shipping a version never
// means redeploying the website. On a GitHub release those are the same bytes
// attached twice, and `/releases/latest/download/<stable name>` resolves
// forever.
//
// Every Void application will have a download page, which is what makes this
// family vocabulary rather than one project's convention -- and the reason it
// is worth Mago knowing the name at all. Its failure mode is the argument:
// forget the second copy and updates keep working perfectly, because the feed
// points at the versioned name, while the website's button 404s for everyone
// who is not already a user. Nothing that only knows the versioned name can
// notice. Mago knows both, so `mago feed --artifacts` can.
std::string stable_installer_name(const Manifest& m, const std::string& platform);

// Build the feed for these applications. An application with no `release`
// block still appears -- it just has one release with nothing to say about
// itself beyond its version.
std::vector<FeedEntry> build_feed(const Registry& reg, const std::vector<std::string>& apps,
                                  const FeedOptions& opt);

std::string feed_json(const std::vector<FeedEntry>& entries, const FeedOptions& opt,
                      int indent = 2);

}  // namespace mago
