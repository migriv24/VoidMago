#include "mago/registry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "mago/vendors.hpp"
#include "mago/versions.hpp"

namespace fs = std::filesystem;

namespace mago {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Are two paths the same folder? Canonical where possible, so
// `app/../VoidHammer` and `VoidHammer/` agree; lexical when that fails.
bool same_dir(const fs::path& a, const fs::path& b) {
    std::error_code ec1, ec2;
    const fs::path ca = fs::weakly_canonical(a, ec1);
    const fs::path cb = fs::weakly_canonical(b, ec2);
    if (ec1 || ec2) return a.lexically_normal() == b.lexically_normal();
    return ca == cb;
}

// Does a project's top-level CMakeLists.txt reach into `../<folder>`? This is
// the edge no manifest records: Void Maiz's CMake adds ../VoidHammer/net as a
// subdirectory, and its void.json names no Void Hammer. Only the top-level
// file is read; a sibling is reached from the root in every Void repository,
// and walking the tree would read vendored CMake that is nobody's edge.
bool cmake_reaches(const fs::path& project, const std::string& folder) {
    std::ifstream in(project / "CMakeLists.txt", std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const std::string needle = "../" + folder;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1)) {
        const std::size_t end = at + needle.size();
        // `../VoidHammer` must not match `../VoidHammerTools`.
        if (end == text.size()) return true;
        const unsigned char next = static_cast<unsigned char>(text[end]);
        if (!std::isalnum(next) && next != '_' && next != '-') return true;
    }
    return false;
}

// "Void Buzz" and "VoidBicho" both look like Void projects.
bool looks_like_void_project(const std::string& dir_name) {
    std::string squashed;
    for (char c : dir_name) {
        if (!std::isspace(static_cast<unsigned char>(c))) squashed += c;
    }
    return lower(squashed).rfind("void", 0) == 0;
}

}  // namespace

Registry Registry::scan(const std::vector<fs::path>& roots) {
    Registry merged;
    for (const auto& root : roots) {
        Registry one = scan(root);
        merged.roots_.push_back(one.roots_.empty() ? root : one.roots_.front());
        for (auto& [name, m] : one.projects_) {
            if (auto found = merged.projects_.find(name); found != merged.projects_.end()) {
                merged.load_errors_.push_back({"error", name,
                                               "declared twice: " + found->second.path.string() +
                                                   " and " + m.path.string()});
                continue;
            }
            merged.projects_.emplace(name, std::move(m));
        }
        for (auto& p : one.unmanifested_) merged.unmanifested_.push_back(std::move(p));
        for (auto& f : one.load_errors_) merged.load_errors_.push_back(std::move(f));
    }
    return merged;
}

Registry Registry::scan(const fs::path& root_in) {
    Registry reg;
    std::error_code ec;
    const fs::path root = fs::absolute(root_in, ec);
    reg.roots_.push_back(root);

    std::vector<fs::path> children;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) children.push_back(it->path());
    }
    std::sort(children.begin(), children.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });

    for (const auto& child : children) {
        const fs::path manifest_path = child / kManifestName;
        if (!fs::exists(manifest_path, ec)) {
            if (looks_like_void_project(child.filename().string())) {
                reg.unmanifested_.push_back(child);
            }
            continue;
        }
        try {
            Manifest m = load(manifest_path);
            if (auto found = reg.projects_.find(m.name); found != reg.projects_.end()) {
                reg.load_errors_.push_back({"error", m.name,
                                            "declared twice: " + found->second.path.string() +
                                                " and " + m.path.string()});
                continue;
            }
            const std::string name = m.name;
            reg.projects_.emplace(name, std::move(m));
        } catch (const ManifestError& e) {
            reg.load_errors_.push_back({"error", child.filename().string(), e.what()});
        }
    }
    return reg;
}

GitRemote git_remote_state(const fs::path& dir) {
    std::error_code ec;
    const fs::path git = dir / ".git";
    if (!fs::exists(git, ec)) return GitRemote::NotARepository;
    // A `.git` FILE is a worktree or a submodule: its config is elsewhere,
    // and a guess about it would be a guess.
    if (!fs::is_directory(git, ec)) return GitRemote::Unknown;
    std::ifstream in(git / "config", std::ios::binary);
    if (!in) return GitRemote::Unknown;
    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "[remote ") == 0) {
            return GitRemote::Some;
        }
    }
    return GitRemote::None;
}

// The 2026-10-18 ask, built 2026-11-02: a folder the family BUILDS FROM is not
// the same gap as a folder that merely looks like a Void project. Void Hammer
// was both for two weeks -- Hormiga's networking compiled out of it through
// Void Maiz's CMake -- and doctor said only that it "looks like a Void
// project". The finding now names who builds from it, and whether any other
// machine could.
void Registry::check_unmanifested(std::vector<Finding>& out) const {
    for (const auto& dir : unmanifested_) {
        const std::string folder = dir.filename().string();
        std::vector<std::string> reachers;
        for (const auto& [name, m] : projects_) {
            bool by_requires = false;
            for (const auto& req : m.requirements) {
                if (!req.sibling.empty() && same_dir(m.path / req.sibling, dir)) {
                    by_requires = true;
                    break;
                }
            }
            if (by_requires) {
                reachers.push_back(name + " (requires it)");
            } else if (cmake_reaches(m.path, folder)) {
                reachers.push_back(name + " (its CMakeLists.txt reaches ../" + folder +
                                   ", and its manifest does not say so)");
            }
        }

        if (reachers.empty()) {
            out.push_back({"note", folder,
                           std::string("looks like a Void project but carries no ") + kManifestName});
            continue;
        }

        std::string who;
        for (const auto& r : reachers) {
            if (!who.empty()) who += ", ";
            who += r;
        }
        std::string message = std::string("carries no ") + kManifestName + ", and " + who +
                              (reachers.size() == 1 ? " builds" : " build") +
                              " from it: a change there reaches them with no verdict";
        switch (git_remote_state(dir)) {
            case GitRemote::None:
                message += ". It has no git remote either, so no machine without this folder can build them";
                break;
            case GitRemote::NotARepository:
                message += ". It is not a git repository either, so no machine without this folder can build them";
                break;
            case GitRemote::Some:
            case GitRemote::Unknown:
                break;
        }
        out.push_back({"warn", folder, message});
    }
}

const Manifest* Registry::get(const std::string& name) const {
    const auto it = projects_.find(name);
    return it == projects_.end() ? nullptr : &it->second;
}

const Manifest& Registry::at(const std::string& name) const {
    const Manifest* m = get(name);
    if (!m) throw ManifestError("no manifest for \"" + name + "\"");
    return *m;
}

std::vector<const Manifest*> Registry::applications() const {
    std::vector<const Manifest*> out;
    for (const auto& [_, m] : projects_) {
        if (m.is_installable()) out.push_back(&m);
    }
    return out;
}

std::vector<const Manifest*> Registry::dependents_of(const std::string& name) const {
    std::vector<const Manifest*> out;
    for (const auto& [_, m] : projects_) {
        if (m.requirement(name)) out.push_back(&m);
    }
    return out;
}

bool Registry::linked(const std::string& a, const std::string& b) const {
    const Manifest* ma = get(a);
    const Manifest* mb = get(b);
    return (ma && ma->requirement(b)) || (mb && mb->requirement(a));
}

std::vector<std::pair<const Manifest*, std::string>> Registry::seam_neighbours(
    const std::string& name) const {
    std::map<std::string, std::string> found;  // ordered, so output is stable
    const Manifest* me = get(name);

    // The manifest seam belongs to whoever DEFINES the manifest. Every project
    // in the family names it, because Mago reads every project -- and if that
    // counted as a contract each of them could break, a Void Core patch would
    // report a retest for Void Mago, which reads a JSON file and is not
    // affected by anything in it.
    const auto seam_is_ours = [&](const Interaction& i) {
        return i.seam != kManifestName || (me && me->provides_document(kManifestName));
    };

    if (me) {
        for (const auto& i : me->interacts_with) {
            if (i.name == "*" || !seam_is_ours(i)) continue;
            if (contains(i.name)) found.emplace(i.name, i.seam);
        }
    }
    for (const auto& [other_name, other] : projects_) {
        if (other_name == name) continue;
        for (const auto& i : other.interacts_with) {
            if (i.name == name && seam_is_ours(i)) found.emplace(other_name, i.seam);
        }
    }

    std::vector<std::pair<const Manifest*, std::string>> out;
    for (const auto& [n, seam] : found) {
        if (!linked(name, n)) out.emplace_back(get(n), seam);
    }
    return out;
}

void Registry::check_reciprocity(const Manifest& m, const Family* family,
                                 std::vector<Finding>& out) const {
    for (const auto& i : m.interacts_with) {
        if (i.name == "*") continue;
        const Manifest* other = get(i.name);
        if (!other) {
            // Not an error -- the sibling may simply not be checked out. It is
            // still worth saying, because a partner Mago cannot read is a
            // verdict Mago cannot compute, and an absent row reads like
            // "unaffected" to everyone who sees the table.
            out.push_back({"note", m.name,
                           "names " + i.name + " at the " +
                               (i.seam.empty() ? "undeclared" : i.seam) +
                               " seam, and no manifest for " + i.name +
                               " is in this registry; its verdict cannot be computed"});
            continue;
        }

        // `*` means every Void project, so a wildcard entry on the other side
        // already mentions us and there is nothing one-sided about it.
        const Interaction* theirs = nullptr;
        for (const auto& x : other->interacts_with) {
            if (x.name == m.name || x.name == "*") {
                theirs = &x;
                break;
            }
        }
        const bool they_require_us = other->requirement(m.name) != nullptr;

        if (!theirs && !they_require_us) {
            out.push_back({"note", m.name,
                           "records an interaction with " + i.name + ", which does not mention " +
                               m.name + " at all"});
        } else if (linked(m.name, i.name)) {
            out.push_back({"note", m.name,
                           "records " + i.name +
                               " under interacts_with, but the two also have a requires edge; "
                               "one relationship described in two places can drift, and Mago "
                               "counts only the requires"});
        } else if (theirs && !theirs->seam.empty() && !i.seam.empty() && theirs->seam != i.seam) {
            std::string message = "calls the " + i.name + " seam \"" + i.seam + "\", but " +
                                  i.name + " calls it \"" + theirs->seam +
                                  "\" -- one of them is wrong";
            const AuthoredPairing* settled = family ? family->pairing(m.name, i.name) : nullptr;
            if (settled && !settled->seam.empty()) {
                message += "; " + std::string(kFamilyName) + " settles it as \"" +
                           settled->seam + "\", so the fix is known";
            }
            out.push_back({"error", m.name, message});
        }
    }
}

// Does the repository agree with its own manifest about what version it is?
//
// Every verdict `mago blast` prints is arithmetic over this number. A stale
// one does not produce a missing answer; it produces a confident wrong one,
// which is why this is an error rather than a note.
void Registry::check_versions(const Manifest& m, std::vector<Finding>& out) const {
    std::vector<std::string> wanted =
        m.version_sources_declared ? m.version_sources : default_version_sources();
    if (wanted.empty()) return;  // the project said so explicitly

    for (const auto& claim : version_claims(m.path)) {
        if (std::find(wanted.begin(), wanted.end(), claim.file) == wanted.end()) continue;
        if (!claim.version) {
            out.push_back({"note", m.name,
                           claim.file + " states a version Mago cannot read: " + claim.where});
            continue;
        }
        if (*claim.version == m.version) continue;
        out.push_back({"error", m.name,
                       std::string(kManifestName) + " says " + m.version.str() + ", but " +
                           claim.file + " says " + claim.version->str() +
                           " -- every verdict about " + m.name + " is arithmetic over that "
                           "number, so one of the two makes them all wrong"});
    }

    // A declared source that is not there is a check nobody is running.
    if (m.version_sources_declared) {
        std::error_code ec;
        for (const auto& file : wanted) {
            if (!std::filesystem::exists(m.path / file, ec)) {
                out.push_back({"warn", m.name,
                               "declares version_sources[\"" + file +
                                   "\"], which does not exist in the repository"});
            }
        }
    }
}

// Third-party code the project uses and does not own.
//
// Everything here is about a promise the manifest makes about a file: that it
// is there, that it is the version claimed, and that it carries a licence
// somebody could put in a notices file.
void Registry::check_vendors(const Manifest& m, std::vector<Finding>& out) const {
    std::error_code ec;
    for (const auto& v : m.vendors) {
        if (!v.path.empty() && !std::filesystem::exists(m.path / v.path, ec)) {
            out.push_back({"error", m.name,
                           "vendors " + v.name + " at \"" + v.path +
                               "\", which is not in the repository"});
        }

        if (v.version.empty()) {
            out.push_back({"note", m.name,
                           "vendors " + v.name +
                               " with no version, so nothing can tell whether it is the same "
                               "copy anybody else has"});
        } else if (!v.version_source.empty()) {
            const std::filesystem::path source = m.path / v.version_source;
            if (!std::filesystem::exists(source, ec)) {
                out.push_back({"warn", m.name,
                               "vendors " + v.name + " with version_source \"" +
                                   v.version_source + "\", which does not exist"});
            } else if (!version_appears_in(source, v.version)) {
                out.push_back({"error", m.name,
                               "declares " + v.name + " " + v.version + ", but " +
                                   v.version_source +
                                   " does not carry that version -- the vendored copy and the "
                                   "manifest disagree about what was vendored"});
            }
        }

        if (v.license.empty()) {
            out.push_back({"warn", m.name,
                           "vendors " + v.name +
                               " with no license, so it cannot appear in a notices file"});
        }

        // The category that broke Void Hormiga's first package: a file from
        // the compiler's own directory, present on one machine in the world.
        if (v.link == "toolchain") {
            out.push_back({"note", m.name,
                           "vendors " + v.name +
                               " from the toolchain; link it statically or ship it "
                               "deliberately, because it is on the build machine and nowhere "
                               "else"});
        }
    }
}

// The release block describes an event, so it is the one part of a manifest
// that can be internally inconsistent rather than merely wrong.
void Registry::check_release(const Manifest& m, std::vector<Finding>& out) const {
    if (!m.release.declared) return;

    if (m.release.version != m.version) {
        out.push_back({"error", m.name,
                       "release.version is " + m.release.version.str() +
                           " but the manifest declares " + m.version.str() +
                           "; a release block describes the version beside it, not another one"});
    }
    if (m.release.previous && !(*m.release.previous < m.release.version)) {
        out.push_back({"error", m.name,
                       "release.previous is " + m.release.previous->str() +
                           ", which is not before release.version " + m.release.version.str()});
    }
    if (!m.release.previous) {
        out.push_back({"note", m.name,
                       "declares a release without release.previous, so `mago blast " + m.name +
                           "` still needs --to; the version it moved FROM is the other half of "
                           "the fact"});
    }
    if (m.release.change.empty()) {
        out.push_back({"note", m.name,
                       "declares a release without release.change, so the change kind is "
                       "inferred from the version numbers rather than stated -- which is the "
                       "one bit of judgement no manifest can derive"});
    }
}

void Registry::check_cycles(std::vector<Finding>& out) const {
    enum class Colour { White, Grey, Black };
    std::map<std::string, Colour> colour;

    // Explicit stack rather than recursion: a manifest is untrusted input, and
    // a deep chain should not be able to blow the stack of the tool that is
    // supposed to diagnose it.
    struct Frame {
        std::string name;
        std::size_t next = 0;
    };

    for (const auto& [start, _] : projects_) {
        if (colour[start] != Colour::White) continue;
        std::vector<Frame> stack{{start, 0}};
        std::vector<std::string> trail{start};
        colour[start] = Colour::Grey;

        while (!stack.empty()) {
            Frame& f = stack.back();
            const auto& reqs = at(f.name).requirements;
            if (f.next >= reqs.size()) {
                colour[f.name] = Colour::Black;
                stack.pop_back();
                trail.pop_back();
                continue;
            }
            const std::string dep = reqs[f.next++].name;
            if (!contains(dep)) continue;

            if (colour[dep] == Colour::Grey) {
                const auto at_dep = std::find(trail.begin(), trail.end(), dep);
                std::string cycle;
                for (auto it = at_dep; it != trail.end(); ++it) cycle += *it + " -> ";
                cycle += dep;
                out.push_back({"error", f.name, "dependency cycle: " + cycle});
            } else if (colour[dep] == Colour::White) {
                colour[dep] = Colour::Grey;
                trail.push_back(dep);
                stack.push_back({dep, 0});
            }
        }
    }
}

namespace {
// A repository URL compared by what it names, not how it is spelled.
std::string same_repo(std::string u) {
    for (auto& c : u) c = (char)std::tolower((unsigned char)c);
    while (!u.empty() && u.back() == '/') u.pop_back();
    if (u.size() > 4 && u.compare(u.size() - 4, 4, ".git") == 0) u.resize(u.size() - 4);
    return u;
}
}  // namespace

std::vector<Finding> Registry::check(const Family* family) const {
    std::vector<Finding> out = load_errors_;

    /* WHO AN APPLICATION IS (okf/concepts/audiences.md, 2026-10-25). A fork is
     * welcome; a fork wearing upstream's identity is an impostor update to
     * every phone and installer that has the original. */
    for (const auto& [_, m] : projects_) {
        const auto& id = m.identity;
        const std::string repo = same_repo(m.repository);
        if (!id.belongs_to.empty() && !repo.empty() && same_repo(id.belongs_to) != repo) {
            out.push_back({"warn", m.name,
                           "ships the identity of " + id.belongs_to +
                               (id.app_id.empty() ? "" : " (" + id.app_id + ")") + " from " + m.repository +
                               ": a fork takes its own app_id, publisher and feed, or it presents itself as "
                               "an update to the original"});
        }
        if (!m.fork_of.empty() && !id.belongs_to.empty() && same_repo(m.fork_of) == same_repo(id.belongs_to)) {
            out.push_back({"warn", m.name,
                           "is a fork of " + m.fork_of + " but keeps that project's identity: give it its own"});
        }
        if (m.is_installable() && !m.platforms.empty() && id.app_id.empty()) {
            out.push_back({"note", m.name,
                           "ships to " + std::to_string(m.platforms.size()) +
                               " platform(s) and states no identity.app_id: a store and an OS know an "
                               "application by an id this manifest does not say"});
        }
        const std::pair<const char*, std::string> files[] = {{"guide", m.contribute.guide},
                                                              {"security", m.contribute.security}};
        for (const auto& [what, rel] : files) {
            std::error_code ec;
            if (!rel.empty() && !std::filesystem::exists(m.path / rel, ec)) {
                out.push_back({"warn", m.name,
                               std::string("contribute.") + what + " names " + rel + ", which is not in the repository"});
            }
        }
    }

    for (const auto& [_, m] : projects_) {
        for (const auto& req : m.requirements) {
            const Manifest* dep = get(req.name);
            if (!dep && req.optional) {
                out.push_back({"note", m.name,
                               "builds without the optional \"" + req.name +
                                   "\": it has no manifest in this registry"});
                continue;
            }
            if (!dep) {
                out.push_back({"error", m.name,
                               "requires \"" + req.name +
                                   "\", which has no manifest in this registry"});
                continue;
            }
            if (!req.version.allows(dep->version)) {
                out.push_back({"error", m.name,
                               "requires " + req.name + " " + req.version.text() + ", but " +
                                   req.name + " is at " + dep->version.str()});
            }
            std::error_code ec;
            if (!req.sibling.empty() && !std::filesystem::exists(m.path / req.sibling, ec)) {
                out.push_back({"warn", m.name,
                               "requires " + req.name + " at sibling path \"" + req.sibling +
                                   "\", which does not exist from " + m.path.string()});
            }
            if (!req.via.empty() && !contains(req.via)) {
                out.push_back({"warn", m.name,
                               "reaches " + req.name + " via \"" + req.via +
                                   "\", an unknown project"});
            }
            if (git_remote_state(dep->path) == GitRemote::None) {
                // Only a checkout that IS a repository and has nowhere to push
                // is reported. A folder that is not under git at all is how
                // every test family is laid out, and says nothing about other
                // machines.
                out.push_back({"warn", m.name,
                               "requires " + req.name + ", whose checkout at " +
                                   dep->path.string() +
                                   " has no git remote: no machine without this folder can build " +
                                   m.name});
            }
            if (dep->kind == "application") {
                out.push_back({"warn", m.name,
                               "requires " + req.name +
                                   ", which declares itself an application; applications are "
                                   "installed, not linked"});
            }
        }
        check_reciprocity(m, family, out);
        check_versions(m, out);
        check_vendors(m, out);
        check_release(m, out);
    }

    // Drift is a fact about the FAMILY, not about any one manifest, so it is
    // computed once over all of them rather than per project.
    {
        const Inventory inventory = take_inventory(*this);
        for (const Library* lib : inventory.drifting()) {
            std::string where;
            for (const auto& use : lib->uses) {
                if (!where.empty()) where += ", ";
                where += use.project + " " +
                         (use.declared.version.empty() ? "(unstated)" : use.declared.version);
            }
            out.push_back({"error", lib->name,
                           "vendored at more than one version: " + where +
                               ". Two copies of one library in one family is a link nobody "
                               "chose; two in one binary is a bug with no error message"});
        }
        for (const auto& u : inventory.undeclared) {
            out.push_back({"note", u.project,
                           "has " + u.path +
                               " on disk and declares no vendors[] entry for it"});
        }
    }

    check_unmanifested(out);
    check_cycles(out);
    return out;
}

}  // namespace mago
