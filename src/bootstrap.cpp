// `mago bootstrap` -- see include/mago/bootstrap.hpp.
#include "mago/bootstrap.hpp"

#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <sstream>

#include "mago/manifest.hpp"

namespace fs = std::filesystem;

namespace mago {

std::optional<Version> tag_version(std::string tag) {
    if (tag.rfind("refs/tags/", 0) == 0) tag.erase(0, 10);
    if (tag.size() > 3 && tag.compare(tag.size() - 3, 3, "^{}") == 0) tag.resize(tag.size() - 3);
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    return Version::parse(tag);
}

std::optional<std::string> choose_tag(const std::vector<std::string>& tags,
                                      const std::vector<Range>& ranges) {
    std::optional<Version> best;
    std::string best_tag;
    for (const auto& t : tags) {
        const auto v = tag_version(t);
        if (!v) continue;
        bool ok = true;
        for (const auto& r : ranges) ok = ok && r.allows(*v);
        if (ok && (!best || *v > *best)) {
            best = v;
            best_tag = t;
        }
    }
    if (!best) return std::nullopt;
    return best_tag;
}

namespace {

std::string joined(const std::vector<Range>& ranges) {
    std::string s;
    for (const auto& r : ranges) s += (s.empty() ? "" : " and ") + r.text();
    return s.empty() ? "*" : s;
}

bool allowed_by_all(const Version& v, const std::vector<Range>& ranges) {
    for (const auto& r : ranges)
        if (!r.allows(v)) return false;
    return true;
}

// A dependency named in one requirer, waiting to be laid out.
struct Pending {
    Requirement req;
    fs::path dir;
    std::string requirer;
};

}  // namespace

BootstrapResult bootstrap(const fs::path& target, Git& git, const BootstrapOptions& options) {
    BootstrapResult result;
    const Manifest root = load(target);

    std::map<std::string, std::vector<Range>> ranges;    // every range seen, per project
    std::map<std::string, size_t> step_of;               // project -> its step
    std::map<std::string, std::optional<Version>> have;  // the version laid out, if known
    std::deque<Pending> queue;

    const auto enqueue = [&](const Manifest& m) {
        for (const auto& req : m.requirements) {
            const fs::path dir = (m.path / req.sibling).lexically_normal();
            queue.push_back({req, req.sibling.empty() ? fs::path() : dir, m.name});
        }
    };
    enqueue(root);

    while (!queue.empty()) {
        Pending p = std::move(queue.front());
        queue.pop_front();
        const std::string& name = p.req.name;
        ranges[name].push_back(p.req.version);

        // Seen before: this requirer can only narrow what is acceptable.
        if (const auto it = step_of.find(name); it != step_of.end()) {
            BootstrapStep& s = result.steps[it->second];
            s.range = joined(ranges[name]);
            const auto& v = have[name];
            if (v && !p.req.version.allows(*v)) {
                s.notes.push_back(p.requirer + " requires " + p.req.version.text() + ", but " +
                                  v->str() + " is what is laid out: nothing was moved");
                if (!p.req.optional) result.ok = false;
            }
            continue;
        }

        BootstrapStep s;
        s.name = name;
        s.optional = p.req.optional;
        s.repository = p.req.repository;
        s.range = joined(ranges[name]);
        s.dir = p.dir;
        const auto record = [&](BootstrapStep&& st) {
            if (st.action == BootstrapStep::Action::Failed) result.ok = false;
            step_of[name] = result.steps.size();
            result.steps.push_back(std::move(st));
        };

        if (s.dir.empty()) {
            s.action = s.optional ? BootstrapStep::Action::Absent : BootstrapStep::Action::Failed;
            s.notes.push_back(p.requirer + " names no `sibling` path for it");
            record(std::move(s));
            continue;
        }

        std::error_code ec;
        if (fs::exists(s.dir, ec)) {
            // The person's folder. Read it, judge it, touch nothing.
            if (!fs::is_directory(s.dir, ec)) {
                s.action = BootstrapStep::Action::Failed;
                s.notes.push_back("a file is in the way at " + s.dir.string());
                record(std::move(s));
                continue;
            }
            if (!fs::exists(s.dir / kManifestName, ec)) {
                // A checkout somebody made by hand (Void Hammer has no manifest
                // yet). Used as it is; its version simply cannot be checked.
                s.action = BootstrapStep::Action::Present;
                s.notes.push_back("already here, with no void.json: used as it is, its version unchecked");
                record(std::move(s));
                continue;
            }
            try {
                const Manifest m = load(s.dir);
                s.action = BootstrapStep::Action::Present;
                have[name] = m.version;
                if (s.repository.empty()) s.repository = m.repository;
                s.ref = m.version.str();
                if (!allowed_by_all(m.version, ranges[name])) {
                    s.notes.push_back("on disk at " + m.version.str() + ", which " +
                                      s.range + " does not allow: update it yourself");
                    if (!s.optional) result.ok = false;
                }
                record(std::move(s));
                enqueue(m);
            } catch (const ManifestError& e) {
                s.action = BootstrapStep::Action::Failed;
                s.notes.push_back(e.what());
                record(std::move(s));
            }
            continue;
        }

        if (s.repository.empty()) {
            s.action = s.optional ? BootstrapStep::Action::Absent : BootstrapStep::Action::Failed;
            s.notes.push_back(s.optional
                                  ? "no repository is named, so it is left out; " + root.name +
                                        " builds without it"
                                  : p.requirer + " requires it but names no `repository` for it");
            record(std::move(s));
            continue;
        }

        std::string err;
        const std::vector<std::string> tags = git.tags(s.repository, err);
        if (!err.empty()) {
            s.action = s.optional ? BootstrapStep::Action::Absent : BootstrapStep::Action::Failed;
            s.notes.push_back("could not read " + s.repository + ": " + err);
            record(std::move(s));
            continue;
        }
        const auto tag = choose_tag(tags, ranges[name]);
        if (tag) {
            s.ref = *tag;
            have[name] = tag_version(*tag);
            s.notes.push_back("the newest release " + s.range + " allows");
        } else if (tags.empty() || ranges[name].size() == 1 && ranges[name][0].is_any()) {
            s.notes.push_back("no release tags: the default branch, which no matrix has approved");
        } else {
            s.action = s.optional ? BootstrapStep::Action::Absent : BootstrapStep::Action::Failed;
            s.notes.push_back("no release satisfies " + s.range);
            record(std::move(s));
            continue;
        }

        s.action = BootstrapStep::Action::Clone;
        if (options.dry_run) {
            s.notes.push_back("its own requirements are read once it is cloned");
            record(std::move(s));
            continue;
        }
        if (!git.clone(s.repository, s.ref, s.dir, options.shallow_deps, err)) {
            s.action = s.optional ? BootstrapStep::Action::Absent : BootstrapStep::Action::Failed;
            s.notes.push_back("clone failed: " + err);
            record(std::move(s));
            continue;
        }
        record(std::move(s));
        if (fs::exists(result.steps.back().dir / kManifestName, ec)) {
            try {
                const Manifest m = load(result.steps.back().dir);
                if (!have[name]) have[name] = m.version;
                // a default branch was taken on trust: hold it to the ranges now
                if (!allowed_by_all(m.version, ranges[name])) {
                    result.steps.back().notes.push_back(
                        "its default branch is " + m.version.str() + ", which " +
                        result.steps.back().range + " does not allow");
                    if (!p.req.optional) result.ok = false;
                }
                enqueue(m);
            } catch (const ManifestError& e) {
                result.steps.back().notes.push_back(e.what());
            }
        }
    }
    return result;
}

// ── the git on PATH ──────────────────────────────────────────────────────────

namespace {

// One argument, quoted for the shell. A quote inside it is refused rather than
// escaped: a URL or a folder name with a quote in it is not one bootstrap has
// any business passing to a shell.
bool quote(const std::string& a, std::string& out) {
    if (a.find('"') != std::string::npos) return false;
    out += " \"" + a + "\"";
    return true;
}

int run(const std::string& cmd, std::string& out) {
#ifdef _WIN32
    FILE* f = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (!f) return -1;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof buf, f)) out.append(buf, n);
#ifdef _WIN32
    return _pclose(f);
#else
    return pclose(f);
#endif
}

std::string last_line(const std::string& s) {
    std::string t = s;
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    const size_t nl = t.find_last_of('\n');
    return nl == std::string::npos ? t : t.substr(nl + 1);
}

class SystemGit final : public Git {
public:
    std::vector<std::string> tags(const std::string& url, std::string& error) override {
        std::string cmd = "git ls-remote --tags --refs";
        if (!quote(url, cmd)) { error = "a quote in the URL"; return {}; }
        std::string out;
        if (run(cmd, out) != 0) { error = last_line(out); return {}; }
        std::vector<std::string> tags;
        std::istringstream in(out);
        std::string line;
        while (std::getline(in, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string ref = line.substr(tab + 1);
            while (!ref.empty() && (ref.back() == '\r' || ref.back() == ' ')) ref.pop_back();
            if (ref.rfind("refs/tags/", 0) == 0) tags.push_back(ref.substr(10));
        }
        return tags;
    }

    bool clone(const std::string& url, const std::string& ref, const fs::path& dir, bool shallow,
               std::string& error) override {
        std::string cmd = "git clone --quiet";
        if (shallow) cmd += " --depth 1";
        if (!ref.empty() && !(quote("--branch", cmd) && quote(ref, cmd))) { error = "bad ref"; return false; }
        cmd += " --";
        if (!quote(url, cmd) || !quote(dir.string(), cmd)) { error = "a quote in the URL or path"; return false; }
        std::string out;
        if (run(cmd, out) != 0) { error = last_line(out); return false; }
        return true;
    }
};

}  // namespace

std::unique_ptr<Git> system_git() { return std::make_unique<SystemGit>(); }

int run_program(const std::vector<std::string>& argv, std::string& out) {
    if (argv.empty()) return -1;
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i == 0 && argv[0].find_first_of(" \"") == std::string::npos) { cmd = argv[0]; continue; }
        std::string q;
        if (!quote(argv[i], q)) return -1;
        cmd += q;
    }
    return run(cmd, out);
}

}  // namespace mago
