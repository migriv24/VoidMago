// The Family tab -- the interaction matrix, and the box you type the sentence
// into.
//
// This is the reason the GUI exists. The table has 21 cells today and 91 when
// the seven unmanifested projects have manifests; authoring those from a
// terminal means typing two project names per cell, and authoring them here
// means clicking the cell you are already looking at. Nothing new is computed
// -- the friction is a hundredth.

#include <algorithm>

#include "app.hpp"
#include "imgui.h"

namespace magod {
namespace {

ImVec4 bond_colour(mago::Bond bond, bool disputed) {
    if (disputed) return ImVec4(0.85f, 0.35f, 0.25f, 1.0f);
    switch (bond) {
        case mago::Bond::Requires: return ImVec4(0.80f, 0.55f, 0.20f, 1.0f);
        case mago::Bond::Seam: return ImVec4(0.30f, 0.60f, 0.80f, 1.0f);
        case mago::Bond::Authored: return ImVec4(0.55f, 0.45f, 0.80f, 1.0f);
        case mago::Bond::None: return ImVec4(0.50f, 0.50f, 0.50f, 0.45f);
    }
    return ImVec4(1, 1, 1, 1);
}

// The same glyphs `mago matrix` prints, so the terminal and the window are
// legible to each other.
const char* cell_glyph(const mago::Cell& c) {
    if (!c.pairing) return ".";
    switch (c.pairing->bond) {
        case mago::Bond::None: return ".";
        case mago::Bond::Authored: return "+";
        case mago::Bond::Seam: return "-";
        case mago::Bond::Requires:
            return c.direction > 0 ? ">" : (c.direction < 0 ? "<" : "=");
    }
    return ".";
}

void set_buffer(char* buffer, std::size_t size, const std::string& value) {
    const std::size_t n = std::min(value.size(), size - 1);
    std::copy_n(value.begin(), n, buffer);
    buffer[n] = '\0';
}

void copy_selection_into_buffers(App& app) {
    set_buffer(app.note_buffer, sizeof(app.note_buffer), "");
    set_buffer(app.seam_buffer, sizeof(app.seam_buffer), "");
    set_buffer(app.shares_buffer, sizeof(app.shares_buffer), "");

    const mago::Cell cell = app.selected_cell();
    if (!cell.pairing) return;
    if (const mago::AuthoredPairing* authored =
            app.family.pairing(cell.pairing->a, cell.pairing->b)) {
        set_buffer(app.note_buffer, sizeof(app.note_buffer), authored->note);
        set_buffer(app.seam_buffer, sizeof(app.seam_buffer), authored->seam);
        std::string shares;
        for (const auto& s : authored->shares) {
            if (!shares.empty()) shares += ", ";
            shares += s;
        }
        set_buffer(app.shares_buffer, sizeof(app.shares_buffer), shares);
    } else if (cell.pairing->disputed) {
        // A disputed seam is the one case where the box should not start
        // blank: the two candidate names are exactly what has to be chosen
        // between, and one of them is almost always the answer.
        set_buffer(app.seam_buffer, sizeof(app.seam_buffer), cell.pairing->seam);
    }
}

// The one write this window performs. It goes through `mago::Family`, which is
// the same code path `mago pair --note` uses -- there is no second way to
// author a pairing and no second idea of what the document looks like.
void commit(App& app) {
    const mago::Cell cell = app.selected_cell();
    if (!cell.pairing) return;

    mago::AuthoredPairing p;
    p.a = cell.pairing->a;
    p.b = cell.pairing->b;
    p.note = app.note_buffer;
    p.seam = app.seam_buffer;
    p.status = "agreed";
    std::string item;
    for (char ch : std::string(app.shares_buffer) + ",") {
        if (ch == ',') {
            const auto begin = item.find_first_not_of(" \t");
            const auto end = item.find_last_not_of(" \t");
            if (begin != std::string::npos) p.shares.push_back(item.substr(begin, end - begin + 1));
            item.clear();
        } else {
            item += ch;
        }
    }
    if (!p.empty()) p.authored = mago::today();

    try {
        app.family.set_pairing(p);
        app.family.save(app.family_path);
        app.say("ok", (p.empty() ? "cleared " : "wrote ") + p.a + " / " + p.b + " in " +
                          app.family_path.filename().string());
        app.reload();
    } catch (const std::exception& e) {
        app.say("error", e.what());
    }
}

void draw_grid(App& app) {
    const mago::Matrix& m = app.matrix;
    const std::size_t n = m.size();
    if (n == 0) {
        ImGui::TextUnformatted("No manifests found. Register a folder in the Registry tab.");
        return;
    }

    const int columns = static_cast<int>(n) + 1;
    if (!ImGui::BeginTable("matrix", columns,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                               ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    for (std::size_t j = 0; j < n; ++j) {
        ImGui::TableSetupColumn(std::to_string(j + 1).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                26.0f);
    }
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < n; ++i) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const mago::Diagonal& d = m.diagonal()[i];
        ImGui::Text("%zu  %s", i + 1, d.display_name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s %s\n%s\n%zu pairs with an edge", d.name.c_str(),
                              d.version.str().c_str(), d.kind.c_str(), d.degree);
        }

        for (std::size_t j = 0; j < n; ++j) {
            ImGui::TableSetColumnIndex(static_cast<int>(j) + 1);
            if (i == j) {
                ImGui::TextDisabled("#");  // the diagonal is the project, not an interaction
                continue;
            }
            const mago::Cell cell = m.at(i, j);
            const bool selected = app.has_selection &&
                                  ((app.sel_row == i && app.sel_col == j) ||
                                   (app.sel_row == j && app.sel_col == i));

            ImGui::PushID(static_cast<int>(i * n + j));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  bond_colour(cell.pairing ? cell.pairing->bond : mago::Bond::None,
                                              cell.pairing && cell.pairing->disputed));
            std::string label = cell_glyph(cell);
            if (cell.pairing && cell.pairing->authored()) label += "*";
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(20, 0))) {
                app.sel_row = i;
                app.sel_col = j;
                app.has_selection = true;
                copy_selection_into_buffers(app);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && cell.pairing) {
                ImGui::SetTooltip("%s / %s\n%s, %s", cell.pairing->a.c_str(),
                                  cell.pairing->b.c_str(), mago::bond_name(cell.pairing->bond),
                                  mago::verdict_name(cell.pairing->cost));
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void draw_pair_panel(App& app) {
    const mago::Cell cell = app.selected_cell();
    if (!cell.pairing) {
        ImGui::TextWrapped(
            "Pick a cell. The table is symmetric -- the pair is one record, read from either "
            "side, and only the arrow flips.");
        return;
    }
    const mago::Pairing& p = *cell.pairing;

    const char* arrow = cell.direction > 0 ? "  ->  " : (cell.direction < 0 ? "  <-  " : "  --  ");
    ImGui::TextUnformatted((app.matrix.names()[app.sel_row] + arrow +
                            app.matrix.names()[app.sel_col])
                               .c_str());
    ImGui::Separator();

    ImGui::TextDisabled("bond");
    help_marker(
        "How tightly they touch: none < authored < seam < requires. A pair described BOTH "
        "ways takes the tighter of the two, because that is what it costs.");
    ImGui::SameLine(120);
    ImGui::TextColored(bond_colour(p.bond, p.disputed), "%s%s", mago::bond_name(p.bond),
                       p.disputed ? "   DISPUTED" : "");
    ImGui::TextDisabled("cost");
    ImGui::SameLine(90);
    ImGui::Text("%s", mago::verdict_name(p.cost));
    ImGui::SameLine();
    ImGui::TextDisabled("(an additive change at either end)");
    if (!p.link.empty()) {
        ImGui::TextDisabled("link");
        ImGui::SameLine(90);
        ImGui::Text("%s %s", p.link.c_str(), p.version.c_str());
    }
    if (!p.seam.empty()) {
        ImGui::TextDisabled("seam");
        ImGui::SameLine(90);
        ImGui::Text("%s", p.seam.c_str());
    }

    if (!p.why.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("why -- every piece of evidence, from both manifests");
        for (const auto& line : p.why) ImGui::BulletText("%s", line.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("what you know  ->  %s", app.family_path.filename().string().c_str());
    help_marker(
        "A fact about a PAIR belongs to neither repository's manifest -- Void Core and Void "
        "Palabra each describe their shared seam differently, and both are right from inside "
        "their own repository. This document is the only place the pair itself can be "
        "described. Nothing in it is computed.");
    ImGui::TextWrapped(
        "A fact about a PAIR belongs to neither repository's manifest. This is the only place "
        "it can live.");
    ImGui::Spacing();

    ImGui::InputTextMultiline("##note", app.note_buffer, sizeof(app.note_buffer),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 6));
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("seam", app.seam_buffer, sizeof(app.seam_buffer));
    help_marker(
        "The AGREED name for what crosses between these two -- a transcript, a conformance "
        "case, a JSON document. Filling it in on a disputed pair settles it: the cell stops "
        "showing '!' and `mago doctor` starts carrying the answer instead of the argument.");
    if (p.disputed) {
        ImGui::TextColored(bond_colour(p.bond, true),
                           "the two manifests disagree; naming it here settles it");
    }
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("shares", app.shares_buffer, sizeof(app.shares_buffer));
    help_marker(
        "What actually crosses -- the CONTENT, not the mechanism. 'OKF bundles', 'the glyph "
        "vocabulary', 'an assets folder'. It answers 'what would break if these two stopped "
        "talking?', and it is what becomes a port mapping when a boundary becomes a process "
        "boundary.");
    ImGui::TextDisabled("comma separated");

    ImGui::Spacing();
    if (ImGui::Button("Save")) commit(app);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        set_buffer(app.note_buffer, sizeof(app.note_buffer), "");
        set_buffer(app.seam_buffer, sizeof(app.seam_buffer), "");
        set_buffer(app.shares_buffer, sizeof(app.shares_buffer), "");
        commit(app);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("same as: mago pair %s %s --note \"...\"", p.a.c_str(), p.b.c_str());
}

}  // namespace

void draw_family(App& app) {
    if (ImGui::Button("Rescan")) app.reload();
    ImGui::SameLine();
    ImGui::Text("%zu projects   %zu pairs   %zu bonded   %zu with a note", app.matrix.size(),
                app.matrix.cells(), app.matrix.bonded(), app.matrix.authored());
    ImGui::SameLine();
    ImGui::TextDisabled("|  > requires   - seam   + authored   * has a note   ! disputed");
    ImGui::Separator();

    const float panel_w = 380.0f;
    const float grid_w = ImGui::GetContentRegionAvail().x - panel_w - 8.0f;

    ImGui::BeginChild("grid", ImVec2(grid_w > 200 ? grid_w : 0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw_grid(app);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("pair", ImVec2(0, 0), true);
    draw_pair_panel(app);
    ImGui::EndChild();
}

}  // namespace magod
