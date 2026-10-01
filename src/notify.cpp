#include "mago/notify.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <sstream>

#include "mago/json_out.hpp"

namespace fs = std::filesystem;

namespace mago {
namespace {

std::string join(const std::vector<std::string>& xs, const char* sep = ", ") {
    std::string out;
    for (const auto& x : xs) {
        if (!out.empty()) out += sep;
        out += x;
    }
    return out;
}

std::string pad(std::string s, std::size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

// A filename slug: lowercase, and anything that is not a letter, a digit or a
// dot becomes a hyphen. Message filenames land in other people's repositories
// and get typed by hand, so they may not carry a space.
std::string slug(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == '.') {
            out += '.';
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// The sender's own path to a document, rewritten from where the recipient
// stands. Whoever reads the message is sitting in their own repository, so
// ../VoidCore/SPEC.md is the useful form and SPEC.md is a wrong one.
std::string from_there(const fs::path& sender_dir, const fs::path& recipient_dir,
                       const std::string& document) {
    std::error_code ec;
    const fs::path rel = fs::relative(sender_dir, recipient_dir, ec);
    if (ec || rel.empty()) {
        return document.empty() ? sender_dir.generic_string()
                                : (sender_dir / document).generic_string();
    }
    return document.empty() ? rel.generic_string() : (rel / document).generic_string();
}

const char* verdict_headline(Verdict v) {
    switch (v) {
        case Verdict::Blocked:
            return "the version you pin does not admit this one; fix the constraint first";
        case Verdict::Rebuild: return "build it again";
        case Verdict::Reship: return "ship the new file, do not rebuild";
        case Verdict::Retest: return "re-run the contract, nothing rebuilds";
        case Verdict::Untouched: return "nothing to do";
    }
    return "";
}

}  // namespace

std::string today() {
    const auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%d}", now);
}

Broadcast gather(const Registry& reg, const std::string& project, std::optional<Version> to,
                 std::optional<Change> change) {
    const Manifest& sender = reg.at(project);

    Broadcast b;
    b.project = sender.name;
    b.display_name = sender.display_name;
    b.report = analyse(reg, project, to, change);
    b.from = b.report.from_version;
    b.to = b.report.to_version;
    b.change = b.report.change;
    b.declared = b.report.from_release;

    const Release& rel = sender.release;
    if (rel.declared) {
        b.summary = rel.summary;
        b.date = rel.date;
        b.adds = rel.adds;
        b.behavior_changes = rel.behavior_changes;
        for (const auto& doc : rel.read_more) b.read_more.emplace_back("", doc);
    }
    // The contracts a manifest already names. This is item (5), and it is
    // mechanical exactly to the extent that a project wrote them down. A
    // release that already pointed at SPEC.md does not get to point at it
    // twice because `provides.contracts` also names it.
    for (const auto& [label, doc] : sender.contracts) {
        const bool already = std::any_of(b.read_more.begin(), b.read_more.end(),
                                         [&](const auto& e) { return e.second == doc; });
        if (!already) b.read_more.emplace_back(label, doc);
    }
    return b;
}

std::vector<Message> compose(const Registry& reg, const Broadcast& b, const NotifyOptions& opt) {
    const Manifest& sender = reg.at(b.project);
    const std::string date = opt.date.empty() ? today() : opt.date;
    const std::string topic =
        opt.topic.empty()
            ? slug(b.project + "-" + (b.to ? b.to->str() : sender.version.str()))
            : slug(opt.topic);

    std::vector<Message> out;
    for (const Impact& impact : b.report.impacts) {
        if (impact.verdict == Verdict::Untouched && !opt.include_untouched) continue;
        const Manifest* recipient = reg.get(impact.project);
        if (!recipient) continue;

        Message m;
        m.to = impact.project;
        m.display_name = impact.display_name;
        m.verdict = impact.verdict;
        m.reason = impact.reason;
        m.path = recipient->path /
                 ("MESSAGE_FOR_" + upper(impact.project) + "_" + topic + "-" + date + ".md");

        const std::string here = from_there(sender.path, recipient->path, "");

        std::ostringstream s;
        s << "# " << b.display_name << " " << (b.to ? b.to->str() : b.from.str());
        if (!b.summary.empty()) s << " -- " << b.summary;
        s << "\n\n";
        s << "**From:** " << b.display_name << " (`" << here << "`), " << date << "  \n";
        s << "**Release:** " << b.from.str() << " -> " << (b.to ? b.to->str() : b.from.str())
          << ", **" << change_name(b.change) << "**"
          << (b.declared ? " -- declared by the release, not inferred from the numbers" : "")
          << "  \n";
        s << "**Your verdict:** **" << upper(verdict_name(impact.verdict)) << "** -- "
          << verdict_headline(impact.verdict) << "\n\n";
        s << "---\n\n";

        s << "## 1. Why you got this verdict\n\n";
        s << impact.display_name << " " << impact.reason << ".\n\n";
        if (impact.path.size() > 1) {
            s << "The edge Mago followed: `" << impact.through() << "`.\n\n";
        }

        if (!b.behavior_changes.empty()) {
            s << "## 2. Behavior changes -- the part no verdict can carry\n\n";
            s << "Nothing below changed a signature or violated a version constraint, so the "
                 "verdict above stays true and your code can still break. This is the category "
                 "a dependency resolver structurally cannot see.\n\n";
            for (const auto& bc : b.behavior_changes) {
                s << "- **" << bc.what << "**";
                if (!bc.who_is_affected.empty()) s << "  \n  *Affects:* " << bc.who_is_affected;
                s << "\n";
            }
            s << "\n";
        }

        if (!b.adds.empty()) {
            s << "## 3. New optional surface\n\n";
            s << "Nobody has to adopt any of it, and the verdict above does not change if you "
                 "ignore it.\n\n";
            for (const auto& add : b.adds) s << "- `" << add << "`\n";
            s << "\n";
        }

        // The hole -- and explicitly labelled as one. A generated message that
        // quietly omitted the most valuable section would be worse than no
        // message, because its absence would read as "there was nothing".
        s << "## 4. What this means for " << impact.display_name << " specifically\n\n";
        s << "<!-- mago: this section is the one part of a broadcast that is not derivable\n"
             "     from a manifest, and it was the most valuable part of every message this\n"
             "     family has sent by hand. Mago leaves the hole rather than filling it\n"
             "     badly. Write it, or delete the heading. -->\n\n";
        s << "_Not yet written._\n\n";

        if (!b.read_more.empty()) {
            s << "## 5. Where to read the rest\n\n";
            for (const auto& [label, doc] : b.read_more) {
                s << "- ";
                if (!label.empty()) s << "**" << label << "** -- ";
                s << "`" << from_there(sender.path, recipient->path, doc) << "`\n";
            }
            s << "\n";
        }

        s << "---\n\n";
        s << "Replies in the usual channel: a `MESSAGE_FOR_" << upper(b.project)
          << "_<topic>-<date>.md` in `" << here << "`.\n\n";
        s << "Sections 1, 2, 3 and 5 were computed by `mago notify " << b.project
          << "` from the root manifests. Section 4 is a person's.\n";

        m.body = s.str();
        out.push_back(std::move(m));
    }
    return out;
}

std::string render(const Registry& reg, const Broadcast& b, const std::vector<Message>& msgs) {
    (void)reg;
    std::ostringstream s;
    s << b.display_name << " " << b.from.str() << " -> " << (b.to ? b.to->str() : b.from.str())
      << "   " << change_name(b.change)
      << (b.declared ? "   (declared by the release block)" : "   (inferred from the version)")
      << "\n";
    if (!b.summary.empty()) s << b.summary << "\n";
    s << "\n";

    s << "the verdict                     mechanical, from the manifests\n";
    for (const Verdict v : {Verdict::Blocked, Verdict::Rebuild, Verdict::Reship, Verdict::Retest,
                            Verdict::Untouched}) {
        const std::vector<Impact> group = b.report.by_verdict(v);
        if (group.empty()) continue;
        std::vector<std::string> names;
        for (const auto& i : group) names.push_back(i.display_name);
        s << "  " << pad(upper(verdict_name(v)), 12) << join(names) << "\n";
    }
    s << "\n";

    if (!b.behavior_changes.empty()) {
        s << "behavior changes (" << b.behavior_changes.size()
          << ")            what a resolver cannot see\n";
        for (const auto& bc : b.behavior_changes) {
            s << "  * " << bc.what << "\n";
            if (!bc.who_is_affected.empty()) s << "    affects: " << bc.who_is_affected << "\n";
        }
        s << "\n";
    }

    if (!b.adds.empty()) {
        s << "new optional surface (" << b.adds.size() << ")        nobody must adopt it\n  "
          << join(b.adds) << "\n\n";
    }

    if (!b.read_more.empty()) {
        s << "where to read the rest\n";
        for (const auto& [label, doc] : b.read_more) {
            s << "  " << pad(label.empty() ? "-" : label, 18) << doc << "\n";
        }
        s << "\n";
    }

    if (!b.report.unverified.empty()) {
        s << "cannot address (" << b.report.unverified.size()
          << ")              no manifest here: no verdict, no message\n";
        for (const auto& u : b.report.unverified) {
            s << "  " << pad(u.name, 18) << u.how << "\n";
        }
        s << "\n";
    }

    if (msgs.empty()) {
        s << "Nothing downstream has anything to do. Release it.\n";
        return s.str();
    }

    s << msgs.size() << (msgs.size() == 1 ? " message" : " messages") << ", none written.\n";
    for (const auto& m : msgs) {
        s << "  " << pad(m.display_name, 18) << m.path.filename().string() << "\n";
    }
    s << "\n--write puts each one in its own project's root.\n"
         "Section 4 of every message -- what this release means for that project in\n"
         "particular -- is left as a marked hole. It is the part that does not scale,\n"
         "and the part worth the most.\n";
    return s.str();
}

std::string to_json(const Broadcast& b, const std::vector<Message>& msgs, int indent) {
    JsonWriter w(indent);
    w.begin_object();
    w.field("report", "mago-notify/0.1");
    w.field("project", b.project);
    w.field("display_name", b.display_name);
    w.key("from").value(b.from.str());
    w.key("to");
    if (b.to) {
        w.value(b.to->str());
    } else {
        w.null_value();
    }
    w.field("change", change_name(b.change));
    w.key("declared_by_release").value(b.declared);
    w.field("summary", b.summary);
    w.string_array("adds", b.adds);

    w.key("behavior_changes").begin_array();
    for (const auto& bc : b.behavior_changes) {
        w.begin_object();
        w.field("what", bc.what);
        w.field("who_is_affected", bc.who_is_affected);
        w.end_object();
    }
    w.end_array();

    w.key("read_more").begin_array();
    for (const auto& [label, doc] : b.read_more) {
        w.begin_object();
        w.field("label", label);
        w.field("document", doc);
        w.end_object();
    }
    w.end_array();

    w.key("messages").begin_array();
    for (const auto& m : msgs) {
        w.begin_object();
        w.field("to", m.to);
        w.field("display_name", m.display_name);
        w.field("verdict", verdict_name(m.verdict));
        w.field("reason", m.reason);
        w.field("path", m.path.string());
        w.end_object();
    }
    w.end_array();

    w.key("unverified").begin_array();
    for (const auto& u : b.report.unverified) {
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

}  // namespace mago
