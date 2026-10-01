// The Release tab -- the blast radius, and the broadcast that carries it.
//
// `mago notify` already produces the mechanical four fifths of a release
// message and leaves section 4 -- what this release means for YOU
// specifically -- as a marked hole. Void Core's own account of writing eight
// of these by hand is that section 4 was the most valuable part of every one
// of them, and the part that does not scale.
//
// A hole with a recipient's name on it is a text box. That is this tab.

#include "app.hpp"
#include "imgui.h"

namespace magod {
namespace {

ImVec4 verdict_colour(mago::Verdict v) {
    switch (v) {
        case mago::Verdict::Blocked: return ImVec4(0.85f, 0.30f, 0.30f, 1.0f);
        case mago::Verdict::Rebuild: return ImVec4(0.85f, 0.55f, 0.20f, 1.0f);
        case mago::Verdict::Reship: return ImVec4(0.35f, 0.70f, 0.45f, 1.0f);
        case mago::Verdict::Retest: return ImVec4(0.35f, 0.60f, 0.85f, 1.0f);
        case mago::Verdict::Untouched: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

}  // namespace

void draw_release(App& app) {
    if (app.registry.projects().empty()) {
        ImGui::TextUnformatted("Nothing to release: no manifests found.");
        return;
    }

    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("project", app.release_project.c_str())) {
        for (const auto& [name, m] : app.registry.projects()) {
            if (ImGui::Selectable(name.c_str(), name == app.release_project)) {
                app.release_project = name;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("same as: mago notify %s", app.release_project.c_str());
    ImGui::Separator();

    if (!app.registry.contains(app.release_project)) return;

    mago::Broadcast b;
    std::vector<mago::Message> messages;
    try {
        b = mago::gather(app.registry, app.release_project, std::nullopt, std::nullopt);
        messages = mago::compose(app.registry, b);
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(0.85f, 0.3f, 0.3f, 1), "%s", e.what());
        return;
    }

    ImGui::Text("%s  %s -> %s", b.display_name.c_str(), b.from.str().c_str(),
                (b.to ? b.to->str() : b.from.str()).c_str());
    ImGui::SameLine();
    ImGui::TextColored(b.change == mago::Change::Breaking ? ImVec4(0.85f, 0.4f, 0.3f, 1)
                                                          : ImVec4(0.4f, 0.7f, 0.5f, 1),
                       "%s", mago::change_name(b.change));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", b.declared ? "(declared by the release block)"
                                         : "(inferred from the version numbers)");
    if (!b.summary.empty()) ImGui::TextWrapped("%s", b.summary.c_str());

    if (!b.declared) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "This project declares no `release` block, so the one fact a resolver cannot "
            "derive -- whether the change was additive or breaking -- is being guessed from "
            "the version numbers. Add `release` to its void.json and the guess becomes a "
            "statement.");
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("verdicts", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("project", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("verdict", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("why");
        ImGui::TableHeadersRow();
        for (const auto& impact : b.report.impacts) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(impact.display_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(verdict_colour(impact.verdict), "%s",
                               mago::verdict_name(impact.verdict));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", impact.reason.c_str());
        }
        ImGui::EndTable();
    }

    if (!b.report.unverified.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.80f, 0.55f, 0.20f, 1.0f),
                           "%zu named as partners with no manifest here -- no verdict, no message",
                           b.report.unverified.size());
        for (const auto& u : b.report.unverified) {
            ImGui::BulletText("%s   (%s)", u.name.c_str(), u.how.c_str());
        }
    }

    if (!b.behavior_changes.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("behavior changes -- what a resolver cannot see");
        for (const auto& bc : b.behavior_changes) {
            ImGui::BulletText("%s", bc.what.c_str());
            if (!bc.who_is_affected.empty()) {
                ImGui::Indent();
                ImGui::TextDisabled("affects: %s", bc.who_is_affected.c_str());
                ImGui::Unindent();
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("messages");
    if (messages.empty()) {
        ImGui::TextUnformatted("Nothing downstream has anything to do. Release it.");
        return;
    }
    for (const auto& m : messages) {
        ImGui::BulletText("%s", m.display_name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(verdict_colour(m.verdict), "%s", mago::verdict_name(m.verdict));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m.path.filename().string().c_str());
    }

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Section 4 of each message -- what this release means for that project in particular "
        "-- is left as a marked hole. Writing those here, one recipient at a time, is what "
        "this tab is for; it is not built yet, and `mago notify %s --write` sends them with "
        "the holes empty.",
        app.release_project.c_str());
}

}  // namespace magod
