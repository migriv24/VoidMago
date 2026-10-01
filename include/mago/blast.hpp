// Blast radius: who is affected when one project changes, and how badly.
//
// The founding question:
//
//     i make an edit to void core, and it passes all the tests. but what if
//     that slightly effects something somewhere else?
//
// The answer is not "everything downstream must be rebuilt". It depends on HOW
// each consumer is bound to the thing that changed, and the manifests already
// record that in `requires[].link`. Four outcomes, cheapest first:
//
//   Untouched  no edge at all.
//   Retest     the consumer reaches it through a SEAM (a transcript, a
//              conformance case, a JSON document) rather than a binary.
//              Nothing rebuilds; the contract tests re-run.
//   Reship     a shared library, and the change stayed inside its compatible
//              line. Drop in the new file; the consumer's binary is untouched.
//   Rebuild    the code is compiled into the consumer (static/source), or the
//              change broke the line. The consumer must be built again, and
//              anything statically bound to IT inherits the same verdict.
//
// That last clause is the transitive part, and it is the reason this is worth
// a program rather than a paragraph.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mago/registry.hpp"

namespace mago {

// Ordered worst-last so a project reached by two paths takes the harsher
// verdict: comparing the enum values is comparing severity.
enum class Verdict {
    Untouched = 0,
    Retest = 1,
    Reship = 2,
    Rebuild = 3,
    Blocked = 4,
};

const char* verdict_name(Verdict v);

// What the change did to the CONTRACT, not merely to the version number.
enum class Change {
    Compatible,  // additive; existing callers keep working
    Breaking,    // the line moved; callers must be rebuilt
};

const char* change_name(Change c);

// Somebody the changed project NAMES, whom Mago cannot see.
//
// This exists because of the second thing Void Core's hand-rolled 0.2.14
// broadcast got wrong: three of its verdicts were about repositories nobody
// re-read, taken from Core's own description of their binding -- "we have no
// idea whether Reyna still emits transcripts in the shape we claim."
//
// A verdict Mago cannot compute must be VISIBLE rather than absent. An empty
// row reads as "not affected"; this reads as "not known", which is the truth.
struct Unverified {
    std::string name;
    std::string how;     // "requires" or the seam the changed project claims
    std::string reason;
};

struct Impact {
    std::string project;
    std::string display_name;
    Verdict verdict = Verdict::Untouched;
    std::string reason;
    std::vector<std::string> path;

    std::string through() const;
};

struct BlastReport {
    std::string changed;
    Version from_version;
    std::optional<Version> to_version;
    Change change = Change::Compatible;

    // Did the release block supply the version move and the change kind, or
    // did a person have to? The broadcast says which, because a verdict is
    // only as good as the fact it rests on.
    bool from_release = false;

    std::vector<Impact> impacts;
    std::vector<Unverified> unverified;

    std::vector<Impact> by_verdict(Verdict v) const;
    std::vector<Impact> needs_action() const;
    std::string to_json(int indent = 2) const;
};

// Classify a version move, using the family's own below-1.0 rule.
Change infer_change(const Version& from, const Version& to);

// What an upstream change costs ONE consumer bound this way -- the direct edge
// only, with no path behind it.
//
// This is the table the whole project rests on, so it is written once and read
// by both the walker and the pair matrix. Two copies of it would be two
// answers to "what does a shared link cost", and the second one to drift would
// be the one somebody was reading.
Verdict cost_of_link(const std::string& link, Change change);

// Walk outward from `changed` and give every affected project a verdict.
// `change` overrides what the version move implies, for when a version number
// lies -- which it eventually will, because a version is a promise not a proof.
//
// With neither argument, a `release` block in the changed project's own
// manifest supplies both: `previous` -> `version`, and the `change` its author
// declared. That is the whole point of the block, and it is why
// `mago blast voidcore` with no flags answers the question a release asks.
BlastReport analyse(const Registry& reg, const std::string& changed,
                    std::optional<Version> to_version = std::nullopt,
                    std::optional<Change> change = std::nullopt);

}  // namespace mago
