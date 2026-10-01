// The Graph tab -- the same content as the matrix, projected onto a canvas.
//
// This is the tab that uses Void Maiz for what Void Maiz is: it draws a Void
// Core mantle as a node graph. The family is not a tree and drawing it as one
// would lie about it -- `requires` is a DAG, seams are an undirected
// multigraph over the same vertices, and the two overlap on several pairs.
//
// Everything on screen is a field the matrix already carried: one rune per
// project (glyph by kind), one link per bonded pair, direction from the
// pairing's antisymmetric part, weight from its cost. Layout is Void Maiz's
// depth auto-layout; camera and selection are its EditorState. Desktop
// contributes no geometry, no ordering and no colour of its own.
//
// The commands `edit_canvas` compiles are dispatched straight back into the
// core, which is the one-sync rule: a view never mutates its own picture, it
// dispatches and re-projects. Those commands are all view ephemera here --
// dragging a node moves it in a mantle that is rebuilt from the documents on
// the next reload, and moves nothing in anybody's repository.

#include "app.hpp"
#include "imgui.h"

namespace magod {

void draw_graph(App& app) {
    if (ImGui::Button("Re-project")) app.project_graph();
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        app.ed.cam = maiz::Camera{};
        app.ed.cam.x = -40;
        app.ed.cam.y = -40;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Shapes by kind", &app.shapes_by_kind)) app.project_graph();
    help_marker(
        "A hexagon is a runtime -- a substrate everything else sits on. A circle is a "
        "capability: no orientation, because you develop with it rather than install it. A "
        "windowed body is an application, the only one of the three a person ever sees by "
        "name.\n\n"
        "Shape is a second channel on purpose: colour must never be the only signal. Both are "
        "Void Maiz `hints` keys -- Desktop draws no geometry of its own.");
    ImGui::SameLine();
    ImGui::Text("%zu runes   %zu wires", app.scene.nodes.size(), app.scene.wires.size());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "|  drag to move, right-drag to pan, wheel to zoom  --  positions are view state");
    ImGui::Separator();

    if (!app.graph_built || app.scene.nodes.empty()) {
        ImGui::TextWrapped(
            "Nothing projected yet. The graph is built from the same matrix the Family tab "
            "shows; if that is empty, register a folder in the Registry tab.");
        return;
    }

    const maiz::CanvasIO io =
        maiz::edit_canvas("family-canvas", app.scene, app.ed, app.canvas_style);

    for (const auto& command : io.commands) {
        const maiz::Result r = app.core.dispatch(command);
        if (!r.ok) app.say("error", command + ": " + r.text());
    }
    if (!io.commands.empty()) {
        app.scene = maiz::project_scene(app.core);  // dispatch, then re-project
    }
}

// Known and reported, so it is not mistaken for something to fix here: every
// wire in this graph is `Kind::Loose` (no ports), and Void Maiz gives a loose
// wire a fixed upward tangent, so a horizontal pair draws as a hump and a
// dozen wires stack into parallel arcs. Sent as
// MESSAGE_FOR_VOIDMAIZ_mago-wire-routing-for-loose-graphs-2026-09-04.md.
//
// Deliberately NOT worked around here: computing our own wire paths would be
// the first piece of geometry in a binary whose whole rule is that it contains
// none.

}  // namespace magod
