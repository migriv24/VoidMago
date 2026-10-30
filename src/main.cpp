// `mago` -- the command line, and the only front end that must always work.
//
// Deliberately dependency-free: C++20, the standard library, and one vendored
// JSON parser. NOT Void Core and NOT Void Maiz. An updater that needs the
// libraries it is updating cannot repair a broken install, and that is a
// bootstrap the family cannot afford. The desktop GUI is a Void Maiz host on
// top of this, exactly as Void Hormiga is -- and it is second.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "mago/blast.hpp"
#include "mago/emit_nsis.hpp"
#include "mago/feed.hpp"
#include "mago/family.hpp"
#include "mago/home.hpp"
#include "mago/matrix.hpp"
#include "mago/notify.hpp"
#include "mago/registry.hpp"
#include "mago/resolve.hpp"
#include "mago/stage.hpp"
#include "mago/vendors.hpp"
#include "mago/bootstrap.hpp"

namespace fs = std::filesystem;
using namespace mago;

namespace {

constexpr const char* kUsage = R"(Void Mago -- the keeper of the Void family.

usage: mago [--root DIR] <command> [options]

  scan                    every Void project that carries a manifest
  graph                   the family's edges, dependencies and seams apart
  doctor                  check the manifests against each other
  plan  <application>     resolve one application into an install plan
  blast <project>         who is affected if this changes, and how badly
  notify <project>        the whole broadcast: verdict, behavior changes, surface
  matrix                  every pair of projects, in one table
  vendors                 third-party code the family uses and does not own
  notices <application>   the third-party notices for everything in its binary
  pair  <a> <b>           one pair in detail, and where you write what you know
  stage <application>     gather the files an installer needs, and emit its script
  feed  [applications]    the update document a client reads to learn what exists
  register <folder>       remember a folder holding Void checkouts
  roots                   the folders this machine knows
  forget <folder>         stop scanning one
  wizard [applications]   emit the installer from the resolved plans
  report <project>        the stack as built here, and where to report it
  upstream <project>      how many releases a fork is behind upstream
  learn <project>         the reading order, and FaultSack's study site for it
  bootstrap <repo|path>   clone a project and everything it requires, as siblings,
                          at releases every requirer accepts

options
  --version               this binary's version, so a stale one is noticed
  --root DIR              folder holding the Void checkouts
                          (default: the registered roots, else the parent of
                          this executable's repo)
  --label TEXT            a name for a registered folder          (register)
  --json                  machine-readable output
                          (scan, doctor, plan, blast, notify)
  --platform NAME         e.g. windows-x64          (plan, wizard)
  --to VERSION            the version it is moving to   (blast, notify)
                          default: the manifest's own `release` block
  --change KIND           compatible | breaking, overriding --to (blast, notify)
  --all                   include untouched projects    (blast, notify)
  --write                 write each message into its project's root  (notify)
  --all                   include pairs with no edge             (matrix)
  --note TEXT             what you know about this pair            (pair)
  --seam NAME             the agreed name, when two manifests differ (pair)
  --shares A,B            what actually crosses                     (pair)
  --status WORD           agreed | proposed | disputed              (pair)
  --clear                 forget what was authored about this pair  (pair)
  --family PATH           the authored document  (default: <voidmago>/family.json)
  --topic SLUG            the filename's middle part            (notify)
  --date YYYY-MM-DD       default: today                        (notify)
  --target KIND           nsis | plan                          (wizard)
  --dry-run               find the files, copy nothing            (stage)
  --base-url URL          where the installers are served          (feed)
  --artifacts DIR         built installers, to hash                (feed)
                          also checks the version-free copy a
                          download page links to
  -o, --output PATH       write here instead of stdout          (wizard)
  --suite-name NAME       default "Void Suite"                  (wizard)
  --publisher NAME        default "Void"                        (wizard)
  --outfile NAME          installer filename                    (wizard)
  --license-file PATH     shown on the licence page             (wizard)
  --source-root PATH      where the built artifacts sit         (wizard)
  --into DIR              where the siblings go             (bootstrap)
                          default: the folder holding the target
  --dry-run               decide every version, clone nothing (bootstrap)
  --full                  full history for dependencies too   (bootstrap)
)";

// A hand-rolled parser, because argparse is not in the standard library and a
// dependency here would contradict the whole point of this file.
struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::vector<std::string> flags;

    bool has(const std::string& f) const {
        return std::find(flags.begin(), flags.end(), f) != flags.end();
    }
    std::string opt(const std::string& k, std::string fallback = "") const {
        const auto it = options.find(k);
        return it == options.end() ? fallback : it->second;
    }
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

const std::vector<std::string> kValueOptions = {
    "--root",       "--platform", "--to",           "--change",      "--target",
    "--output",     "-o",         "--suite-name",   "--publisher",   "--outfile",
    "--license-file", "--source-root", "--topic",   "--date",
    "--note",       "--seam",     "--shares",       "--status",      "--family",
    "--label",      "--base-url", "--artifacts", "--into"};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            args.flags.push_back("--help");
        } else if (std::find(kValueOptions.begin(), kValueOptions.end(), a) !=
                   kValueOptions.end()) {
            if (i + 1 >= argc) throw UsageError(a + " needs a value");
            const std::string key = (a == "-o") ? "--output" : a;
            args.options[key] = argv[++i];
        } else if (a.rfind("--", 0) == 0) {
            args.flags.push_back(a);
        } else if (args.command.empty()) {
            args.command = a;
        } else {
            args.positional.push_back(a);
        }
    }
    return args;
}

// Default root: the parent of the directory holding this repository. The
// executable lives in build/bin, so walk up until a void.json is beside us.
fs::path default_root(const char* argv0) {
    std::error_code ec;
    fs::path here = fs::absolute(fs::path(argv0), ec).parent_path();
    for (int i = 0; i < 6 && !here.empty(); ++i) {
        if (fs::exists(here / kManifestName, ec)) return here.parent_path();
        here = here.parent_path();
    }
    return fs::current_path(ec).parent_path();
}

// The authored document lives with the project whose job is the family --
// version-controlled, and travelling with the tool rather than sitting
// unversioned above everybody's checkouts. `--family` overrides for a second
// family, or a scratch copy.
fs::path family_path(const Registry& reg, const Args& args) {
    if (args.options.count("--family")) return fs::path(args.opt("--family"));
    if (const Manifest* mago = reg.get("voidmago")) return Family::default_path(mago->path);
    return fs::path(kFamilyName);
}

// Manifests that were present and refused, counted once by `load_registry` and
// read once by `main`. A command that computed over a family with a hole in it
// did not do what it was asked, however well the part it could see went -- and
// an exit code of 0 is the last place that could still say otherwise.
int g_unread_manifests = 0;

// `--root` wins; then whatever was registered; then the built-in guess. A
// machine with nothing registered behaves exactly as it did before there was a
// home to register anything in.
Registry load_registry(const Args& args, const char* argv0) {
    std::error_code ec;
    std::vector<fs::path> roots;
    if (args.options.count("--root")) {
        roots.push_back(fs::path(args.opt("--root")));
    } else {
        roots = Home::load().paths();
    }
    if (roots.empty()) roots.push_back(default_root(argv0));

    for (const auto& root : roots) {
        if (!fs::is_directory(root, ec)) {
            throw UsageError(root.string() + " is not a directory");
        }
    }

    Registry reg = Registry::scan(roots);

    // A manifest that is present and unreadable is a project missing from
    // everything printed below, and nothing in the output would show the gap.
    // So it is said once, on stderr, before any command speaks -- except for
    // `doctor`, whose whole job is to list these properly.
    g_unread_manifests = static_cast<int>(reg.load_errors().size());
    if (const auto& unread = reg.load_errors(); !unread.empty() && args.command != "doctor") {
        for (const auto& f : unread) std::cerr << "mago: " << f.message << "\n";
        std::cerr << "mago: " << unread.size()
                  << (unread.size() == 1 ? " manifest was not read, and nothing below counts it."
                                         : " manifests were not read, and nothing below counts "
                                           "them.")
                  << "\n\n";
    }

    if (reg.projects().empty()) {
        // A registry emptied by REFUSALS is not an empty folder, and saying
        // "none found" would send the reader looking in the wrong place -- the
        // manifests are exactly where they expect, and Mago would not read
        // them. `doctor` is handed the registry anyway: listing these is the
        // one job it still has when nothing else can be computed.
        if (!reg.load_errors().empty()) {
            if (args.command == "doctor") return reg;
            throw UsageError("every " + std::string(kManifestName) +
                             " under the roots was refused, so there is no family to work "
                             "from. The refusals are above; `mago doctor` prints them with "
                             "the rest.");
        }
        std::string where;
        for (const auto& root : roots) where += "\n        " + root.string();
        throw UsageError("no void.json found under:" + where +
                         "\n      Point --root at the folder that holds the Void checkouts, "
                         "or `mago register` it.");
    }
    return reg;
}

std::string pad(std::string s, std::size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

std::string join(const std::vector<std::string>& xs, const char* sep = ", ") {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

// ---- commands -----------------------------------------------------------

int cmd_scan(const Registry& reg, const Args& args) {
    if (args.has("--json")) {
        JsonWriter w(2);
        w.begin_object().key("projects").begin_object();
        for (const auto& [name, m] : reg.projects()) {
            w.key(name).begin_object();
            w.field("display_name", m.display_name);
            w.field("kind", m.kind);
            w.key("version").value(m.version.str());
            w.field("path", m.path.string());
            w.string_array("platforms", m.platforms);
            w.key("requires").begin_array();
            for (const auto& r : m.requirements) w.value(r.name);
            w.end_array().end_object();
        }
        w.end_object().key("unmanifested").begin_array();
        for (const auto& p : reg.unmanifested()) w.value(p.filename().string());
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << reg.projects().size() << " Void projects with a manifest\n\n";
    std::size_t name_width = 18;  // the widest name, so none runs into its version
    for (const auto& [_, m] : reg.projects()) {
        name_width = std::max(name_width, m.display_name.size() + 2);
    }
    for (const char* kind : {"application", "capability", "runtime"}) {
        std::vector<const Manifest*> members;
        for (const auto& [_, m] : reg.projects()) {
            if (m.kind == kind) members.push_back(&m);
        }
        if (members.empty()) continue;
        std::cout << "  " << kind << "s\n";
        for (const Manifest* m : members) {
            std::vector<std::string> deps;
            for (const auto& r : m->requirements) deps.push_back(r.name);
            std::cout << "    " << pad(m->display_name, name_width) << pad(m->version.str(), 9)
                      << " requires " << (deps.empty() ? "-" : join(deps)) << "\n";
        }
        std::cout << "\n";
    }
    if (!reg.unmanifested().empty()) {
        std::cout << "  no manifest yet (" << reg.unmanifested().size() << ")\n";
        for (const auto& p : reg.unmanifested()) {
            std::cout << "    " << p.filename().string() << "\n";
        }
    }
    return 0;
}

int cmd_graph(const Registry& reg) {
    std::cout << "requires  (a real dependency edge: Mago resolves these)\n\n";
    for (const auto& [_, m] : reg.projects()) {
        if (m.requirements.empty()) continue;
        std::cout << "  " << m.display_name << " " << m.version.str() << "\n";
        for (const auto& r : m.requirements) {
            const Manifest* dep = reg.get(r.name);
            std::cout << "    -> " << pad(r.name, 16) << pad(r.version.text(), 10)
                      << pad(r.link.empty() ? "?" : r.link, 9) << " at "
                      << (dep ? dep->version.str() : "MISSING")
                      << (r.via.empty() ? "" : " via " + r.via) << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "interacts_with  (data crosses, nothing links)\n\n";
    for (const auto& [_, m] : reg.projects()) {
        std::vector<const Interaction*> edges;
        for (const auto& i : m.interacts_with) {
            if (i.name != "*") edges.push_back(&i);
        }
        if (edges.empty()) continue;
        std::cout << "  " << m.display_name << "\n";
        for (const Interaction* i : edges) {
            std::cout << "    " << (i->direction == "inbound" ? "<-" : "->") << " "
                      << pad(i->name, 16) << (i->seam.empty() ? "?" : i->seam) << "\n";
        }
        std::cout << "\n";
    }
    return 0;
}

int cmd_doctor(const Registry& reg, const Args& args, const Family& family) {
    const std::vector<Finding> findings = reg.check(&family);

    if (args.has("--json")) {
        JsonWriter w(2);
        w.begin_array();
        for (const auto& f : findings) {
            w.begin_object();
            w.field("level", f.level);
            w.field("project", f.project);
            w.field("message", f.message);
            w.end_object();
        }
        w.end_array();
        std::cout << w.str() << "\n";
    } else if (findings.empty()) {
        std::cout << "mago doctor: the family agrees with itself.\n";
        return 0;
    } else {
        std::map<std::string, int> counts;
        for (const char* level : {"error", "warn", "note"}) {
            bool printed = false;
            for (const auto& f : findings) {
                if (f.level != level) continue;
                if (!printed) {
                    std::cout << "\n";
                    printed = true;
                }
                std::cout << std::setw(5) << f.level << "  " << pad(f.project, 16) << f.message
                          << "\n";
            }
        }
        for (const auto& f : findings) counts[f.level]++;
        std::cout << "\n"
                  << counts["error"] << " errors, " << counts["warn"] << " warnings, "
                  << counts["note"] << " notes\n";
    }

    for (const auto& f : findings) {
        if (f.level == "error") return 1;
    }
    return 0;
}

int cmd_plan(const Registry& reg, const Args& args) {
    if (args.positional.empty()) throw UsageError("plan needs an application name");
    const Plan plan = resolve(reg, args.positional.front(), args.opt("--platform"));

    if (args.has("--json")) {
        std::cout << plan.to_json() << "\n";
        return 0;
    }

    std::cout << plan.display_name << " " << plan.version.str() << "   platform "
              << (plan.platform.empty() ? "unspecified" : plan.platform) << "\n"
              << "install into  " << plan.install_dir_name() << "/\n\n";

    std::cout << "build order\n";
    int n = 0;
    for (const auto& r : plan.build_order) {
        std::cout << "  " << ++n << ". " << r.display_name << " " << r.version.str();
        if (r.role != Role::Root) {
            std::cout << "  (" << (r.link.empty() ? "no link declared" : r.link) << ")";
        }
        std::cout << "\n";
    }

    std::cout << "\nfiles on the machine\n";
    for (const auto& r : plan.payload) {
        if (r.files.empty()) {
            std::cout << "  " << pad(r.display_name, 18) << "(no artifacts declared)\n";
            continue;
        }
        for (const auto& f : r.files) {
            std::cout << "  " << pad(r.display_name, 18) << f << "\n";
        }
    }

    if (!plan.absorbed.empty()) {
        std::cout << "\ncompiled in -- no file of its own, but a rebuild if it changes\n";
        for (const auto& r : plan.absorbed) {
            std::cout << "  " << r.display_name << " " << r.version.str() << "  (" << r.link
                      << (r.via.empty() ? "" : " via " + r.via) << ")\n";
        }
    }

    if (!plan.notes.empty()) {
        std::cout << "\nnotes\n";
        for (const auto& note : plan.notes) std::cout << "  " << note << "\n";
    }
    return 0;
}

// `--to` and `--change`, which mean the same thing to `blast` and to `notify`.
// Both may be absent: the changed project's own `release` block answers then.
struct Move {
    std::optional<Version> to;
    std::optional<Change> change;
};

Move parse_move(const Args& args) {
    Move move;
    if (const std::string t = args.opt("--to"); !t.empty()) {
        move.to = Version::parse(t);
        if (!move.to) throw UsageError("--to " + t + " is not a version");
    }
    if (const std::string c = args.opt("--change"); !c.empty()) {
        if (c == "compatible") {
            move.change = Change::Compatible;
        } else if (c == "breaking") {
            move.change = Change::Breaking;
        } else {
            throw UsageError("--change must be compatible or breaking, got \"" + c + "\"");
        }
    }
    return move;
}

// The project a release is about, checked before anything is computed about it.
std::string subject(const Registry& reg, const Args& args, const char* verb) {
    if (args.positional.empty()) throw UsageError(std::string(verb) + " needs a project name");
    const std::string project = args.positional.front();
    if (!reg.contains(project)) throw UsageError("no manifest for \"" + project + "\"");
    return project;
}

int cmd_blast(const Registry& reg, const Args& args) {
    const std::string project = subject(reg, args, "blast");
    const Move move = parse_move(args);

    const BlastReport report = analyse(reg, project, move.to, move.change);
    if (args.has("--json")) {
        std::cout << report.to_json() << "\n";
        return 0;
    }

    std::cout << reg.at(report.changed).display_name;
    if (report.to_version) {
        std::cout << " " << report.from_version.str() << " -> " << report.to_version->str();
    }
    std::cout << "   change is " << change_name(report.change)
              << (report.from_release ? "   (declared by the release block)" : "") << "\n\n";

    struct Row {
        Verdict v;
        const char* label;
    };
    for (const Row row : {Row{Verdict::Blocked,
                              "BLOCKED  -- this version is not allowed by its own consumer"},
                          Row{Verdict::Rebuild, "REBUILD  -- must be built again"},
                          Row{Verdict::Reship, "RESHIP   -- ship the new file, do not rebuild"},
                          Row{Verdict::Retest,
                              "RETEST   -- re-run the contract, nothing rebuilds"},
                          Row{Verdict::Untouched, "UNTOUCHED"}}) {
        const std::vector<Impact> group = report.by_verdict(row.v);
        if (group.empty()) continue;

        if (row.v == Verdict::Untouched && !args.has("--all")) {
            std::vector<std::string> names;
            for (const auto& i : group) names.push_back(i.project);
            std::cout << group.size() << " untouched  (" << join(names) << ")\n";
            continue;
        }
        std::cout << row.label << "\n";
        for (const auto& i : group) {
            std::cout << "  " << pad(i.display_name, 18) << i.reason << "\n";
            if (i.path.size() > 1) {
                std::cout << "  " << pad("", 18) << "through " << i.through() << "\n";
            }
        }
        std::cout << "\n";
    }

    if (!report.unverified.empty()) {
        // Not a verdict: a verdict Mago CANNOT compute, said out loud. This is
        // the exact gap that made three of Void Core's hand-written 0.2.14
        // verdicts guesses about repositories nobody re-read.
        std::cout << "UNVERIFIED -- named as a partner, no manifest here, no verdict\n";
        for (const auto& u : report.unverified) {
            std::cout << "  " << pad(u.name, 18) << u.reason << "\n";
        }
        std::cout << "\n";
    }

    if (report.needs_action().empty()) std::cout << "Nothing downstream. Release it.\n";
    return 0;
}

// `mago notify` -- the broadcast, not the verdict.
//
// A verdict alone would not have been enough for any of the eight messages
// Void Core wrote by hand for 0.2.14. This adds the three mechanical parts a
// verdict cannot carry -- behavior changes, new optional surface, where to
// read the rest -- and leaves the fourth, what the release means for one
// project in particular, as a hole with a person's name on it.
int cmd_notify(const Registry& reg, const Args& args) {
    const std::string project = subject(reg, args, "notify");
    const Move move = parse_move(args);

    const Broadcast b = gather(reg, project, move.to, move.change);
    NotifyOptions opt;
    opt.topic = args.opt("--topic");
    opt.date = args.opt("--date");
    opt.include_untouched = args.has("--all");
    const std::vector<Message> messages = compose(reg, b, opt);

    if (args.has("--json")) {
        std::cout << to_json(b, messages) << "\n";
        return 0;
    }

    if (!args.has("--write")) {
        std::cout << render(reg, b, messages);
        if (!b.has_narrative()) {
            std::cout << "\nThis release declares no summary, no behavior changes and no new "
                         "surface, so\nthe messages say only what `mago blast` already said. A "
                         "`release` block in\n"
                      << project << "'s " << kManifestName
                      << " is what makes a broadcast worth sending.\n";
        }
        return 0;
    }

    // Writing into somebody else's repository is the only thing here with a
    // side effect, so it is never the default and it always says what it did.
    int written = 0;
    for (const auto& m : messages) {
        std::ofstream f(m.path, std::ios::binary);
        if (!f) throw UsageError("cannot write " + m.path.string());
        f << m.body;
        f.close();
        std::cout << "mago: wrote " << m.path.string() << "\n";
        ++written;
    }
    if (written == 0) {
        std::cout << "mago: nothing downstream has anything to do; no messages written.\n";
        return 0;
    }
    std::cout << "\n"
              << written << (written == 1 ? " message" : " messages")
              << " written. Section 4 of each is an empty hole: what this release\n"
                 "means for that project in particular is the part no manifest holds.\n";
    return 0;
}

// `mago matrix` -- every pair of projects, in one table.
//
// The GUI's whole data source, and useful in a terminal on its own. Desktop
// renders this; it does not compute it, and there is nothing here it could
// compute better.
// `mago register` / `roots` / `forget` -- the folders this machine knows.
//
// The only state Mago keeps between runs, and it is deliberately the smallest
// possible thing: a list of where to look. Nothing about what was found is
// remembered, because a registry that cached what a manifest said would be a
// registry that could be wrong about it.
int cmd_roots(const Args& args, const char* argv0) {
    Home home = Home::load();

    if (args.command == "register" || args.command == "forget") {
        if (args.positional.empty()) {
            throw UsageError(args.command + " needs a folder");
        }
        for (const auto& given : args.positional) {
            const fs::path path(given);
            if (args.command == "forget") {
                std::cout << (home.remove(path) ? "mago: forgot " : "mago: was not registered: ")
                          << path.string() << "\n";
                continue;
            }
            std::error_code ec;
            if (!fs::is_directory(path, ec)) {
                throw UsageError(path.string() + " is not a directory");
            }
            const bool added = home.add(path, args.opt("--label"), today());
            const Registry found = Registry::scan(path);
            std::cout << (added ? "mago: registered " : "mago: already registered ")
                      << fs::absolute(path, ec).lexically_normal().string() << "\n"
                      << "      " << found.projects().size() << " with a manifest, "
                      << found.unmanifested().size() << " that look like Void projects "
                      << "and carry none\n";
        }
        home.save();
        std::cout << "      " << Home::roots_file().string() << "\n";
        return 0;
    }

    if (args.has("--json")) {
        JsonWriter w(2);
        w.begin_object();
        w.field("report", "mago-roots/0.1");
        w.field("home", Home::dir().generic_string());
        w.key("roots").begin_array();
        for (const auto& r : home.roots()) {
            w.begin_object();
            w.field("path", r.path.generic_string());
            w.field("label", r.label);
            w.field("added", r.added);
            w.key("exists").value(r.exists);
            w.end_object();
        }
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    if (home.empty()) {
        std::cout << "No folder is registered, so `mago` scans "
                  << default_root(argv0).string() << ".\n\n"
                  << "  mago register <folder>    remember a folder holding Void checkouts\n";
        return 0;
    }
    std::cout << home.roots().size() << " registered\n\n";
    for (const auto& r : home.roots()) {
        std::cout << "  " << pad(r.path.string(), 52) << (r.exists ? "" : "MISSING");
        if (!r.label.empty()) std::cout << "  " << r.label;
        std::cout << "\n";
    }
    std::cout << "\n" << Home::roots_file().string() << "\n";
    return 0;
}

// `mago vendors` -- what is in these binaries that the family did not write.
//
// The inventory nobody had: six repositories vendor cJSON, five keep a
// hand-maintained notices file, and one shipped a toolchain DLL it never
// mentioned. None of that is resolvable -- the family vendors on purpose --
// but all of it is knowable, and the difference between knowing and not is
// whether two copies of one library at two versions is a decision or an
// accident.
int cmd_vendors(const Registry& reg, const Args& args) {
    const Inventory inventory = take_inventory(reg);

    if (args.has("--json")) {
        std::cout << inventory.to_json() << "\n";
        return inventory.drifting().empty() ? 0 : 1;
    }

    if (inventory.libraries.empty() && inventory.undeclared.empty()) {
        std::cout << "No project declares a vendors[] entry, and nothing on disk looks "
                     "vendored.\n";
        return 0;
    }

    std::cout << inventory.libraries.size() << " libraries, "
              << inventory.declarations() << " declarations across "
              << reg.projects().size() << " projects\n\n";

    for (const auto& lib : inventory.libraries) {
        const std::string versions =
            lib.versions.empty() ? "(unstated)" : join(lib.versions, " / ");
        const std::string licenses =
            lib.licenses.empty() ? "(none)" : join(lib.licenses, " / ");
        std::cout << "  " << pad(lib.name, 18) << pad(versions, 14) << pad(licenses, 12)
                  << lib.uses.size() << (lib.uses.size() == 1 ? " project" : " projects")
                  << (lib.drifts() ? "   DRIFT" : "") << "\n";
        std::vector<std::string> who;
        for (const auto& use : lib.uses) {
            who.push_back(use.project +
                          (use.declared.link.empty() ? "" : " (" + use.declared.link + ")"));
        }
        std::cout << "  " << pad("", 18) << join(who) << "\n";
    }

    const std::vector<const Library*> drifting = inventory.drifting();
    if (!drifting.empty()) {
        std::cout << "\nDRIFT -- one library, more than one version\n";
        for (const Library* lib : drifting) {
            for (const auto& use : lib->uses) {
                std::cout << "  " << pad(lib->name, 18) << pad(use.project, 16)
                          << (use.declared.version.empty() ? "(unstated)" : use.declared.version)
                          << "\n";
            }
        }
        std::cout << "\nTwo copies of one library in one family is a link nobody chose. Two\n"
                     "in one binary is a bug with no error message: one set of symbols wins\n"
                     "and the other's callers get different behaviour, silently.\n";
    }

    const std::vector<const Library*> shipping = inventory.must_ship();
    if (!shipping.empty()) {
        std::cout << "\nMUST REACH THE MACHINE -- shared or from the toolchain\n";
        for (const Library* lib : shipping) {
            for (const auto& use : lib->uses) {
                if (use.declared.link != "shared" && use.declared.link != "toolchain") continue;
                std::cout << "  " << pad(lib->name, 18) << pad(use.project, 16)
                          << use.declared.link << "\n";
            }
        }
    }

    if (!inventory.undeclared.empty()) {
        std::cout << "\nUNDECLARED -- on disk, in no manifest\n";
        for (const auto& u : inventory.undeclared) {
            std::cout << "  " << pad(u.project, 16) << u.path << "\n";
        }
        std::cout << "\nEach of these is code in a shipped binary with no recorded version,\n"
                     "no recorded licence, and nothing to compare against the next copy.\n";
    }
    return drifting.empty() ? 0 : 1;
}

// `mago notices` -- the third-party notices for one application.
//
// The argument for declaring vendors at all, and the one thing a
// hand-maintained per-repository notices file structurally cannot get right.
//
// `voidhormiga.exe` contains Void Maiz, Void Allomone and Void Palabra, each
// compiled in, each carrying its own vendored libraries. Hormiga's obligation
// is to the code IN ITS BINARY, which is the transitive closure of the build
// order -- and no file maintained inside Hormiga's repository can see past
// Hormiga's own `vendor/` directory. Mago can, because the build order is
// exactly what it already computes.
int cmd_notices(const Registry& reg, const Args& args) {
    const std::string app = subject(reg, args, "notices");
    const Plan plan = resolve(reg, app, args.opt("--platform"));

    // Everything that ends up in the binary: the application, everything
    // compiled into it, and everything shipped beside it.
    std::vector<std::string> included;
    for (const auto& r : plan.build_order) included.push_back(r.name);

    struct Entry {
        Vendor vendor;
        std::string via;  // which project brought it in
    };
    std::vector<Entry> entries;
    std::size_t unlicensed = 0;
    for (const auto& name : included) {
        const Manifest* m = reg.get(name);
        if (!m) continue;
        for (const auto& v : m->vendors) {
            if (v.link == "system") continue;  // the OS provides it; nothing is redistributed
            if (v.license.empty()) ++unlicensed;
            entries.push_back({v, m->display_name});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.vendor.name < b.vendor.name;
    });

    std::ostringstream s;
    s << "# Third-party notices -- " << plan.display_name << " " << plan.version.str() << "\n\n";
    s << "Generated by `mago notices " << app << "`. Do not edit: it is derived from the\n";
    s << "`vendors[]` blocks of every project compiled into this binary, which is why it\n";
    s << "covers libraries that are not in " << plan.display_name << "'s own repository.\n\n";

    s << "Compiled in: ";
    for (std::size_t i = 0; i < included.size(); ++i) {
        s << (i ? ", " : "") << reg.at(included[i]).display_name;
    }
    s << ".\n\n";

    if (entries.empty()) {
        s << "No project in this build declares a `vendors[]` entry.\n\n";
        s << "That is almost certainly wrong rather than true -- run `mago vendors` for what\n";
        s << "is on disk and undeclared.\n";
    }

    for (const auto& e : entries) {
        s << "## " << e.vendor.name;
        if (!e.vendor.version.empty()) s << " " << e.vendor.version;
        s << "\n\n";
        if (!e.vendor.license.empty()) s << "- **Licence:** " << e.vendor.license << "\n";
        if (!e.vendor.upstream.empty()) s << "- **Upstream:** " << e.vendor.upstream << "\n";
        s << "- **Reaches this binary through:** " << e.via;
        if (!e.vendor.link.empty()) s << " (" << e.vendor.link << ")";
        s << "\n";
        if (!e.vendor.path.empty()) s << "- **Vendored at:** `" << e.vendor.path << "`\n";
        if (!e.vendor.note.empty()) s << "\n" << e.vendor.note << "\n";
        s << "\n";
    }

    const std::string document = s.str();
    const std::string output = args.opt("--output");
    if (output.empty()) {
        std::cout << document;
    } else {
        const fs::path out(output);
        std::error_code ec;
        if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        if (!f) throw UsageError("cannot write " + out.string());
        f << document;
        f.close();
        std::cout << "mago: wrote " << out.string() << "  (" << entries.size()
                  << (entries.size() == 1 ? " library)" : " libraries)") << "\n";
    }

    if (unlicensed > 0) {
        std::cerr << "mago: " << unlicensed
                  << " declared libraries carry no license and appear above without one.\n";
        return 1;
    }
    return entries.empty() ? 1 : 0;
}

int cmd_matrix(const Registry& reg, const Args& args, const Family& family) {
    const Matrix m = Matrix::build(reg, family);
    if (args.has("--json")) {
        std::cout << m.to_json() << "\n";
        return 0;
    }
    std::cout << m.render(args.has("--all"));
    return 0;
}

// `mago pair a b [--note ...]` -- read or write one cell.
//
// The one command in Mago that WRITES authored content, which is why it is the
// only place `family.json` is opened for writing. A GUI editing a cell calls
// this; it does not learn the document's shape.
int cmd_pair(const Registry& reg, const Args& args, Family& family,
             const fs::path& family_path) {
    if (args.positional.size() < 2) throw UsageError("pair needs two project names");
    const std::string a = args.positional[0];
    const std::string b = args.positional[1];
    if (a == b) throw UsageError("a project has no pairing with itself");
    for (const auto& name : {a, b}) {
        if (!reg.contains(name)) throw UsageError("no manifest for \"" + name + "\"");
    }

    const bool writing = args.options.count("--note") || args.options.count("--seam") ||
                         args.options.count("--shares") || args.options.count("--status") ||
                         args.has("--clear");
    if (writing) {
        AuthoredPairing p;
        std::tie(p.a, p.b) = Family::canonical(a, b);
        if (const AuthoredPairing* existing = family.pairing(a, b); existing && !args.has("--clear")) {
            p = *existing;
        }
        if (args.has("--clear")) p = AuthoredPairing{p.a, p.b, "", "", {}, "", ""};
        if (args.options.count("--note")) p.note = args.opt("--note");
        if (args.options.count("--seam")) p.seam = args.opt("--seam");
        if (args.options.count("--status")) p.status = args.opt("--status");
        if (args.options.count("--shares")) {
            p.shares.clear();
            std::string item;
            std::istringstream in(args.opt("--shares"));
            while (std::getline(in, item, ',')) {
                const auto begin = item.find_first_not_of(" \t");
                const auto end = item.find_last_not_of(" \t");
                if (begin != std::string::npos) p.shares.push_back(item.substr(begin, end - begin + 1));
            }
        }
        if (!p.empty()) p.authored = args.opt("--date", today());
        family.set_pairing(p);
        family.save(family_path);
        std::cout << "mago: wrote " << family_path.string() << "\n";
    }

    const Matrix m = Matrix::build(reg, family);
    const Cell cell = m.find(a, b);
    if (args.has("--json")) {
        JsonWriter w(2);
        w.begin_object();
        w.string_array("pair", {a, b});
        w.field("bond", cell.pairing ? bond_name(cell.pairing->bond) : "none");
        w.key("direction").value(static_cast<long long>(cell.direction));
        w.field("seam", cell.pairing ? cell.pairing->seam : "");
        w.field("cost", cell.pairing ? verdict_name(cell.pairing->cost) : "untouched");
        w.field("note", cell.pairing ? cell.pairing->note : "");
        w.string_array("why", cell.pairing ? cell.pairing->why : std::vector<std::string>{});
        w.end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << reg.at(a).display_name
              << (cell.direction > 0 ? "  ->  " : (cell.direction < 0 ? "  <-  " : "  --  "))
              << reg.at(b).display_name << "\n";
    if (!cell.pairing || cell.pairing->bond == Bond::None) {
        std::cout << "\nNo edge in either manifest, and nothing authored.\n"
                     "  mago pair " << a << " " << b << " --note \"...\"   records what you know\n";
        return 0;
    }
    const Pairing& p = *cell.pairing;
    std::cout << "  bond   " << bond_name(p.bond) << (p.disputed ? "   DISPUTED" : "") << "\n";
    if (!p.seam.empty()) std::cout << "  seam   " << p.seam << "\n";
    if (!p.link.empty()) std::cout << "  link   " << p.link << " " << p.version << "\n";
    std::cout << "  cost   " << verdict_name(p.cost)
              << "   (an additive change at either end)\n";
    if (!p.why.empty()) {
        std::cout << "\nwhy\n";
        for (const auto& line : p.why) std::cout << "  " << line << "\n";
    }
    std::cout << "\nnote\n";
    if (p.note.empty()) {
        std::cout << "  -- nothing authored. This is the half no manifest holds:\n"
                     "     mago pair " << a << " " << b << " --note \"...\"\n";
    } else {
        std::cout << "  " << p.note << "\n";
    }
    return 0;
}

// `mago stage` -- gather the files an installer needs into one folder.
//
// The step that was missing. `wizard` has always emitted a script referencing
// `<source-root>\<project>\<file>`; this is what finally puts files there,
// and then writes the script beside them so the whole thing is one command
// away from `makensis`.
int cmd_stage(const Registry& reg, const Args& args) {
    const std::string app = subject(reg, args, "stage");
    const Plan plan = resolve(reg, app, args.opt("--platform"));

    StageOptions opt;
    opt.dry_run = args.has("--dry-run");
    if (args.options.count("--out")) {
        opt.out = fs::path(args.opt("--out"));
    } else {
        const fs::path base = reg.get("voidmago") ? reg.at("voidmago").path / "stage"
                                                  : fs::path("stage");
        opt.out = base / (plan.application + "-" + plan.version.str() +
                          (plan.platform.empty() ? "" : "-" + plan.platform));
    }

    const StageReport report = stage(reg, plan, opt);

    if (args.has("--json")) {
        std::cout << report.to_json() << "\n";
        return report.complete() ? 0 : 1;
    }

    std::cout << plan.display_name << " " << plan.version.str() << "   platform "
              << (plan.platform.empty() ? "unspecified" : plan.platform) << "\n"
              << (opt.dry_run ? "would stage into  " : "staged into  ") << opt.out.string()
              << "\n\n";

    for (const auto& f : report.files) {
        if (!f.problem.empty()) continue;
        std::cout << "  " << pad(f.project, 16) << pad(f.destination.filename().string(), 30);
        if (f.bytes > 0) {
            std::cout << pad(std::to_string(f.bytes / 1024) + " KB", 10);
        } else {
            std::cout << pad("", 10);
        }
        std::cout << f.source.string() << "\n";
        // Several files answered to that name. Which one shipped is not a
        // thing anybody should have to work out from a timestamp.
        for (const auto& other : f.also_found) {
            std::cout << "  " << pad("", 16) << "also found, not used: " << other.string()
                      << "\n";
        }
    }

    if (!report.unresolved.empty()) {
        std::cout << "\nWILL NOT START -- imported by a staged binary, not in the package,\n"
                     "                 and not provided by Windows\n";
        for (const auto& u : report.unresolved) {
            std::cout << "  " << pad(u.binary, 26) << u.dll << "\n";
        }
        std::cout << "\nThis is the failure that only happens on somebody else's machine: the\n"
                     "installer compiles, the files land, and the application does not open.\n"
                     "Fix it in the BUILD -- Void Mago's own CMakeLists does\n"
                     "`target_link_options(voidmago PUBLIC -static)` under MinGW for exactly\n"
                     "this -- or declare the file in `ships_beside_binary` so it travels.\n";
    }

    const std::size_t missing = report.missing();
    if (missing > 0) {
        std::cout << "\nMISSING -- these were promised by a manifest and are not on disk\n";
        for (const auto& f : report.files) {
            if (f.problem.empty()) continue;
            std::cout << "  " << pad(f.project, 16) << f.problem << "\n";
        }
        std::cout << "\n"
                  << missing << (missing == 1 ? " file" : " files")
                  << " missing. A staging tree with a hole compiles into an installer with\n"
                     "a hole, and that is discovered by somebody on another machine when the\n"
                     "application does not start. Fix the build, or fix the manifest.\n";
        return 1;
    }

    if (!report.unresolved.empty()) return 1;

    std::cout << "\n" << report.files.size() << " files, " << report.bytes() / 1024
              << " KB total.\n";

    if (opt.dry_run) return 0;

    // The script goes beside the files it references, with --source-root
    // already pointed at them: a staging tree that needs an argument to
    // compile is a staging tree somebody will compile wrongly.
    nsis::Options nopt;
    nopt.suite_name = args.opt("--suite-name", plan.display_name);
    nopt.suite_dir = nopt.suite_name;
    nopt.suite_dir.erase(std::remove(nopt.suite_dir.begin(), nopt.suite_dir.end(), ' '),
                         nopt.suite_dir.end());
    nopt.publisher = args.opt("--publisher", nopt.publisher);
    nopt.suite_version = plan.version.str();
    nopt.outfile = args.opt("--outfile", installer_name(reg.at(app), plan.platform));
    nopt.source_root = ".";
    if (args.options.count("--license-file")) {
        nopt.license_file = args.opt("--license-file");
    } else if (const Manifest& m = reg.at(app); fs::exists(m.path / "LICENSE")) {
        std::error_code ec;
        fs::copy_file(m.path / "LICENSE", opt.out / "LICENSE",
                      fs::copy_options::overwrite_existing, ec);
        nopt.license_file = "LICENSE";
    }

    const fs::path script = opt.out / (plan.application + ".nsi");
    std::ofstream f(script, std::ios::binary);
    if (!f) throw UsageError("cannot write " + script.string());
    f << nsis::render({plan}, nopt);
    f.close();

    std::ofstream r(opt.out / nsis::kReceiptName, std::ios::binary);
    r << nsis::receipt({plan}, nopt.suite_name);
    r.close();

    std::cout << "\nwrote " << script.filename().string() << " and "
              << nsis::kReceiptName << " beside them.\n"
              << "compile it with:  makensis " << script.string() << "\n"
              << "        produces:  " << nopt.outfile << "\n";
    return 0;
}

// `mago feed` -- the one document that says what exists.
//
// The answer to "do we need a Void Hub": no, because the only part of a hub a
// user benefits from is a list, and a list is a file. Standalone installers,
// one static JSON beside them, and every application reads its own entry.
int cmd_feed(const Registry& reg, const Args& args) {
    std::vector<std::string> names = args.positional;
    if (names.empty()) {
        for (const Manifest* m : reg.applications()) names.push_back(m->name);
    }
    for (const auto& name : names) {
        if (!reg.contains(name)) throw UsageError("no manifest for \"" + name + "\"");
    }

    FeedOptions opt;
    opt.base_url = args.opt("--base-url");
    opt.publisher = args.opt("--publisher", "Void");
    if (args.options.count("--artifacts")) opt.artifacts_dir = fs::path(args.opt("--artifacts"));

    const std::vector<FeedEntry> entries = build_feed(reg, names, opt);
    const std::string document = feed_json(entries, opt);

    const std::string output = args.opt("--output");
    if (output.empty()) {
        std::cout << document << "\n";
        return 0;
    }
    const fs::path out(output);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    if (!f) throw UsageError("cannot write " + out.string());
    f << document << "\n";
    f.close();

    std::cout << "mago: wrote " << out.string() << "  (" << entries.size()
              << (entries.size() == 1 ? " application)" : " applications)") << "\n";
    std::size_t unhashed = 0;
    for (const auto& entry : entries) {
        for (const auto& release : entry.releases) {
            for (const auto& artifact : release.artifacts) {
                if (artifact.sha256.empty()) ++unhashed;
            }
        }
    }
    if (unhashed > 0) {
        std::cout << "      " << unhashed
                  << " artifacts have no hash: pass --artifacts <dir> pointing at the built\n"
                     "      installers, or the feed describes files it has never seen.\n";
    }
    if (opt.base_url.empty()) {
        std::cout << "      no --base-url, so no download URLs. A client can read versions "
                     "from this\n      feed but cannot fetch anything with it.\n";
    }

    // The one step in this pipeline a person still does by hand, and the only
    // one whose failure is invisible from the update side: the version-free
    // copy a download page links to. Updates keep working without it, because
    // they follow `file`; the website button 404s for everybody who is not
    // already a user. Only checkable when there are files to look at.
    if (!opt.artifacts_dir.empty()) {
        for (const auto& entry : entries) {
            for (const auto& release : entry.releases) {
                for (const auto& artifact : release.artifacts) {
                    if (artifact.sha256.empty()) continue;  // already reported above
                    if (!artifact.stable_present) {
                        std::cout << "      no " << artifact.stable_file
                                  << " beside it: a download page that links the\n"
                                     "      version-free name has nothing to fetch. Attach a "
                                     "copy of\n      " << artifact.file
                                  << " under that name.\n";
                    } else if (artifact.stable_sha256 != artifact.sha256) {
                        std::cout << "      " << artifact.stable_file
                                  << " is NOT a copy of " << artifact.file
                                  << ": it hashes\n      differently, so the download page is "
                                     "handing out a different\n      build than the feed "
                                     "describes -- most likely the previous release.\n";
                    }
                }
            }
        }
    }
    return 0;
}

int cmd_wizard(const Registry& reg, const Args& args) {
    std::vector<std::string> names = args.positional;
    if (names.empty()) {
        for (const Manifest* m : reg.applications()) names.push_back(m->name);
    }
    std::vector<Plan> plans;
    for (const auto& name : names) plans.push_back(resolve(reg, name, args.opt("--platform")));

    const std::string target = args.opt("--target", "nsis");
    std::string payload;
    if (target == "plan") {
        JsonWriter w(2);
        w.begin_array();
        for (const auto& p : plans) p.write_json(w);
        w.end_array();
        payload = w.str();
    } else if (target == "nsis") {
        nsis::Options opt;
        opt.suite_name = args.opt("--suite-name", opt.suite_name);
        opt.suite_dir = opt.suite_name;
        opt.suite_dir.erase(std::remove(opt.suite_dir.begin(), opt.suite_dir.end(), ' '),
                            opt.suite_dir.end());
        opt.publisher = args.opt("--publisher", opt.publisher);
        opt.outfile = args.opt("--outfile", opt.outfile);
        opt.license_file = args.opt("--license-file", opt.license_file);
        opt.source_root = args.opt("--source-root", opt.source_root);
        payload = nsis::render(plans, opt);
    } else {
        throw UsageError("unknown --target \"" + target + "\"; expected nsis or plan");
    }

    const std::string output = args.opt("--output");
    if (output.empty()) {
        std::cout << payload << "\n";
        return 0;
    }

    const fs::path out(output);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    if (!f) throw UsageError("cannot write " + out.string());
    f << payload;
    f.close();

    std::vector<std::string> what;
    for (const auto& p : plans) what.push_back(p.display_name);
    std::cout << "mago: wrote " << out.string() << "  (" << target << ": " << join(what) << ")\n";

    if (target == "nsis") {
        // The receipt is a sidecar rather than a string baked into the script:
        // it has to survive onto the machine as readable JSON, and escaping a
        // whole document through NSIS string literals is a way to lose it.
        const fs::path receipt_path = out.parent_path() / nsis::kReceiptName;
        std::ofstream r(receipt_path, std::ios::binary);
        if (!r) throw UsageError("cannot write " + receipt_path.string());
        r << nsis::receipt(plans, args.opt("--suite-name", "Void Suite"));
        std::cout << "      wrote " << receipt_path.string() << "\n"
                  << "      compile it with:  makensis " << out.string() << "\n";
    }
    return 0;
}

// ── report: what a fork maintainer or a contributor pastes into an issue ─────
//
// okf/concepts/audiences.md. An application's own report (`voidhormiga-cli
// report`) knows one binary. A developer's problem is usually a COMBINATION: this
// fork, on that Void Core, with this Maiz. So this prints the whole stack as the
// registry sees it on this machine -- names and versions only, never a path or a
// user -- and the tracker to take it to, upstream's as well when it is a fork.
const char* host_platform() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__)
    return "macos-x64";
#elif defined(__linux__)
    return "linux-x64";
#else
    return "unknown";
#endif
}

std::string tracker_of(const Manifest& m) {
    if (!m.contribute.issues.empty()) return m.contribute.issues;
    if (m.repository.empty()) return "";
    std::string r = m.repository;
    while (!r.empty() && r.back() == '/') r.pop_back();
    if (r.size() > 4 && r.compare(r.size() - 4, 4, ".git") == 0) r.resize(r.size() - 4);
    return r + "/issues/new/choose";
}

int cmd_report(const Registry& reg, const Args& args) {
    if (args.positional.empty()) throw UsageError("report needs a project name");
    const Manifest* m = reg.get(args.positional[0]);
    if (!m) throw UsageError("no project named \"" + args.positional[0] + "\" in this registry");
    std::cout << "Stack, as built on this machine:\n\n";
    std::cout << "- " << m->display_name << " " << m->version.str()
              << (m->fork_of.empty() ? "" : "  (a fork of " + m->fork_of + ")") << "\n";
    for (const auto& req : m->requirements) {
        const Manifest* d = reg.get(req.name);
        std::cout << "- " << (d ? d->display_name : req.name) << " "
                  << (d ? d->version.str() : std::string(req.optional ? "absent (optional)" : "ABSENT"))
                  << "  (requires " << req.version.text() << ")\n";
    }
    std::cout << "- Platform: " << host_platform() << "\n\n";
    const std::string here = tracker_of(*m);
    if (!here.empty()) std::cout << "Report it here: " << here << "\n";
    if (!m->fork_of.empty()) {
        Manifest up;
        up.repository = m->fork_of;
        std::cout << "If it happens on upstream too, report it there as well: " << tracker_of(up) << "\n"
                  << "  (with steps that do not need your fork's changes)\n";
    }
    std::cout << "\nKeep out of any report: real people's names, emails, phone numbers and\n"
                 "addresses, an organization's database or logs, keys, and paths on your computer.\n";
    return 0;
}

// ── learn: the family contributor's first afternoon ─────────────────────────
//
// okf/concepts/audiences.md, person 3. Somebody about to change a Void project
// needs its vocabulary and its design before its code. FaultSack (a sibling
// application, never a dependency) turns a project's OKF and code into a study
// site and a book; this runs it when it is installed and says how to get it when
// it is not, then gives the reading order: the project, then each library it
// stands on, in the order it depends on them.
int cmd_learn(const Registry& reg, const Args& args) {
    if (args.positional.empty()) throw UsageError("learn needs a project name");
    const Manifest* m = reg.get(args.positional[0]);
    if (!m) throw UsageError("no project named \"" + args.positional[0] + "\" in this registry");

    std::cout << "Learning " << m->display_name << "\n\n1. Read, in this order:\n";
    std::error_code ec;
    const auto line = [&](const Manifest& p, const std::string& rel) {
        if (fs::exists(p.path / rel, ec)) std::cout << "     " << (p.path / rel).string() << "\n";
    };
    if (!m->contribute.guide.empty()) line(*m, m->contribute.guide);
    line(*m, "okf/index.md");
    for (const auto& req : m->requirements)
        if (const Manifest* d = reg.get(req.name)) line(*d, "okf/index.md");

    const fs::path out = args.opt("--into").empty() ? fs::temp_directory_path() / ("mago-learn-" + m->name)
                                                    : fs::path(args.opt("--into"));
    std::string text;
    const bool have = run_program({"faultsack", "--help"}, text) == 0;
    std::cout << "\n2. Study it with FaultSack";
    if (!have) {
        std::cout << " (not installed here). FaultSack turns a project's OKF and code into\n"
                     "   flashcards, quizzes, a module map and a printable book.\n";
        if (const Manifest* fs_ = reg.get("faultsack")) {
            std::cout << "   There is a checkout at " << fs_->path.string() << ": follow its README's Setup,\n"
                      << "   then run `mago learn " << m->name << "` again.\n";
        } else {
            // its manifest names a repository that does not exist yet (2026-10-25): say so
            std::cout << "   It has no public release yet; ask a maintainer for it. Until then, read.\n";
        }
        return 0;
    }
    std::cout << ": building the study site in " << out.string() << " ...\n";
    text.clear();
    if (run_program({"faultsack", "analyze", m->path.string(), "--out", out.string(), "--name", m->display_name},
                    text) != 0) {
        std::cout << "   FaultSack could not analyse it:\n" << text;
        return 1;
    }
    std::cout << "   done. Open it with:  faultsack serve --out " << out.string() << "\n"
              << "   and the book with:   faultsack book " << m->path.string() << " --with-mago\n";
    return 0;
}

// ── upstream: how far a fork's base is from upstream's releases ─────────────
//
// okf/concepts/audiences.md, person 2. Read from upstream's tags, so it works
// without cloning upstream at all.
int cmd_upstream(const Registry& reg, const Args& args) {
    if (args.positional.empty()) throw UsageError("upstream needs a project name");
    const Manifest* m = reg.get(args.positional[0]);
    if (!m) throw UsageError("no project named \"" + args.positional[0] + "\" in this registry");
    const std::string up = !m->fork_of.empty() ? m->fork_of : m->repository;
    if (up.empty()) throw UsageError(m->name + " names neither fork_of nor repository");
    auto git = system_git();
    std::string err;
    const auto tags = git->tags(up, err);
    if (!err.empty()) throw UsageError("could not read " + up + ": " + err);
    std::vector<Version> newer;
    std::optional<Version> newest;
    for (const auto& t : tags) {
        const auto v = tag_version(t);
        if (!v) continue;
        if (!newest || *v > *newest) newest = v;
        if (*v > m->version) newer.push_back(*v);
    }
    std::sort(newer.begin(), newer.end());
    std::cout << m->display_name << " here: " << m->version.str()
              << (m->fork_of.empty() ? "" : "  (a fork of " + m->fork_of + ")") << "\n"
              << "upstream's newest release: " << (newest ? newest->str() : std::string("none tagged")) << "\n";
    if (newer.empty()) {
        std::cout << "nothing newer upstream.\n";
        return 0;
    }
    std::cout << newer.size() << " release(s) newer than this one:";
    for (const auto& v : newer) std::cout << " " << v.str() << (breaks_compat(m->version, v) ? " (breaking)" : "");
    std::string base = up;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".git") == 0) base.resize(base.size() - 4);
    std::cout << "\nread each one's behavior changes before merging: " << base << "/releases\n";
    return 0;
}

// ── bootstrap: a project and its family onto a disk that has nothing ────────
//
// The argument is a repository URL or a checkout already on disk. A URL is
// cloned first, with full history, because the target is the one a contributor
// works in; everything it requires lands beside it at the sibling paths its
// manifest names. See include/mago/bootstrap.hpp for the version rule.
int cmd_bootstrap(const Args& args) {
    if (args.positional.empty())
        throw UsageError("bootstrap needs a repository URL or a checkout path");
    const std::string what = args.positional[0];
    const bool is_url = what.find("://") != std::string::npos || what.rfind("git@", 0) == 0;
    auto git = system_git();
    std::error_code ec;
    fs::path target;
    if (is_url) {
        std::string base = what;
        while (!base.empty() && base.back() == '/') base.pop_back();
        base = base.substr(base.find_last_of("/:") + 1);
        if (base.size() > 4 && base.compare(base.size() - 4, 4, ".git") == 0) base.resize(base.size() - 4);
        const fs::path into = args.opt("--into").empty() ? fs::current_path() : fs::path(args.opt("--into"));
        target = into / base;
        if (!fs::exists(target, ec)) {
            if (args.has("--dry-run")) {
                std::cout << "mago: would clone " << what << " into " << target.string()
                          << "; its requirements are read once it is there\n";
                return 0;
            }
            fs::create_directories(into, ec);
            std::string err;
            if (!git->clone(what, "", target, /*shallow=*/false, err))
                throw UsageError("could not clone " + what + ": " + err);
            std::cout << "cloned   " << what << " -> " << target.string() << "\n";
        }
    } else {
        target = fs::absolute(fs::path(what), ec);
    }
    if (!args.opt("--into").empty() && !is_url)
        throw UsageError("--into applies to a URL; a checkout's siblings go beside it");

    BootstrapOptions opt;
    opt.dry_run = args.has("--dry-run");
    opt.shallow_deps = !args.has("--full");
    const BootstrapResult r = bootstrap(target, *git, opt);

    static const char* kWord[] = {"clone", "present", "absent", "FAILED"};
    for (const auto& s : r.steps) {
        const char* word = kWord[static_cast<int>(s.action)];
        std::cout << word << std::string(9 - std::string(word).size(), ' ') << s.name
                  << (s.optional ? " (optional)" : "") << "  " << (s.ref.empty() ? "default branch" : s.ref)
                  << "  -> " << s.dir.string() << "\n";
        for (const auto& n : s.notes) std::cout << "         " << n << "\n";
    }
    if (r.steps.empty()) std::cout << "nothing to lay out: it requires no Void project\n";
    std::cout << (r.ok ? "\nthe family is laid out. Build each dependency the way its README says, then "
                         "this project.\n"
                       : "\nnot everything could be laid out; see FAILED and the notes above.\n");
    return r.ok ? 0 : 1;
}

// The single exit through which every command's code passes.
int done(int code) { return code != 0 ? code : (g_unread_manifests > 0 ? 1 : 0); }

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        // An old binary has to be able to say so: Void Hormiga staged a
        // release with a stale mago that nothing identified (2026-10-18).
        if (args.command == "version" || args.has("--version")) {
            std::cout << "mago " << MAGO_VERSION_STR << "\n";
            return 0;
        }
        if (args.command.empty() || args.has("--help")) {
            std::cout << kUsage;
            return args.command.empty() && !args.has("--help") ? 2 : 0;
        }

        if (args.command == "register" || args.command == "roots" ||
            args.command == "forget") {
            return cmd_roots(args, argv[0]);
        }
        if (args.command == "graph") return done(cmd_graph(load_registry(args, argv[0])));
        if (args.command == "scan") return done(cmd_scan(load_registry(args, argv[0]), args));
        if (args.command == "doctor") {
            const Registry reg = load_registry(args, argv[0]);
            return done(cmd_doctor(reg, args, Family::load(family_path(reg, args))));
        }
        if (args.command == "plan") return done(cmd_plan(load_registry(args, argv[0]), args));
        if (args.command == "blast") return done(cmd_blast(load_registry(args, argv[0]), args));
        if (args.command == "notify") return done(cmd_notify(load_registry(args, argv[0]), args));
        if (args.command == "vendors") return done(cmd_vendors(load_registry(args, argv[0]), args));
        if (args.command == "notices") return done(cmd_notices(load_registry(args, argv[0]), args));
        if (args.command == "matrix" || args.command == "pair") {
            const Registry reg = load_registry(args, argv[0]);
            const fs::path path = family_path(reg, args);
            Family family = Family::load(path);
            if (args.command == "matrix") return done(cmd_matrix(reg, args, family));
            return done(cmd_pair(reg, args, family, path));
        }
        if (args.command == "stage") return done(cmd_stage(load_registry(args, argv[0]), args));
        if (args.command == "feed") return done(cmd_feed(load_registry(args, argv[0]), args));
        if (args.command == "wizard") return done(cmd_wizard(load_registry(args, argv[0]), args));
        if (args.command == "bootstrap") return cmd_bootstrap(args);
        if (args.command == "report") return cmd_report(load_registry(args, argv[0]), args);
        if (args.command == "learn") return cmd_learn(load_registry(args, argv[0]), args);
        if (args.command == "upstream") return cmd_upstream(load_registry(args, argv[0]), args);

        std::cerr << "mago: unknown command \"" << args.command << "\"\n\n" << kUsage;
        return 2;
    } catch (const UsageError& e) {
        std::cerr << "mago: " << e.what() << "\n";
        return 2;
    } catch (const ResolveError& e) {
        std::cerr << "mago: " << e.what() << "\n";
        return 1;
    } catch (const ManifestError& e) {
        std::cerr << "mago: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "mago: " << e.what() << "\n";
        return 1;
    }
}
