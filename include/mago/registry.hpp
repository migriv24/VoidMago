// The family, as one graph.
//
// Void Mago's first and most valuable job. A registry is built by walking a
// folder of sibling checkouts and reading every `void.json` it finds; nothing
// is inferred from directory names, and a project without a manifest is
// REPORTED rather than guessed at.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mago/family.hpp"
#include "mago/manifest.hpp"

namespace mago {

// Something `mago doctor` noticed. Not necessarily an error.
struct Finding {
    std::string level;  // "error" | "warn" | "note"
    std::string project;
    std::string message;
};

class Registry {
public:
    // Read every manifest one directory deep under `root`. Directories whose
    // name suggests a Void project but which carry no manifest are recorded
    // as unmanifested: the convention is only worth anything if its gaps are
    // visible.
    static Registry scan(const std::filesystem::path& root);

    // Several roots at once, for a machine whose checkouts are in more than
    // one folder. Merged into ONE registry rather than kept apart: the family
    // is one graph however many directories it is spread across, and two
    // folders holding the same project name is a `declared twice` error, which
    // is exactly what it would be inside one folder.
    static Registry scan(const std::vector<std::filesystem::path>& roots);

    const std::map<std::string, Manifest>& projects() const { return projects_; }
    const std::vector<std::filesystem::path>& unmanifested() const { return unmanifested_; }
    const std::vector<std::filesystem::path>& roots() const { return roots_; }

    bool contains(const std::string& name) const { return projects_.count(name) > 0; }
    const Manifest* get(const std::string& name) const;
    const Manifest& at(const std::string& name) const;

    std::vector<const Manifest*> applications() const;

    // Projects that `requires` this one -- the real downstream edges.
    std::vector<const Manifest*> dependents_of(const std::string& name) const;

    // Projects that meet this one at a seam: data crosses, nothing links.
    //
    // Symmetric on purpose. A seam is a contract with two ends, and either end
    // moving is a reason to re-run it -- so it does not matter which of the two
    // repositories happened to write the edge down.
    //
    // Two kinds of edge are filtered out here: a WILDCARD edge (`name: "*"`),
    // which says "I read every project's X" and is a standing habit rather
    // than a contract with anybody in particular; and any pair that also has a
    // `requires` edge in either direction, because that is one edge described
    // twice and counting it as both would report a rebuild AND a retest for
    // the same relationship.
    //
    // The manifest seam is filtered by a third rule, which is really the
    // general one: A SEAM BELONGS TO WHOEVER DEFINES THE DOCUMENT THAT CROSSES
    // IT. `void.json` is Void Mago's format, so an edge at that seam is a
    // retest when MAGO changes -- FaultSack parses that schema -- and nothing
    // at all when anybody else does. Without that rule, either every project
    // that names the manifest seam becomes fragile to its neighbours, or
    // Mago's own consumers never hear that their input format moved.
    std::vector<std::pair<const Manifest*, std::string>> seam_neighbours(
        const std::string& name) const;

    // Manifests that are present and could not be read.
    //
    // Separate from `check()` because these are the findings that come with a
    // HOLE: the project is not in `projects()` at all, so every answer below
    // is computed as though it did not exist. A command that prints its list
    // of projects without mentioning these is printing a shorter family than
    // the one on disk.
    const std::vector<Finding>& load_errors() const { return load_errors_; }

    // Everything Mago can tell is wrong without building anything.
    //
    // `family` is optional and changes no verdict: where a person has already
    // adjudicated a disagreement between two manifests, the finding carries
    // their answer instead of only the argument. The manifests are still
    // wrong, and still an error -- but now with the fix attached.
    std::vector<Finding> check(const Family* family = nullptr) const;

private:
    bool linked(const std::string& a, const std::string& b) const;
    void check_reciprocity(const Manifest& m, const Family* family,
                           std::vector<Finding>& out) const;
    void check_versions(const Manifest& m, std::vector<Finding>& out) const;
    void check_vendors(const Manifest& m, std::vector<Finding>& out) const;
    void check_release(const Manifest& m, std::vector<Finding>& out) const;
    void check_cycles(std::vector<Finding>& out) const;

    std::vector<std::filesystem::path> roots_;
    std::map<std::string, Manifest> projects_;
    std::vector<std::filesystem::path> unmanifested_;
    std::vector<Finding> load_errors_;
};

}  // namespace mago
