// From an application name to an install plan.
//
// The resolver answers two different questions that are easy to conflate:
//
//   What has to be BUILT, and in what order?  Every project in the transitive
//   `requires` closure, dependencies first.
//
//   What lands on the MACHINE?  Far less than that. A statically linked
//   dependency is already inside the consumer's binary; putting it in the file
//   list would describe an install that does not exist.
//
// Keeping those apart is most of the value here.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mago/json_out.hpp"
#include "mago/registry.hpp"

namespace mago {

// What a resolved dependency costs the person installing it.
enum class Role {
    Payload,   // a file that ships: a shared library, a data folder
    Absorbed,  // compiled into a consumer; no file of its own
    Root,      // the application being installed
};

const char* role_name(Role r);

class ResolveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One project in a closure, with the constraints that put it there.
struct Resolved {
    std::string name;
    std::string display_name;
    std::string kind;
    Version version;
    Role role = Role::Absorbed;
    std::vector<Range> constraints;
    std::vector<std::string> required_by;
    std::string link;
    std::string via;
    std::vector<std::string> files;

    Range effective_floor() const { return strictest(constraints); }
};

// A complete, ordered account of one install.
struct Plan {
    std::string application;
    std::string display_name;
    std::string description;
    Version version;
    std::string platform;

    std::vector<Resolved> build_order;
    std::vector<Resolved> payload;
    std::vector<Resolved> absorbed;
    std::vector<std::string> notes;
    std::string generated;  // ISO-8601 UTC

    // Side-by-side by construction: the version is in the path. A new version
    // never overwrites a working one, which is the whole reason
    // `update.side_by_side` is true in every manifest in the family.
    std::string install_dir_name() const;

    std::string to_json(int indent = 2) const;

    // Write this plan as an object into an open writer. The receipt embeds a
    // plan verbatim, and a second encoding of the same document would be a
    // second definition to keep in step.
    void write_json(JsonWriter& w) const;
};

// THREE NAMES FOR ONE FILE, and conflating any two of them is a bug that
// only shows up on somebody else's machine.
//
//   the BUILD output     libvoidcore.dll            what the compiler wrote
//   the RELEASE asset    libvoidcore-win-x64.dll    what a manifest declares,
//                                                   because a release page
//                                                   holds every platform at once
//   the INSTALLED file   libvoidcore.dll            what the consumer's binary
//                                                   asks the loader for
//
// The platform tag exists to keep three files apart in one folder on a release
// page. It must not survive onto the machine: an executable linked against
// `libvoidcore.dll` does not start next to a file called
// `libvoidcore-win-x64.dll`.
std::string installed_name(const std::string& declared, const std::string& platform);

// Build the install plan for one application. `platform` may be empty.
Plan resolve(const Registry& reg, const std::string& app_name,
             const std::string& platform = "");

}  // namespace mago
