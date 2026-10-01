// The Registry tab -- which folders this machine knows, and what is missing
// from them.
//
// The one surface with real work behind it, and the work is all in the
// library: `mago::Home` remembers the roots, `mago::Registry::scan` reads
// them, and this file draws two lists and calls those two.
//
// The second list is the important one. Seven Void projects carry no
// `void.json`, and Mago cannot compute a verdict for a project it has never
// read -- it now says so on every run, in `doctor` and under UNVERIFIED in
// every blast. Turning that list into manifests is the largest single
// improvement available to the whole project, and it is what this tab is for.
//
// What this tab must never become: an editor for other projects' manifests.
// Each repository owns its own `void.json`; a central GUI that edited
// everybody's would make the registry the source of truth instead of the
// repositories, which is backwards from the family's entire convention.

#include <algorithm>

#include "app.hpp"
#include "imgui.h"
#include "platform.hpp"

namespace magod {
namespace {

void set_buffer(char* buffer, std::size_t size, const std::string& value) {
    const std::size_t n = std::min(value.size(), size - 1);
    std::copy_n(value.begin(), n, buffer);
    buffer[n] = '\0';
}

void draw_roots(App& app) {
    ImGui::TextDisabled("folders this machine scans   ->   %s",
                        mago::Home::roots_file().string().c_str());
    ImGui::Spacing();

    if (app.home.empty()) {
        ImGui::TextWrapped(
            "Nothing registered, so Mago falls back to the folder above this repository. "
            "Register one and it is remembered between runs -- for the CLI too.");
    }

    for (const auto& root : app.home.roots()) {
        ImGui::PushID(root.path.string().c_str());
        if (ImGui::SmallButton("Forget")) {
            try {
                mago::Home home = mago::Home::load();
                home.remove(root.path);
                home.save();
                app.say("ok", "forgot " + root.path.string());
                app.reload();
            } catch (const std::exception& e) {
                app.say("error", e.what());
            }
        }
        ImGui::SameLine();
        if (!root.exists) {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.25f, 1.0f), "MISSING");
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(root.path.string().c_str());
        if (!root.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("-- %s", root.label.c_str());
        }
        ImGui::PopID();
    }

    ImGui::Spacing();

    // Browse first, because typing a path is the thing nobody wants to do.
    // The picker opens where you already are: the first registered root, or
    // wherever this window was launched from.
    if (ImGui::Button("Choose a folder...")) {
        std::error_code ec;
        const std::filesystem::path start =
            app.home.roots().empty() ? std::filesystem::current_path(ec).parent_path()
                                     : app.home.roots().front().path;
        const std::filesystem::path chosen =
            pick_folder("Choose a folder holding Void checkouts", start);
        if (!chosen.empty()) {
            // Say what is in there BEFORE it is registered. A folder with no
            // manifests in it is almost always the wrong folder -- one level
            // too deep, or one too shallow -- and finding that out from an
            // empty table later is a worse way to learn it.
            const mago::Registry found = mago::Registry::scan(chosen);
            set_buffer(app.root_buffer, sizeof(app.root_buffer), chosen.string());
            app.say(found.projects().empty() ? "warn" : "ok",
                    chosen.string() + ": " + std::to_string(found.projects().size()) +
                        " with a manifest, " + std::to_string(found.unmanifested().size()) +
                        " that look like Void projects and carry none" +
                        (found.projects().empty()
                             ? " -- is this the folder that HOLDS the checkouts, rather than "
                               "one of them?"
                             : ""));
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-260);
    ImGui::InputTextWithHint("##root", "...or type a path", app.root_buffer,
                             sizeof(app.root_buffer));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputTextWithHint("##label", "label", app.label_buffer, sizeof(app.label_buffer));
    ImGui::SameLine();

    const bool have_path = app.root_buffer[0] != '\0';
    ImGui::BeginDisabled(!have_path);
    if (ImGui::Button("Register")) {
        try {
            const std::filesystem::path path(app.root_buffer);
            std::error_code ec;
            if (!std::filesystem::is_directory(path, ec)) {
                app.say("error", path.string() + " is not a directory");
            } else {
                mago::Home home = mago::Home::load();
                const bool added = home.add(path, app.label_buffer, mago::today());
                home.save();
                app.say(added ? "ok" : "note",
                        (added ? "registered " : "already registered ") + path.string());
                app.root_buffer[0] = '\0';
                app.label_buffer[0] = '\0';
                app.reload();
            }
        } catch (const std::exception& e) {
            app.say("error", e.what());
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("same as: mago register <folder>");
    help_marker(
        "A root is a folder that HOLDS Void checkouts -- the one with VoidCore, VoidMaiz and "
        "the rest inside it -- not a project itself. Mago reads one directory deep. Registering "
        "it is remembered for the CLI too, so `mago scan` stops needing --root.");
}

void draw_projects(App& app) {
    if (!ImGui::BeginTable("projects", 5,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("project");
    ImGui::TableSetupColumn("kind");
    ImGui::TableSetupColumn("version");
    ImGui::TableSetupColumn("pairs");
    ImGui::TableSetupColumn("where");
    ImGui::TableHeadersRow();

    for (const auto& d : app.matrix.diagonal()) {
        const mago::Manifest* m = app.registry.get(d.name);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(d.display_name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(d.kind.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(d.version.str().c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%zu", d.degree);
        ImGui::TableSetColumnIndex(4);
        ImGui::TextDisabled("%s", m ? m->path.string().c_str() : "");
    }
    ImGui::EndTable();
}

void draw_gap(App& app) {
    const auto& missing = app.registry.unmanifested();
    if (missing.empty()) {
        ImGui::TextDisabled("Every folder that looks like a Void project carries a manifest.");
        return;
    }
    ImGui::TextColored(ImVec4(0.80f, 0.55f, 0.20f, 1.0f), "%zu look like Void projects and carry no %s",
                       missing.size(), mago::kManifestName);
    ImGui::TextWrapped(
        "Mago cannot compute a verdict for a project it has never read, and an absent row "
        "reads as \"unaffected\" to everyone who sees the table. These are the rows that are "
        "missing from every blast, every broadcast and every cell of the matrix.");
    ImGui::Spacing();
    for (const auto& path : missing) {
        ImGui::BulletText("%s", path.filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", path.string().c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled(
        "`mago init <folder>` -- which would write a starter manifest from what is on disk --\n"
        "is not built yet. Until it is, copy a sibling's void.json and edit it; the schema is\n"
        "at schema/void.schema.json.");
}

}  // namespace

void draw_registry(App& app) {
    if (ImGui::Button("Rescan")) app.reload();
    ImGui::SameLine();
    ImGui::Text("%zu projects with a manifest, %zu without", app.registry.projects().size(),
                app.registry.unmanifested().size());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Roots", ImGuiTreeNodeFlags_DefaultOpen)) draw_roots(app);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Projects", ImGuiTreeNodeFlags_DefaultOpen)) draw_projects(app);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("The gap", ImGuiTreeNodeFlags_DefaultOpen)) draw_gap(app);
}

}  // namespace magod
