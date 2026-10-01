// What a built executable actually asks the loader for.
//
// The manifest says which files belong in the package. The BINARY says which
// files it cannot start without, and the two are not the same list -- they are
// only ever the same list by accident or by checking.
//
// The case that motivated this, found while staging Void Hormiga for the first
// time: `voidhormiga.exe` imports `libwinpthread-1.dll`, a MinGW runtime that
// exists in the developer's toolchain directory and on no other machine in the
// world. Nothing in any manifest mentions it. The installer would have
// compiled cleanly, installed cleanly, and the application would have failed to
// start on the first device it was tested on, with a dialog naming a DLL
// nobody had ever heard of.
//
// That is precisely the class of failure Void Mago exists to catch -- "it
// passes its own tests, but what breaks somewhere else" -- so it is worth a
// PE parser. This one reads the import directory and nothing else: ~120 lines,
// no dependencies, and it does not need to be correct about anything except
// the list of names.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mago {

// The DLL names in a PE file's import directory, in file order. Empty for a
// file that is not a PE image, which is not an error -- an ELF or a Mach-O
// binary simply has nothing to say to this function yet.
std::vector<std::string> pe_imports(const std::filesystem::path& binary);

// Is this a library Windows itself provides?
//
// Deliberately generous: the cost of a false "system" is a missing DLL that
// the person discovers on the target machine, and the cost of a false
// "bundled" is one line of noise in a report. The first is worse, so the list
// stays short and specific rather than pattern-matching anything plausible.
bool is_system_library(const std::string& dll);

}  // namespace mago
