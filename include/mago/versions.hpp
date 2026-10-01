// Does a repository agree with itself about what version it is?
//
// The check exists because of a specific failure. Void Core shipped 0.2.14 by
// hand and its `void.json` still said 0.2.12 -- stale by two releases, and
// nothing caught it:
//
//     Nothing caught it, because nothing reads it -- `mago doctor` is
//     precisely the thing that would have. Void Core has a `version_test.py`
//     that pins five version strings against each other and `void.json` was
//     NOT one of them; it is still not, because the right fix is a
//     family-level check rather than a sixth entry in a per-repo test.
//
// So the manifest's version is checked against the version the repository
// declares to its own build system. This matters more than it looks: every
// verdict `mago blast` computes is arithmetic over that number, and a stale
// one makes every one of them confidently wrong.
//
// Nothing here parses a build system. It reads the handful of places a version
// is written as a literal, and reports what it finds; a file it cannot read is
// not a finding.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mago/semver.hpp"

namespace mago {

// One place a repository states its own version.
struct VersionClaim {
    std::string file;   // relative to the project root, e.g. "pyproject.toml"
    std::string where;  // the line that said it, trimmed
    std::optional<Version> version;
};

// Every version literal found in the well-known files at `dir`, in a stable
// order. Only files that exist are read, and only the first claim per file is
// taken -- a second `version =` in a pyproject is a dependency pin, not the
// project's own version.
std::vector<VersionClaim> version_claims(const std::filesystem::path& dir);

// The files above, by name, for a manifest that wants to narrow or widen the
// set with `version_sources`.
const std::vector<std::string>& default_version_sources();

}  // namespace mago
