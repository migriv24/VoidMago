// Staging: from a resolved plan to a folder of real files.
//
// THE MISSING STEP. `mago plan` says which files belong in the package and
// `mago wizard` writes an installer script that references them -- at paths
// under a `--source-root` that nothing had ever produced. The script compiled
// against a directory that did not exist, which is a working answer to the
// hard question (what belongs in the package) and no answer at all to the easy
// one (where are those files).
//
// Staging is the easy one, done. It walks each project's checkout, finds the
// file the manifest promised, and copies it into a layout the emitter already
// expects:
//
//     <out>/voidcore/libvoidcore-win-x64.dll
//     <out>/voidhormiga/voidhormiga.exe
//     <out>/voidhormiga/okf/...
//
// Two properties are the whole point:
//
//   IT REPORTS WHAT IT COULD NOT FIND, and fails. A staging tree with a hole
//   compiles into an installer with a hole, which is discovered by a person on
//   another machine when the application does not start. Void Hormiga's
//   manifest promises a `fonts/` folder that is not in its repository; that is
//   exactly the class of thing that should be caught here and not there.
//
//   IT SAYS WHERE EVERY FILE CAME FROM. A build tree can hold several copies
//   of a name, and "which one did it pick" must never be a guess.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mago/resolve.hpp"

namespace mago {

// One file the plan asked for, and what became of it.
struct StagedFile {
    std::string project;
    std::string declared;              // as the manifest wrote it
    std::filesystem::path source;      // where it was found; empty if it was not
    std::filesystem::path destination;
    bool is_folder = false;
    std::uintmax_t bytes = 0;
    std::vector<std::filesystem::path> also_found;  // other candidates, not chosen
    std::string problem;               // empty when it staged cleanly
};

// A DLL a staged executable imports that the payload does not contain and
// Windows does not provide. The single most common way an installer that
// compiles cleanly produces an application that will not start.
struct MissingImport {
    std::string binary;   // the staged file that needs it
    std::string dll;      // what it asks the loader for
};

struct StageReport {
    std::filesystem::path root;        // the staging directory
    std::string application;
    Version version;
    std::string platform;
    std::vector<StagedFile> files;
    std::vector<MissingImport> unresolved;

    std::size_t missing() const;
    bool complete() const { return missing() == 0 && unresolved.empty(); }
    std::uintmax_t bytes() const;

    std::string to_json(int indent = 2) const;
};

struct StageOptions {
    std::filesystem::path out;   // the staging directory; required
    bool dry_run = false;        // find everything, copy nothing
    bool clean = true;           // empty the destination first
};

// Find and copy every file `plan` calls for. Never throws for a missing file:
// a missing file is a finding, and the report is the answer.
StageReport stage(const Registry& reg, const Plan& plan, const StageOptions& opt);

// Where a project's built files might be, deepest-last. Exposed because
// "where did it look" is the first question when something is not found.
std::vector<std::filesystem::path> search_roots(const Manifest& m);

}  // namespace mago
