// Reading and checking `void.json`.
//
// The manifest is the whole input to Void Mago. Everything else in this
// library is arithmetic over what these files say.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mago/semver.hpp"

namespace mago {

inline constexpr const char* kManifestName = "void.json";
inline constexpr const char* kManifestFormat = "void/0.1";

// A manifest that cannot be trusted to mean what it appears to mean.
class ManifestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A `requires[]` entry: a real dependency edge Mago resolves.
struct Requirement {
    std::string name;
    std::string kind;
    Range version;
    std::string sibling;    // relative path to the checkout during development
    std::string cmake_var;  // the variable that points a build at that checkout
    std::string link;       // shared | static | source | vendored | ""
    std::string via;        // reached through another project's headers
    std::string note;

    // Does this dependency become a FILE ON DISK next to the binary?
    //
    // Only shared links do. A statically linked dependency is already inside
    // the consumer's binary -- it governs rebuilds, never install steps, and
    // listing it as something to install would be a lie about the file set.
    bool is_installed_artifact() const { return link == "shared"; }

    // Is the dependency's code INSIDE the consumer's build output? This is the
    // question that decides `rebuild`, and it is not the same as "the consumer
    // has the dependency's source on disk".
    bool is_compiled_in() const {
        return link == "static" || link == "source" || link == "vendored";
    }

    // Imported at runtime from source, the way a Python package imports
    // another. Nothing is compiled, so nothing rebuilds and no file of the
    // dependency's ships -- but the API it imports is a contract, and a
    // consumer bound this way has to re-run against a new upstream.
    bool is_imported() const { return link == "import"; }
};

// A `vendors[]` entry: code the project uses and does not own.
//
// `requires` is a Void project. This is everything else -- cJSON, SQLite,
// GLFW, Dear ImGui, libsodium, and the toolchain runtime that comes along
// whether anybody asked for it or not. The family's rule has always been
// "vendor rather than resolve", and that rule was doing two things at once:
// deciding how third-party code is ACQUIRED (copied in, pinned, reviewed) and
// quietly deciding it need not be WRITTEN DOWN. Only the first is a good idea.
//
// Three things go wrong when it is not written down, and all three are already
// true of this family:
//
//   VERSION DRIFT. cJSON is vendored in six repositories. They are all 1.7.18
//   today, by discipline, and nothing checks it. Two of those copies end up
//   inside one binary.
//
//   LICENCES. Five repositories keep a hand-maintained THIRD-PARTY-NOTICES.md
//   and Void Mago keeps none at all, which is a compliance gap in the tool
//   whose job is finding gaps.
//
//   FILES THAT MUST SHIP. `libwinpthread-1.dll` is a toolchain runtime that no
//   manifest mentioned, and Void Hormiga would have shipped without it.
//
// NOT CALLED A HOLIDAY, deliberately. Void Core's holiday is "the protocol
// interface to an external system the application does not own" -- a runtime
// I/O adapter, where all real I/O lives. A vendored library is source code
// compiled in, with no interface and no I/O of its own. The modelling instinct
// is the same (name the external thing explicitly, do not let it be implicit)
// and the referents are different, so reusing the word would have been the
// collision Void Core avoided when it declined to call its rune kinds
// gamma/delta/epsilon because Void Maiz already used those letters.
struct Vendor {
    std::string name;      // as its authors write it: "cJSON", "Dear ImGui"
    std::string version;   // free text: third-party versions are not all semver
    std::string license;   // SPDX where there is one
    std::string upstream;  // where it came from
    std::string path;      // relative to this repository, when it is vendored
    std::string note;

    // How it reaches the consumer's binary. `source`, `static`, `header-only`
    // and `vendored` are compiled in and ship no file of their own. `shared`
    // ships a file. `system` is provided by the operating system and must NOT
    // ship. `toolchain` comes from the compiler and is the dangerous one: it
    // is a file on the developer's machine and on no other, so it must be
    // linked statically or shipped deliberately.
    std::string link;

    // A file in this repository carrying the upstream's own version literal,
    // for `mago doctor` to check the declared version against.
    std::string version_source;

    bool ships_a_file() const { return link == "shared"; }
    bool is_compiled_in() const {
        return link == "source" || link == "static" || link == "vendored" ||
               link == "header-only";
    }
};

// An `interacts_with[]` entry: exchange without linkage.
//
// Kept apart from `requires` on purpose. Void Reyna emits a transcript that
// Void Hormiga replays and neither links the other; recording that as a
// dependency would invent an edge that does not exist.
struct Interaction {
    std::string name;  // "*" means every Void project
    std::string kind;
    std::string seam;
    std::string direction;  // inbound | outbound | both
    std::string note;
};

// One entry of `release.behavior_changes`: something that moved WITHOUT the
// signature moving.
//
// The category a dependency resolver structurally cannot see, and the one that
// bites. No version constraint is violated and no symbol changed, so every
// verdict Mago computes stays correct and a consumer still breaks. Named by
// Void Core after shipping 0.2.14 by hand:
//
//     `link --weight` now refuses a non-number. No signature changed, no
//     version constraint is violated, and any host that depended on the old
//     silent 0.0 is affected.
struct BehaviorChange {
    std::string what;
    std::string who_is_affected;
};

// What THIS version of the project did, declared by the person who did it.
//
// Everything else in a manifest is a standing fact. This block is the only
// part that describes an event, and it exists because of one sentence in Void
// Core's 0.2.14 message: every verdict in a broadcast is derivable from the
// manifests plus one fact a human supplied -- "the change is additive within
// the 0.2.x line" -- and *that one bit is a field a release could declare*.
//
// With it, `mago blast <project>` needs no flags and `mago notify <project>`
// can write the whole broadcast. Without it, both still work; they just ask
// for `--to` and `--change` the way they always did.
struct Release {
    bool declared = false;
    Version version{};                    // must equal the manifest's own
    std::optional<Version> previous;      // what consumers are moving FROM
    std::string change;                   // compatible | breaking | "" (infer)
    std::string date;                     // free text, as written
    std::string summary;                  // one line, for the broadcast header
    std::vector<std::string> adds;        // new OPTIONAL surface nobody must adopt
    std::vector<BehaviorChange> behavior_changes;
    std::vector<std::string> read_more;   // where the argument is written down
};

struct Manifest {
    std::string name;
    std::string display_name;
    std::string kind;  // runtime | capability | application
    Version version;
    std::filesystem::path path;  // the directory holding the manifest

    std::string license;
    std::string repository;
    std::string description;
    std::string status;

    // `requires` is a C++20 keyword, so the field cannot carry the manifest's
    // own name here. Everything user-facing still says "requires".
    std::vector<Requirement> requirements;
    std::vector<Interaction> interacts_with;
    std::vector<Vendor> vendors;

    // From `provides`.
    std::string cli;
    std::string gui;
    std::vector<std::string> artifacts;            // prebuilt per-platform files
    std::vector<std::string> ships_beside_binary;  // folders/files next to the exe

    std::vector<std::string> platforms;

    // From `provides.effects`, `provides.contracts` and `provides.documents`.
    std::vector<std::string> effects;
    std::map<std::string, std::string> contracts;
    std::map<std::string, std::string> documents;

    Release release;

    // Which files in this repository state the same version, for `mago doctor`
    // to check the manifest against. Absent means "the well-known ones";
    // present -- including present and empty -- is the project's own answer,
    // for a repository that versions a subcomponent separately and would
    // otherwise be told off for it every run.
    bool version_sources_declared = false;
    std::vector<std::string> version_sources;

    // The names a person or a script actually invokes, gui first.
    std::vector<std::string> executables() const;

    // Does a person ever install THIS, by name? Runtimes and capabilities ship
    // inside an application's release; they are never a row in anybody's list
    // of installed software.
    bool is_installable() const { return kind == "application"; }

    const Requirement* requirement(const std::string& dep) const;

    // Is `document` a format THIS project defines? A seam is named after the
    // thing that crosses it, and the project that publishes that thing is the
    // only one whose change can move the seam. Void Mago defines `void.json`,
    // so an edge at that seam is a retest when Mago changes and nothing at all
    // when anybody else does -- which is why every other project can name the
    // manifest seam without becoming fragile to its neighbours.
    bool provides_document(const std::string& document) const;
};

// Read one `void.json`. `path` may be the file or the directory holding it.
// Throws ManifestError on anything ambiguous.
Manifest load(const std::filesystem::path& path);

}  // namespace mago
