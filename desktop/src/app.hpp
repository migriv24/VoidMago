// Void Mago Desktop -- the state, which is deliberately almost nothing.
//
// THE RULE (okf/concepts/desktop.md):
//
//     Desktop renders documents Void Mago produces, and edits one document
//     Void Mago writes. Delete Desktop and no answer changes; delete the CLI
//     and nothing works.
//
// So this struct holds exactly three kinds of thing:
//
//   1. DOCUMENTS, as `libvoidmago` produced them -- Registry, Family, Matrix,
//      the doctor findings. Never edited in place, never partially updated:
//      `reload()` throws the lot away and asks the library again, which is the
//      same one-sync rule Void Maiz follows over Void Core ("a view never
//      mutates its own picture -- it dispatches, then re-projects").
//
//   2. EPHEMERA -- which tab is open, which cell is selected, what is typed in
//      a text box that has not been committed. None of it is truth and none of
//      it is saved.
//
//   3. ONE core, for the graph tab, holding a projection of (1). Also not
//      truth: it is rebuilt from the documents on every reload.
//
// There is no fourth kind, and adding one is the mistake this file exists to
// prevent. If Desktop ever needs an answer it cannot get by asking the
// library, the answer belongs in the library.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mago/blast.hpp"
#include "mago/family.hpp"
#include "mago/home.hpp"
#include "mago/matrix.hpp"
#include "mago/notify.hpp"
#include "mago/registry.hpp"
#include "voidmaiz/canvas.hpp"
#include "voidmaiz/editor.hpp"
#include "voidmaiz/embed.hpp"
#include "voidmaiz/project.hpp"
#include "voidmaiz/scene.hpp"

namespace magod {

// One line for the strip along the bottom -- what the app did, and what the
// library said back. Every write goes through here, so the window can never
// have changed something the strip did not report.
struct LogLine {
    std::string level;  // "ok" | "error" | "note"
    std::string text;
};

struct App {
    // ── (1) the documents ───────────────────────────────────────────────────
    mago::Registry registry;
    mago::Family family;
    mago::Matrix matrix;
    std::vector<mago::Finding> findings;
    mago::Home home;
    std::filesystem::path family_path;
    std::size_t errors = 0, warnings = 0, notes = 0;

    // ── (2) ephemera ────────────────────────────────────────────────────────
    int tab = 0;
    std::size_t sel_row = 0;   // the selected cell, as matrix indices
    std::size_t sel_col = 0;
    bool has_selection = false;

    // Plain char buffers because Dear ImGui edits those and Void Maiz does not
    // vendor imgui_stdlib. Uncommitted by definition: nothing here is a fact
    // until it has gone through mago::Family and back out of a reload().
    char note_buffer[2048] = {};
    char seam_buffer[160] = {};
    char shares_buffer[512] = {};
    char root_buffer[512] = {};
    char label_buffer[160] = {};

    char command_buffer[512] = {};   // the Command tab's input
    std::string command_output;      // and what the CLI printed back
    std::string command_ran;         // the argument line that produced it
    int command_exit = 0;

    std::string release_project;  // whose broadcast is on screen

    bool light_mode = true;
    bool show_help = false;
    bool shapes_by_kind = true;

    // The interface's scale, which is NOT the canvas camera's zoom. Text and
    // chrome scale here; the drawing scales in maiz::EditorState::cam. Keeping
    // them separate is the difference between "the words are too small" and
    // "I want to see more of the graph", which are different complaints.
    float ui_scale = 1.0f;

    std::vector<LogLine> log;

    // ── (3) the graph's core ────────────────────────────────────────────────
    maiz::Core core;
    maiz::Scene scene;
    maiz::EditorState ed;
    maiz::CanvasStyle canvas_style;
    bool graph_built = false;

    // Throw everything away and ask the library again. The ONLY way a document
    // in this struct changes.
    void reload();

    // Rebuild the graph tab's mantle from the matrix. Called by reload().
    void project_graph();

    void say(const std::string& level, const std::string& text);

    const mago::Manifest* selected_project() const;
    mago::Cell selected_cell() const;
};

// Each surface, drawn between Begin()/End() by main.cpp. None of them computes
// anything; every one of them calls back into `libvoidmago` to act.
void draw_family(App& app);
void draw_graph(App& app);
void draw_registry(App& app);
void draw_release(App& app);

// The help window, and the (?) beside a word that carries weight.
void draw_help(App& app);
void help_marker(const char* text);

}  // namespace magod
