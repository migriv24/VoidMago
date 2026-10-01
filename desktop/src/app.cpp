#include "app.hpp"

#include <algorithm>

namespace fs = std::filesystem;

namespace magod {
namespace {

// The family graph as a Void Core mantle: one rune per project, one link per
// pair. Colours by kind, so the picture answers "what is a runtime" before you
// read a single label.
//
// Nothing is decided here -- every glyph, colour and edge is a field the
// matrix already carries. A different projection of the same document would be
// a differently-shaped picture of identical facts, which is what makes this
// rendering rather than logic.
const char* glyph_for(const std::string& kind) {
    if (kind == "runtime") return "runtime";
    if (kind == "capability") return "capability";
    return "application";
}

std::string quote(const std::string& s) {
    // Void Core SPEC 6.1 quoting is the core's own job, and the only values
    // that reach it here are project names and version strings. Single quotes
    // keep a space-free token intact and are what the CLI examples use.
    return "'" + s + "'";
}

}  // namespace

void App::say(const std::string& level, const std::string& text) {
    log.push_back({level, text});
    if (log.size() > 400) log.erase(log.begin(), log.begin() + 100);
}

void App::reload() {
    errors = warnings = notes = 0;
    try {
        home = mago::Home::load();
        std::vector<fs::path> roots = home.paths();
        if (roots.empty()) roots.push_back(fs::current_path().parent_path());
        registry = mago::Registry::scan(roots);

        family_path = registry.get("voidmago")
                          ? mago::Family::default_path(registry.at("voidmago").path)
                          : fs::path(mago::kFamilyName);
        family = mago::Family::load(family_path);

        matrix = mago::Matrix::build(registry, family);
        findings = registry.check(&family);
        for (const auto& f : findings) {
            if (f.level == "error") ++errors;
            else if (f.level == "warn") ++warnings;
            else ++notes;
        }
        if (release_project.empty() && registry.contains("voidcore")) release_project = "voidcore";
        if (release_project.empty() && !registry.projects().empty()) {
            release_project = registry.projects().begin()->first;
        }
        say("ok", "read " + std::to_string(registry.projects().size()) + " manifests, " +
                      std::to_string(matrix.bonded()) + " bonded pairs, " +
                      std::to_string(findings.size()) + " findings");
        project_graph();
    } catch (const std::exception& e) {
        say("error", e.what());
    }
}

void App::project_graph() {
    // A fresh manager every time: the mantle is a picture of the documents, so
    // rebuilding it is cheaper and more honest than reconciling it.
    core = maiz::Core{};
    core.dispatch("config set actor human:voidmago-desktop");

    // SHAPE CARRIES THE KIND, and colour only repeats it. A runtime is a
    // hexagon because it is a substrate that everything sits on; a capability
    // is a circle because it has no orientation -- you develop with it, you do
    // not install it; an application is the ordinary windowed body, because it
    // is the only one of the three a person ever sees by name.
    //
    // Shape is a second channel deliberately: colour must never be the only
    // signal (Void Maiz makes the same argument for its port markers). Every
    // one of these is a `hints` key Void Maiz already reads -- Desktop draws
    // no geometry of its own.
    const char* runtime_shape = shapes_by_kind ? R"("shape":{"kind":"polygon","sides":6},)" : "";
    const char* capability_shape = shapes_by_kind ? R"("shape":{"kind":"circle"},)" : "";

    core.register_glyph(
        std::string(
            R"({"glyph":"runtime","label":"Runtime","editor":"form","fields":["version","status"],)"
            R"("hints":{)") +
        runtime_shape + R"("color":"#3f8f5f","face":{"w":150,"h":110}}})");
    core.register_glyph(
        std::string(
            R"({"glyph":"capability","label":"Capability","editor":"form","fields":["version","status"],)"
            R"("hints":{)") +
        capability_shape + R"("color":"#3f6f9f","face":{"w":140,"h":140}}})");
    core.register_glyph(
        R"({"glyph":"application","label":"Application","editor":"form","fields":["version","status"],)"
        R"("hints":{"color":"#8f5f3f","face":{"w":180,"h":78}}})");

    core.dispatch("mantle new family");

    for (const auto& d : matrix.diagonal()) {
        const maiz::Result r =
            core.dispatch("rune new " + std::string(glyph_for(d.kind)) + " " + d.name);
        if (!r.ok) {
            say("error", "rune new " + d.name + ": " + r.text());
            continue;
        }
        core.dispatch("set " + d.name + " version " + quote(d.version.str()));
        core.dispatch("set " + d.name + " status " + quote(d.status.empty() ? "-" : d.status));
    }

    // One link per bonded pair. The edge is drawn from whoever depends on
    // whoever they depend on; a seam has no direction, so it is drawn in
    // canonical order and means neither.
    int wired = 0;
    for (const auto& p : matrix.pairings()) {
        if (p.bond == mago::Bond::None) continue;
        const std::string from = p.direction >= 0 ? p.a : p.b;
        const std::string to = p.direction >= 0 ? p.b : p.a;
        const maiz::Result r = core.dispatch("link " + from + " " + to + " --weight " +
                                             std::to_string(static_cast<int>(p.cost) + 1));
        if (r.ok) {
            ++wired;
        } else {
            say("error", "link " + from + " " + to + ": " + r.text());
        }
    }

    scene = maiz::project_scene(core);
    graph_built = true;
    say("ok", "projected " + std::to_string(matrix.size()) + " runes and " +
                  std::to_string(wired) + " links into the family mantle");
}

const mago::Manifest* App::selected_project() const {
    if (!has_selection || sel_row >= matrix.size()) return nullptr;
    return registry.get(matrix.names()[sel_row]);
}

mago::Cell App::selected_cell() const {
    if (!has_selection) return {};
    return matrix.at(sel_row, sel_col);
}

}  // namespace magod
