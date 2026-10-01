// Emit an NSIS installer script from one or more install plans.
//
// Void Mago does not install anything itself. It writes the script that a
// real, maintained installer engine compiles -- here `makensis`. The reason is
// the same one that keeps the family from writing its own JSON parser:
// staging, elevation, uninstall registration and Add/Remove Programs entries
// are solved problems, and the part nobody else can compute is which files
// belong in the script at all.
//
// A suite script gets one selectable COMPONENT PER APPLICATION, because a
// person's list has one row per application. Runtimes are folded into the
// component that needs them and never appear as a checkbox -- surfacing them
// would undo the property that makes them free.

#pragma once

#include <string>
#include <vector>

#include "mago/resolve.hpp"

namespace mago::nsis {

inline constexpr const char* kReceiptName = "mago-receipt.json";

struct Options {
    std::string suite_name = "Void Suite";
    std::string suite_dir = "VoidSuite";
    std::string suite_version = "0.1.0";
    std::string publisher = "Void";
    std::string outfile = "VoidSuiteSetup.exe";
    std::string license_file = "LICENSE";
    std::string source_root = "..\\dist";
    // Where the compiling machine will find the receipt that `receipt()` wrote.
    std::string receipt_source = kReceiptName;
};

// A complete `.nsi` script for these plans. Throws std::invalid_argument if
// there is nothing to install.
std::string render(const std::vector<Plan>& plans, const Options& opt = {});

// The install receipt, written beside the binaries and read back later.
std::string receipt(const std::vector<Plan>& plans, const std::string& suite_name);

}  // namespace mago::nsis
