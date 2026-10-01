// The two things a window needs that a library cannot give it: a folder
// picker, and a way to run the CLI.
//
// Both are deliberately thin. The picker returns a path and decides nothing
// about it; the runner returns whatever `mago` printed and interprets none of
// it. Neither is a place for logic to accumulate.

#pragma once

#include <filesystem>
#include <string>

namespace magod {

// A native "choose a folder" dialog. Returns an empty path if the person
// cancelled -- which is not an error and must not be reported as one.
// `start_in` seeds the dialog; empty means the system's default.
std::filesystem::path pick_folder(const std::string& title,
                                  const std::filesystem::path& start_in = {});

struct CommandResult {
    bool ran = false;   // did the binary start at all
    int exit_code = 0;
    std::string output;  // stdout and stderr, interleaved as they arrived
};

// Where the `mago` binary is, or an empty path. Looked for beside this
// executable first, then in the sibling CLI build, then on PATH.
std::filesystem::path find_mago();

// Run `mago <args>` and capture what it printed.
//
// SHELLING OUT IS THE POINT. The desktop links `libvoidmago`, so it could call
// the verbs directly -- but then it would need its own argument parser, and a
// second parser is a second idea of what the commands are. Running the real
// binary makes "the GUI can do nothing the CLI cannot" true by construction
// rather than by discipline.
CommandResult run_mago(const std::string& args);

}  // namespace magod
