// The broadcast: what a release has to tell the rest of the family.
//
// `mago blast` answers one question — who is affected, and how badly — and
// that answer is mechanical, from the manifests. It is not a release message.
// Void Core shipped 0.2.14 by writing eight messages into eight repository
// roots by hand, and then wrote down what a verdict alone had failed to carry:
//
//   1. The verdict.                        mechanical, from the manifests
//   2. Behavior changes that are not ABI changes.   THE ONE THAT BITES
//   3. New optional surface.               nobody must adopt it; some will want to
//   4. Per-project relevance.              a human's, and always will be
//   5. Where to read the rest.             mechanical, if a manifest names its
//                                          contract documents
//
// Four of those five are arithmetic over files Mago already reads. This module
// produces them, and leaves (4) as a marked hole rather than pretending to it:
//
//     None of that is derivable from a manifest, and it was the most valuable
//     part of every message. It is also the part that does not scale — which
//     is the honest limit on how much of this Mago can ever do.
//
// (2) is the reason this exists at all. `link --weight now refuses a
// non-number` changed no signature and violated no version constraint, so
// every verdict in this file stays true and a consumer still breaks. A
// resolver cannot see that category. A notifier can carry it.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mago/blast.hpp"
#include "mago/registry.hpp"

namespace mago {

// Everything one release has to say, gathered from the sender's own manifest.
struct Broadcast {
    std::string project;
    std::string display_name;
    Version from;
    std::optional<Version> to;
    Change change = Change::Compatible;
    bool declared = false;  // did the release block supply the move, or a flag?

    std::string summary;
    std::string date;
    std::vector<std::string> adds;
    std::vector<BehaviorChange> behavior_changes;

    // `release.read_more` plus every document in `provides.contracts`, as
    // "spec" -> "SPEC.md". Paths are the sender's own; a message rewrites them
    // relative to its recipient.
    std::vector<std::pair<std::string, std::string>> read_more;

    BlastReport report;

    // Did the sender declare anything a verdict cannot carry? A release with
    // no behavior changes and no new surface has nothing to say that
    // `mago blast` did not already say.
    bool has_narrative() const {
        return !behavior_changes.empty() || !adds.empty() || !summary.empty();
    }
};

// One message, addressed to one project.
struct Message {
    std::string to;                    // recipient's machine name
    std::string display_name;
    Verdict verdict = Verdict::Untouched;
    std::string reason;
    std::filesystem::path path;        // where `--write` would put it
    std::string body;                  // markdown, ready to write
};

struct NotifyOptions {
    std::string topic;  // filename slug; defaults to <sender>-<version>
    std::string date;   // ISO date; defaults to today
    bool include_untouched = false;
};

// Gather the broadcast. `to` and `change` behave exactly as in `analyse`: with
// neither, the sender's `release` block supplies both.
Broadcast gather(const Registry& reg, const std::string& project,
                 std::optional<Version> to = std::nullopt,
                 std::optional<Change> change = std::nullopt);

// One message per project with a verdict, worst first. Untouched projects are
// included only when asked for -- "nothing changed for you" is worth sending
// deliberately and not by default.
std::vector<Message> compose(const Registry& reg, const Broadcast& b,
                             const NotifyOptions& opt = {});

// The terminal view: the whole broadcast at once, for a person about to
// release something.
std::string render(const Registry& reg, const Broadcast& b, const std::vector<Message>& msgs);

std::string to_json(const Broadcast& b, const std::vector<Message>& msgs, int indent = 2);

// Today, UTC, as YYYY-MM-DD. The family's message filenames end in one.
std::string today();

}  // namespace mago
