// `mago bootstrap`: the family onto a disk that has nothing.
//
// §8.1 of the index, settled for its first case: `mago` resolves against
// whatever is on disk, which "works for one developer and does not survive a
// second one." A contributor to Void Hormiga needs Void Core, Void Maiz, Void
// Allomone and Void Palabra beside it as siblings, at versions that work
// together, and until now every repository that needed that spelled out its own
// `git clone` lines -- and CI cloned `main`, which no matrix ever approved.
//
// The walk: read the target's `requires`, find each dependency's repository
// (the edge carries it: the dependency's own manifest is inside the repository
// nobody has cloned yet), pick a release tag every requirer accepts, clone it
// at the sibling path the requirer names, then read ITS manifest and continue.
//
// THE VERSION RULE. For one project, every requirer contributes a range; the
// admissible tags are the ones every range allows, and the chosen tag is the
// greatest of them. With the forms the family writes (floors, carets, tildes,
// `*`) each range is an interval on the version order, so the admissible set is
// their intersection, an interval too, and "the greatest admissible tag" is
// well defined whenever it is non-empty. A requirer discovered AFTER a project
// was cloned can only narrow the interval; if the version already on disk falls
// outside it, that is reported as a conflict and nothing is moved.
//
// What it never does: overwrite, check out or delete anything that is already
// on disk. A folder at a sibling path is the person's, whatever is in it.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mago/semver.hpp"

namespace mago {

// The two things bootstrap asks of git. An interface so the planner can be
// tested with a fake family and no network.
class Git {
public:
    virtual ~Git() = default;
    // Release tags on a remote, as written ("v0.2.14"). Empty, with `error`
    // set, when the remote cannot be read.
    virtual std::vector<std::string> tags(const std::string& url, std::string& error) = 0;
    // Clone `url` into `dir` at `ref` ("" = the default branch).
    virtual bool clone(const std::string& url, const std::string& ref,
                       const std::filesystem::path& dir, bool shallow,
                       std::string& error) = 0;
};

// The `git` on PATH.
std::unique_ptr<Git> system_git();

struct BootstrapOptions {
    bool dry_run = false;     // decide everything, clone nothing
    bool shallow_deps = true; // dependencies at depth 1; the target is never shallow
};

struct BootstrapStep {
    enum class Action {
        Clone,    // cloned (or would be, in a dry run)
        Present,  // already on disk; left exactly as it was
        Absent,   // an optional dependency with no repository or no remote
        Failed,   // a required dependency that could not be laid out
    };
    std::string name;
    std::string repository;
    std::string ref;    // the tag chosen; "" = the default branch
    std::string range;  // every requirer's range, joined
    std::filesystem::path dir;
    Action action = Action::Failed;
    bool optional = false;
    std::vector<std::string> notes;  // why this version, and anything wrong
};

struct BootstrapResult {
    std::vector<BootstrapStep> steps;  // in the order they were laid out
    bool ok = true;                    // false if any required step failed or conflicted
};

// Lay out everything `target` (a checkout holding a void.json) requires,
// transitively, beside it.
BootstrapResult bootstrap(const std::filesystem::path& target, Git& git,
                          const BootstrapOptions& options);

// The greatest tag every range allows. Tags that are not versions (`latest`,
// `nightly`) are ignored. nullopt when none is admissible.
std::optional<std::string> choose_tag(const std::vector<std::string>& tags,
                                      const std::vector<Range>& ranges);

// "v0.2.14" -> 0.2.14; "refs/tags/v1.0.0^{}" -> 1.0.0; nullopt otherwise.
std::optional<Version> tag_version(std::string tag);

// Run a program and collect what it printed (stdout and stderr). Each argument
// is quoted; one containing a quote is refused (exit -1). For the few commands
// that reach outside Mago: git, and FaultSack for `mago learn`.
int run_program(const std::vector<std::string>& argv, std::string& out);

}  // namespace mago
