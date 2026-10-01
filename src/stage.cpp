#include "mago/stage.hpp"

#include <algorithm>
#include <set>

#include "mago/imports.hpp"
#include "mago/json_out.hpp"

namespace fs = std::filesystem;

namespace mago {
namespace {

// Directories that never hold a shipped artifact, and are large enough that
// walking them is the difference between instant and not.
bool skip_directory(const std::string& name) {
    static const std::set<std::string> skip = {".git",   ".svn",         "node_modules",
                                               ".venv",  "__pycache__",  ".vs",
                                               ".idea",  "CMakeFiles",   "vendor",
                                               "Testing"};
    return skip.count(name) > 0 || (!name.empty() && name.front() == '.');
}

// A manifest names the file a RELEASE carries, which is not the name the build
// produced: Void Core builds `libvoidcore.dll` and ships it as
// `libvoidcore-win-x64.dll`. The platform tag is the difference, so strip it
// and look for both.
std::vector<std::string> candidate_names(const std::string& declared,
                                         const std::string& platform_tag) {
    std::vector<std::string> names{declared};
    if (platform_tag.empty()) return names;
    const std::string needle = "-" + platform_tag;
    if (const auto at = declared.find(needle); at != std::string::npos) {
        names.push_back(declared.substr(0, at) + declared.substr(at + needle.size()));
    }
    return names;
}

// `windows-x64` names a `-win-x64` artifact. The same small table resolve.cpp
// uses; kept in step by being this short.
std::string platform_tag(const std::string& platform) {
    if (platform == "windows-x64") return "win-x64";
    return platform;
}

// On Windows a manifest's `voidhormiga` is `voidhormiga.exe` on disk.
std::vector<std::string> executable_names(const std::string& declared,
                                          const std::string& platform) {
    std::vector<std::string> names{declared};
    if (platform.rfind("windows", 0) == 0 && declared.find('.') == std::string::npos) {
        names.insert(names.begin(), declared + ".exe");
    }
    return names;
}

// Every file matching one of `names` under `root`, bounded in depth. Bounded
// because an unbounded walk of somebody's checkout is a way to find a copy of
// the right name in a place that has nothing to do with the release.
void find_named(const fs::path& root, const std::vector<std::string>& names, int max_depth,
                std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;

    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) break;
        if (it.depth() >= max_depth) it.disable_recursion_pending();
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec)) {
            if (skip_directory(name)) it.disable_recursion_pending();
            continue;
        }
        if (std::find(names.begin(), names.end(), name) != names.end()) out.push_back(p);
    }
}

// Of several files with the right name, the one to ship. Shallower wins, then
// a path that looks like a build output, then lexicographic order so the
// answer never depends on the filesystem's mood.
void rank(std::vector<fs::path>& candidates, const fs::path& base) {
    const auto score = [&](const fs::path& p) {
        const std::string s = p.lexically_relative(base).generic_string();
        int depth = 0;
        for (char c : s) depth += (c == '/');
        const bool build_ish = s.find("build") != std::string::npos ||
                               s.find("bin") != std::string::npos ||
                               s.find("Release") != std::string::npos;
        return depth * 2 + (build_ish ? 0 : 1);
    };
    std::sort(candidates.begin(), candidates.end(), [&](const fs::path& a, const fs::path& b) {
        const int sa = score(a), sb = score(b);
        if (sa != sb) return sa < sb;
        return a.generic_string() < b.generic_string();
    });
}

std::uintmax_t folder_bytes(const fs::path& dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    }
    return total;
}

}  // namespace

std::vector<fs::path> search_roots(const Manifest& m) {
    // The checkout itself, walked with a depth bound. Listing likely build
    // directories explicitly was the first design and it was wrong: Void Core
    // builds into `core/build/bin`, Void Maiz into `build`, and enumerating
    // everybody's layout here would make Mago's file the place a project's
    // build directory is recorded -- which is precisely the drift this whole
    // tool exists to catch.
    return {m.path};
}

std::size_t StageReport::missing() const {
    return static_cast<std::size_t>(
        std::count_if(files.begin(), files.end(),
                      [](const StagedFile& f) { return !f.problem.empty(); }));
}

std::uintmax_t StageReport::bytes() const {
    std::uintmax_t total = 0;
    for (const auto& f : files) total += f.bytes;
    return total;
}

std::string StageReport::to_json(int indent) const {
    JsonWriter w(indent);
    w.begin_object();
    w.field("report", "mago-stage/0.1");
    w.field("application", application);
    w.key("version").value(version.str());
    w.field("platform", platform);
    w.field("root", root.generic_string());
    w.key("complete").value(complete());
    w.key("bytes").value(static_cast<long long>(bytes()));
    w.key("files").begin_array();
    for (const auto& f : files) {
        w.begin_object();
        w.field("project", f.project);
        w.field("declared", f.declared);
        w.field("source", f.source.generic_string());
        w.field("destination", f.destination.generic_string());
        w.key("folder").value(f.is_folder);
        w.key("bytes").value(static_cast<long long>(f.bytes));
        w.field("problem", f.problem);
        w.key("also_found").begin_array();
        for (const auto& other : f.also_found) w.value(other.generic_string());
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.key("unresolved_imports").begin_array();
    for (const auto& u : unresolved) {
        w.begin_object();
        w.field("binary", u.binary);
        w.field("dll", u.dll);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

StageReport stage(const Registry& reg, const Plan& plan, const StageOptions& opt) {
    StageReport report;
    report.root = opt.out;
    report.application = plan.application;
    report.version = plan.version;
    report.platform = plan.platform;

    std::error_code ec;
    if (!opt.dry_run) {
        if (opt.clean) fs::remove_all(opt.out, ec);
        fs::create_directories(opt.out, ec);
    }

    const std::string tag = platform_tag(plan.platform);

    for (const auto& entry : plan.payload) {
        const Manifest* m = reg.get(entry.name);
        if (!m) continue;

        for (const auto& declared : entry.files) {
            StagedFile staged;
            staged.project = entry.name;
            staged.declared = declared;
            staged.is_folder = !declared.empty() && declared.back() == '/';

            const std::string bare =
                staged.is_folder ? declared.substr(0, declared.size() - 1) : declared;

            // A folder or a plain document is a path relative to the checkout;
            // an executable or an artifact is a file to be found in whatever
            // the project calls its build directory.
            const fs::path direct = m->path / bare;
            std::vector<fs::path> candidates;
            if (fs::exists(direct, ec)) {
                candidates.push_back(direct);
            } else if (!staged.is_folder) {
                const std::vector<std::string> names =
                    entry.role == Role::Root ? executable_names(bare, plan.platform)
                                             : candidate_names(bare, tag);
                for (const auto& root : search_roots(*m)) {
                    find_named(root, names, /*max_depth=*/5, candidates);
                }
                rank(candidates, m->path);
            }

            if (candidates.empty()) {
                staged.problem = m->display_name + " declares \"" + declared +
                                 "\", which is not in " + m->path.string();
                report.files.push_back(std::move(staged));
                continue;
            }

            staged.source = candidates.front();
            staged.also_found.assign(candidates.begin() + 1, candidates.end());

            // The emitter writes `<source_root>\<project>\<file>`, so the
            // layout here is not a choice -- it is the other half of a
            // contract emit_nsis.cpp already wrote down.
            // The name it lands under. A folder keeps its declared path; the
            // application's own executables keep the name the build gave them
            // (`voidhormiga` is `voidhormiga.exe` on disk, and an installer
            // that dropped the extension would produce a file Windows will not
            // run); everything else loses its platform tag, because that tag
            // exists to keep platforms apart on a release page and means
            // nothing once the file is on one machine.
            const fs::path destination_dir = opt.out / entry.name;
            staged.destination =
                destination_dir /
                (staged.is_folder      ? fs::path(bare)
                 : entry.role == Role::Root ? staged.source.filename()
                                            : fs::path(installed_name(bare, plan.platform)));

            if (staged.is_folder) {
                staged.bytes = folder_bytes(staged.source);
            } else {
                staged.bytes = fs::file_size(staged.source, ec);
                if (ec) staged.bytes = 0;
            }

            if (!opt.dry_run) {
                fs::create_directories(staged.destination.parent_path(), ec);
                if (staged.is_folder) {
                    fs::copy(staged.source, staged.destination,
                             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                             ec);
                } else {
                    fs::copy_file(staged.source, staged.destination,
                                  fs::copy_options::overwrite_existing, ec);
                }
                if (ec) staged.problem = "could not copy: " + ec.message();
            }
            report.files.push_back(std::move(staged));
        }
    }

    // WHAT THE BINARIES ACTUALLY ASK FOR.
    //
    // The manifests said what belongs in the package; now ask the executables
    // what they cannot start without, and report anything the payload does not
    // contain and Windows does not provide. A dry run skips it -- there is
    // nothing staged to read.
    if (!opt.dry_run) {
        std::set<std::string> present;
        for (const auto& f : report.files) {
            if (f.problem.empty() && !f.is_folder) {
                std::string name = f.destination.filename().string();
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                present.insert(name);
            }
        }
        for (const auto& f : report.files) {
            if (!f.problem.empty() || f.is_folder) continue;
            const std::string ext = f.destination.extension().string();
            if (ext != ".exe" && ext != ".dll") continue;
            for (const auto& dll : pe_imports(f.destination)) {
                std::string key = dll;
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (present.count(key) || is_system_library(dll)) continue;
                report.unresolved.push_back(
                    {f.destination.filename().string(), dll});
            }
        }
    }
    return report;
}

}  // namespace mago
