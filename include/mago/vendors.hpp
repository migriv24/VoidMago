// The third-party inventory: what the family uses and does not own.
//
// `mago matrix` answers "how do these Void projects relate to each other".
// This answers the question underneath it, which nobody had been asking:
// **what else is in these binaries.**
//
// The three findings this exists to produce, in order of how much they cost
// when nobody is looking:
//
//   1. DRIFT. The same library at two versions in one family. cJSON is
//      vendored in six repositories; they agree today by discipline, and two
//      of those copies are compiled into the same executable. The day they
//      stop agreeing, one set of symbols wins the link and the loser's callers
//      get subtly different behaviour with no error anywhere.
//
//   2. FILES THAT MUST SHIP AND ARE NOT DECLARED. `libwinpthread-1.dll` was
//      found by reading Void Hormiga's import table, not by reading anybody's
//      manifest, because no manifest had a place to put it.
//
//   3. LICENCES NOBODY IS TRACKING. Five repositories keep a hand-maintained
//      THIRD-PARTY-NOTICES.md. Hand-maintained lists drift from the code they
//      describe -- that is the whole premise of this project applied to a file
//      that happens to be a legal obligation.
//
// And one thing it does NOT do: resolve, fetch, or update third-party code.
// The family vendors deliberately. This is an inventory of what was vendored,
// not a package manager, and the difference is that nothing here ever changes
// a byte in anybody's `vendor/` directory.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "mago/registry.hpp"

namespace mago {

// One project's use of one library.
struct VendorUse {
    std::string project;
    Vendor declared;
};

// One library, everywhere it appears.
struct Library {
    std::string name;                 // the canonical spelling, first seen
    std::vector<VendorUse> uses;

    // Every distinct version string anybody declared, sorted. More than one is
    // drift; empty means nobody said.
    std::vector<std::string> versions;

    // Licences declared for it. More than one is a project describing the same
    // library differently, which is worth a look.
    std::vector<std::string> licenses;

    bool drifts() const { return versions.size() > 1; }
    bool unversioned() const { return versions.empty(); }
};

// A directory that looks vendored and that no manifest accounts for.
struct UndeclaredVendoring {
    std::string project;
    std::string path;   // relative to the checkout
};

struct Inventory {
    std::vector<Library> libraries;         // sorted by name
    std::vector<UndeclaredVendoring> undeclared;

    // Libraries at more than one version somewhere in the family.
    std::vector<const Library*> drifting() const;

    // Declared `shared` or `toolchain`: a real file that has to reach the
    // machine, or the application does not start.
    std::vector<const Library*> must_ship() const;

    std::size_t declarations() const;
    std::string to_json(int indent = 2) const;
};

// Read every manifest's `vendors[]`, and look for vendored directories nobody
// declared.
Inventory take_inventory(const Registry& reg);

// The directories a project might keep third-party code in. Convention, not
// configuration: a manifest that declares its vendors does not need this, and
// one that declares none is exactly the case worth guessing about.
const std::vector<std::string>& vendor_directories();

// Does `file` contain `version`, either literally ("3.45.1") or split across
// separate macros the way cJSON writes it (MAJOR 1 / MINOR 7 / PATCH 18)?
//
// Deliberately loose. This confirms a declared version rather than deriving
// one, so a false "cannot confirm" is a note somebody reads and a false
// "confirmed" costs nothing that was not already wrong.
bool version_appears_in(const std::filesystem::path& file, const std::string& version);

}  // namespace mago
