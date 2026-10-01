#include "mago/blast.hpp"

#include <algorithm>
#include <map>

#include "mago/json_out.hpp"

namespace mago {
namespace {

std::string join(const std::vector<std::string>& xs, const char* sep) {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

}  // namespace

const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Untouched: return "untouched";
        case Verdict::Retest: return "retest";
        case Verdict::Reship: return "reship";
        case Verdict::Rebuild: return "rebuild";
        case Verdict::Blocked: return "blocked";
    }
    return "?";
}

const char* change_name(Change c) {
    return c == Change::Breaking ? "breaking" : "compatible";
}

std::string Impact::through() const {
    return join(path, " -> ");
}

std::vector<Impact> BlastReport::by_verdict(Verdict v) const {
    std::vector<Impact> out;
    for (const auto& i : impacts) {
        if (i.verdict == v) out.push_back(i);
    }
    return out;
}

std::vector<Impact> BlastReport::needs_action() const {
    std::vector<Impact> out;
    for (const auto& i : impacts) {
        if (i.verdict != Verdict::Untouched) out.push_back(i);
    }
    return out;
}

std::string BlastReport::to_json(int indent) const {
    JsonWriter w(indent);
    w.begin_object();
    w.field("report", "mago-blast/0.1");
    w.field("changed", changed);
    w.key("from").value(from_version.str());
    w.key("to");
    if (to_version) {
        w.value(to_version->str());
    } else {
        w.null_value();
    }
    w.field("change", change_name(change));
    w.key("declared_by_release").value(from_release);
    w.key("impacts").begin_array();
    for (const auto& i : impacts) {
        w.begin_object();
        w.field("project", i.project);
        w.field("display_name", i.display_name);
        w.field("verdict", verdict_name(i.verdict));
        w.field("reason", i.reason);
        w.string_array("path", i.path);
        w.end_object();
    }
    w.end_array();
    w.key("unverified").begin_array();
    for (const auto& u : unverified) {
        w.begin_object();
        w.field("project", u.name);
        w.field("how", u.how);
        w.field("reason", u.reason);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

Change infer_change(const Version& from, const Version& to) {
    return breaks_compat(from, to) ? Change::Breaking : Change::Compatible;
}

Verdict cost_of_link(const std::string& link, Change change) {
    const bool breaking = change == Change::Breaking;
    if (link == "shared") return breaking ? Verdict::Rebuild : Verdict::Reship;
    if (link == "static" || link == "source" || link == "vendored") return Verdict::Rebuild;
    // Imported source: no build output on either side of the edge, so an
    // additive change is a test run. A breaking one is worse than a rebuild --
    // the consumer's own code has to be edited -- and rebuild is the verdict
    // that propagates, which is the property that matters here.
    if (link == "import") return breaking ? Verdict::Rebuild : Verdict::Retest;
    // No link declared: treat it as a contract and say so.
    return Verdict::Retest;
}

BlastReport analyse(const Registry& reg, const std::string& changed,
                    std::optional<Version> to_version, std::optional<Change> change) {
    const Manifest* origin = reg.get(changed);
    if (!origin) throw ManifestError("no manifest for \"" + changed + "\"");

    BlastReport report;
    report.changed = changed;
    report.from_version = origin->version;
    report.to_version = to_version;

    // Nothing on the command line, but the manifest declares what this release
    // did: take the move and the change kind from there. This is the one fact
    // a resolver cannot derive, and a release that writes it down is a release
    // that broadcasts itself.
    const Release& rel = origin->release;
    if (!to_version && !change && rel.declared && rel.previous) {
        report.from_version = *rel.previous;
        report.to_version = rel.version;
        report.from_release = true;
    }

    if (change) {
        report.change = *change;
    } else if (report.from_release && !rel.change.empty()) {
        report.change = rel.change == "breaking" ? Change::Breaking : Change::Compatible;
    } else if (report.to_version) {
        report.change = infer_change(report.from_version, *report.to_version);
    } else {
        report.change = Change::Compatible;
    }

    // Everything below asks "is this the version move itself?" by comparing
    // against `to_version`, so the local copy has to be the resolved one.
    to_version = report.to_version;

    std::map<std::string, Impact> impacts;

    // Keep the harshest verdict seen for a project. Returns true if it worsened.
    const auto record = [&](const std::string& name, Verdict verdict, std::string reason,
                            std::vector<std::string> path) -> bool {
        const auto it = impacts.find(name);
        if (it != impacts.end() && it->second.verdict >= verdict) return false;
        Impact impact;
        impact.project = name;
        impact.display_name = reg.at(name).display_name;
        impact.verdict = verdict;
        impact.reason = std::move(reason);
        impact.path = std::move(path);
        impacts[name] = std::move(impact);
        return true;
    };

    // Verdicts whose project ends up with a DIFFERENT BUILD OUTPUT, and so
    // have something of their own to push one hop further. A reship or a
    // retest leaves the project's binary byte-identical; nothing downstream
    // can be affected by a file that did not change.
    const auto propagates = [](Verdict v) {
        return v == Verdict::Rebuild || v == Verdict::Blocked;
    };

    const auto spread = [&](auto&& self, const std::string& upstream,
                            const std::vector<std::string>& path) -> void {
        for (const Manifest* consumer : reg.dependents_of(upstream)) {
            const Requirement* req = consumer->requirement(upstream);
            std::vector<std::string> here = path;
            here.push_back(consumer->name);

            // A version the consumer's own floor forbids is not an upgrade, it
            // is a break. Say so before talking about link kinds.
            if (to_version && upstream == changed && !req->version.allows(*to_version)) {
                const bool worsened =
                    record(consumer->name, Verdict::Blocked,
                           "pins " + upstream + " " + req->version.text() + ", which " +
                               to_version->str() + " does not satisfy",
                           here);
                if (worsened) self(self, consumer->name, here);
                continue;
            }

            Verdict verdict = cost_of_link(req->link, report.change);
            std::string why;
            if (req->link == "shared") {
                if (upstream != changed) {
                    // We only recurse past a rebuild, so `upstream` was itself
                    // rebuilt. A recompile does not by itself move an ABI, so
                    // the consumer takes the new file and stays as it is.
                    verdict = Verdict::Reship;
                    why = "takes a rebuilt " + upstream +
                          " as a shared library; ship the new binary, do not rebuild this";
                } else if (report.change == Change::Breaking) {
                    why = "links " + upstream +
                          " as a shared library across a breaking change; the ABI it was built "
                          "against is gone";
                } else {
                    verdict = Verdict::Reship;
                    why = "links " + upstream +
                          " as a shared library and the change is additive; drop in the new file";
                }
            } else if (req->is_compiled_in()) {
                why = "compiles " + upstream + " into its own binary" +
                      (req->via.empty() ? "" : " (reached through " + req->via + ")") +
                      "; a changed " + upstream + " is a changed " + consumer->name;
            } else if (req->is_imported()) {
                // Imported source: nothing is compiled and nothing ships, so
                // there is no build output to rebuild or re-ship. What binds
                // is the API, which makes an additive change a retest -- and a
                // breaking one worse than a rebuild, because the consumer's
                // own code has to be edited before it runs at all.
                if (report.change == Change::Breaking) {
                    why = "imports " + upstream +
                          "'s source at runtime across a breaking change; nothing links and "
                          "nothing rebuilds, but the code must be ported before it runs";
                } else {
                    why = "imports " + upstream +
                          "'s source at runtime and the change is additive; re-run its tests, "
                          "there is no binary to ship";
                }
            } else {
                why = "requires " + upstream +
                      " with no declared link; treat as a contract and re-run its tests, then "
                      "set `link` in the manifest";
            }

            if (record(consumer->name, verdict, why, here) && propagates(verdict)) {
                self(self, consumer->name, here);
            }
        }

        for (const auto& [neighbour, seam] : reg.seam_neighbours(upstream)) {
            if (neighbour->name == changed || impacts.count(neighbour->name)) continue;
            std::vector<std::string> here = path;
            here.push_back(neighbour->name);
            record(neighbour->name, Verdict::Retest,
                   "meets " + upstream + " at the " + (seam.empty() ? "undeclared" : seam) +
                       " seam and links nothing of it; re-run the contract, do not rebuild",
                   here);
        }
    };

    spread(spread, changed, {changed});

    // Partners the changed project names but Mago has never read. Their
    // verdicts are not "untouched"; they are unknown, and the difference is
    // the whole reason `mago doctor` checks manifests against each other.
    {
        std::map<std::string, Unverified> missing;
        for (const auto& req : origin->requirements) {
            if (reg.contains(req.name)) continue;
            missing.emplace(req.name,
                            Unverified{req.name, "requires",
                                       changed + " requires it " + req.version.text() +
                                           ", but it carries no manifest here"});
        }
        for (const auto& i : origin->interacts_with) {
            if (i.name == "*" || i.seam == kManifestName || reg.contains(i.name)) continue;
            missing.emplace(i.name,
                            Unverified{i.name, i.seam.empty() ? "seam" : i.seam,
                                       changed + " claims a " +
                                           (i.seam.empty() ? "seam" : i.seam) +
                                           " seam with it, and nothing checks that claim from "
                                           "the other side"});
        }
        for (auto& [_, u] : missing) report.unverified.push_back(std::move(u));
    }

    for (const auto& [name, m] : reg.projects()) {
        if (name == changed || impacts.count(name)) continue;
        impacts[name] = Impact{name, m.display_name, Verdict::Untouched, "no edge", {}};
    }

    for (auto& [_, impact] : impacts) report.impacts.push_back(std::move(impact));
    std::sort(report.impacts.begin(), report.impacts.end(),
              [](const Impact& a, const Impact& b) {
                  if (a.verdict != b.verdict) return a.verdict > b.verdict;  // worst first
                  if (a.path.size() != b.path.size()) return a.path.size() < b.path.size();
                  return a.project < b.project;
              });
    return report;
}

}  // namespace mago
