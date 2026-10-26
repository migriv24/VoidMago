#include "mago/resolve.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <set>

#include "mago/json_out.hpp"

namespace mago {
namespace {

std::string now_iso8601() {
    const auto now = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}+00:00", now);
}

// `windows-x64` names a `-win-x64` artifact. Small, explicit table.
std::string platform_tag(const std::string& platform) {
    if (platform == "windows-x64") return "win-x64";
    return platform;
}

}  // namespace

std::string installed_name(const std::string& declared, const std::string& platform) {
    const std::string tag = platform_tag(platform);
    if (tag.empty()) return declared;
    const std::string needle = "-" + tag;
    const auto at = declared.find(needle);
    if (at == std::string::npos) return declared;
    return declared.substr(0, at) + declared.substr(at + needle.size());
}

namespace {

std::string join(const std::vector<std::string>& xs, const char* sep = ", ") {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

std::vector<std::string> sorted_unique(std::vector<std::string> xs) {
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    return xs;
}

// The files this project contributes to somebody else's install folder.
std::vector<std::string> files_for(const Manifest& m, const Requirement* req,
                                   const std::string& platform) {
    std::vector<std::string> files;

    if (!req) {  // the application itself
        files = m.executables();
        files.insert(files.end(), m.ships_beside_binary.begin(), m.ships_beside_binary.end());
        return files;
    }
    if (req->link != "shared") return files;

    std::vector<std::string> artifacts = m.artifacts;
    if (!platform.empty()) {
        const std::string tag = platform_tag(platform);
        std::vector<std::string> matched;
        for (const auto& a : artifacts) {
            if (a.find(tag) != std::string::npos) matched.push_back(a);
        }
        if (!matched.empty()) artifacts = std::move(matched);
    }
    files = std::move(artifacts);
    files.insert(files.end(), m.ships_beside_binary.begin(), m.ships_beside_binary.end());
    return files;
}

// Report where one consumer's floor silently raises another's.
//
// Void Hormiga asks for Void Core >=0.2.7. Void Maiz, which Hormiga also uses,
// asks for >=0.2.12. The install has to satisfy both, so Hormiga's real floor
// is 0.2.12 -- and nothing in Hormiga's own repository says so.
std::vector<std::string> tighten(const std::vector<Resolved>& build_order) {
    std::vector<std::string> notes;
    for (const auto& r : build_order) {
        if (r.constraints.size() < 2) continue;
        const Range binding = r.effective_floor();
        if (!binding.floor()) continue;

        std::vector<std::string> looser;
        for (const auto& c : r.constraints) {
            if (c.text() != binding.text() && c.text() != "*") looser.push_back(c.text());
        }
        looser = sorted_unique(std::move(looser));
        if (looser.empty()) continue;

        notes.push_back(r.name + ": floor is " + binding.text() + " (from " +
                        join(sorted_unique(r.required_by)) + "), which raises the looser " +
                        join(looser) + " declared elsewhere");
    }
    return notes;
}

void write_resolved(JsonWriter& w, const Resolved& r) {
    w.begin_object();
    w.field("name", r.name);
    w.field("display_name", r.display_name);
    w.field("kind", r.kind);
    w.key("version").value(r.version.str());
    w.field("role", role_name(r.role));
    w.key("constraints").begin_array();
    for (const auto& c : r.constraints) w.value(c.text());
    w.end_array();
    w.string_array("required_by", r.required_by);
    w.field("link", r.link);
    w.field("via", r.via);
    w.string_array("files", r.files);
    w.field("effective_constraint", r.effective_floor().text());
    w.end_object();
}

}  // namespace

const char* role_name(Role r) {
    switch (r) {
        case Role::Payload: return "payload";
        case Role::Absorbed: return "absorbed";
        case Role::Root: return "root";
    }
    return "?";
}

std::string Plan::install_dir_name() const {
    return application + "-" + version.str();
}

std::string Plan::to_json(int indent) const {
    JsonWriter w(indent);
    write_json(w);
    return w.str();
}

void Plan::write_json(JsonWriter& w) const {
    w.begin_object();
    w.field("plan", "mago/0.1");
    w.field("generated", generated);
    w.field("application", application);
    w.field("display_name", display_name);
    w.field("description", description);
    w.key("version").value(version.str());
    w.field("platform", platform);
    w.field("install_dir", install_dir_name());

    w.key("build_order").begin_array();
    for (const auto& r : build_order) w.value(r.name);
    w.end_array();

    w.key("payload").begin_array();
    for (const auto& r : payload) write_resolved(w, r);
    w.end_array();

    w.key("absorbed").begin_array();
    for (const auto& r : absorbed) write_resolved(w, r);
    w.end_array();

    w.string_array("notes", notes);
    w.end_object();
}

Plan resolve(const Registry& reg, const std::string& app_name, const std::string& platform) {
    const Manifest* root = reg.get(app_name);
    if (!root) {
        std::vector<std::string> known;
        for (const auto* m : reg.applications()) known.push_back(m->name);
        throw ResolveError("no manifest for \"" + app_name + "\". Known applications: " +
                           (known.empty() ? "none" : join(sorted_unique(known))));
    }
    if (!root->is_installable()) {
        throw ResolveError(app_name + " is a " + root->kind +
                           ", not an application. Runtimes ship inside an application's "
                           "release; they are not installed by name.");
    }
    if (!platform.empty() && !root->platforms.empty() &&
        std::find(root->platforms.begin(), root->platforms.end(), platform) ==
            root->platforms.end()) {
        throw ResolveError(root->display_name + " does not declare " + platform +
                           "; it builds for " + join(root->platforms));
    }

    std::map<std::string, Resolved> resolved;
    std::vector<std::string> order;

    // Recursion depth is bounded by the dependency chain, which `mago doctor`
    // reports a cycle in; the trail below also refuses one outright.
    const auto visit = [&](auto&& self, const Manifest& m,
                           std::vector<std::string> trail) -> void {
        for (const auto& req : m.requirements) {
            const Manifest* dep = reg.get(req.name);
            if (!dep && req.optional) continue;  // the consumer builds without it
            if (!dep) {
                throw ResolveError(m.name + " requires " + req.name +
                                   ", which has no manifest. Add a void.json to it, or Mago "
                                   "cannot promise this install is complete.");
            }
            if (!req.version.allows(dep->version)) {
                throw ResolveError(m.name + " requires " + req.name + " " + req.version.text() +
                                   ", but " + req.name + " is at " + dep->version.str() +
                                   ". Nothing here can be installed honestly.");
            }
            if (std::find(trail.begin(), trail.end(), req.name) != trail.end()) {
                trail.push_back(req.name);
                throw ResolveError("dependency cycle: " + join(trail, " -> "));
            }

            auto it = resolved.find(req.name);
            if (it == resolved.end()) {
                Resolved entry;
                entry.name = dep->name;
                entry.display_name = dep->display_name;
                entry.kind = dep->kind;
                entry.version = dep->version;
                entry.role = req.is_installed_artifact() ? Role::Payload : Role::Absorbed;
                entry.link = req.link;
                entry.via = req.via;
                entry.files = files_for(*dep, &req, platform);
                it = resolved.emplace(req.name, std::move(entry)).first;

                auto next = trail;
                next.push_back(req.name);
                self(self, *dep, std::move(next));
                order.push_back(req.name);
            } else if (req.is_installed_artifact() && it->second.role == Role::Absorbed) {
                // One consumer links it statically, another takes the shared
                // library. The shared one wins: the file has to be there.
                it->second.role = Role::Payload;
                it->second.link = req.link;
                it->second.files = files_for(*dep, &req, platform);
            }

            it->second.constraints.push_back(req.version);
            it->second.required_by.push_back(m.name);
        }
    };

    visit(visit, *root, {root->name});

    Resolved root_entry;
    root_entry.name = root->name;
    root_entry.display_name = root->display_name;
    root_entry.kind = root->kind;
    root_entry.version = root->version;
    root_entry.role = Role::Root;
    root_entry.files = files_for(*root, nullptr, platform);
    resolved.emplace(root->name, std::move(root_entry));
    order.push_back(root->name);

    Plan plan;
    plan.application = root->name;
    plan.display_name = root->display_name;
    plan.description = root->description;
    plan.version = root->version;
    plan.platform = !platform.empty()          ? platform
                    : !root->platforms.empty() ? root->platforms.front()
                                               : std::string();
    plan.generated = now_iso8601();

    for (const auto& name : order) plan.build_order.push_back(resolved.at(name));
    for (const auto& r : plan.build_order) {
        if (r.role == Role::Absorbed) {
            plan.absorbed.push_back(r);
        } else {
            plan.payload.push_back(r);
        }
    }
    plan.notes = tighten(plan.build_order);
    return plan;
}

}  // namespace mago
