// What a release has to say, and the one part of it Mago refuses to invent.

#include "mago/notify.hpp"

#include <string>

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

const Message* message_to(const std::vector<Message>& msgs, const std::string& project) {
    for (const auto& m : msgs) {
        if (m.to == project) return &m;
    }
    return nullptr;
}

// A family whose runtime declares what its release actually did.
void write_family_with_a_release(const TempDir& t) {
    t.project("core", R"("name":"core","display_name":"Core","kind":"runtime",
        "version":"1.2.0",
        "provides":{"artifacts":["libcore-win-x64.dll"],
                    "contracts":{"spec":"SPEC.md"}},
        "release":{
          "previous":"1.1.0",
          "change":"compatible",
          "summary":"three kinds and a split",
          "adds":["glyph","measure"],
          "behavior_changes":[
            {"what":"link --weight now refuses a non-number",
             "who_is_affected":"anything that relied on the silent zero"}],
          "read_more":["okf/concepts/quantity.md"]})");
    t.project("view", R"("name":"view","display_name":"View","kind":"runtime",
        "version":"0.4.0",
        "requires":[{"name":"core","version":">=1.1.0","link":"shared"}])");
    t.project("app", R"("name":"app","display_name":"App","kind":"application",
        "version":"2.0.0",
        "requires":[{"name":"core","version":">=1.0.0","link":"shared"}])");
}

// The point of the release block: the version move and the change kind stop
// being something a person types at the moment of broadcasting.
void a_release_block_answers_without_any_flags() {
    TempDir t;
    write_family_with_a_release(t);
    const Broadcast b = gather(Registry::scan(t.path()), "core");
    CHECK(b.declared);
    CHECK(b.from.str() == "1.1.0");
    CHECK(b.to && b.to->str() == "1.2.0");
    CHECK(b.change == Change::Compatible);
    CHECK(b.summary == "three kinds and a split");
}

// The category a resolver structurally cannot see. Every verdict below stays
// true and a consumer still breaks, so the message has to carry it.
void behavior_changes_reach_every_message() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const std::vector<Message> msgs = compose(reg, gather(reg, "core"));
    CHECK(msgs.size() == 2);
    for (const auto& m : msgs) {
        CHECK(contains(m.body, "link --weight now refuses a non-number"));
        CHECK(contains(m.body, "anything that relied on the silent zero"));
        CHECK(contains(m.body, "no verdict can carry"));
    }
}

// The shape that was silently dropped. Void Hormiga 0.1.1 wrote four behavior
// changes as strings, `mago feed` reported success, and the feed said
// `"behavior_changes": []` -- on the one field whose absence nothing else in
// the manifest can reveal. An object with no `what` was already refused by
// name; the easier mistake said nothing. Both are refusals now.
void a_behavior_change_of_the_wrong_shape_is_refused_rather_than_dropped() {
    TempDir t;

    // A string, which is the mistake this exists for.
    t.project("core", R"("name":"core","kind":"runtime","version":"1.2.0",
        "release":{"previous":"1.1.0","summary":"a fix that moves things",
          "behavior_changes":["times on the calendar grid move"]})");
    CHECK_THROWS(load(t.path() / "core" / "void.json"), ManifestError,
                 "release.behavior_changes[0] is a string");

    // The array itself written as one string -- the same mistake one level up.
    t.project("core", R"("name":"core","kind":"runtime","version":"1.2.0",
        "release":{"previous":"1.1.0","behavior_changes":"times move"})");
    CHECK_THROWS(load(t.path() / "core" / "void.json"), ManifestError,
                 "release.behavior_changes is a string");

    // The refusal that already existed, now carrying which entry.
    t.project("core", R"("name":"core","kind":"runtime","version":"1.2.0",
        "release":{"previous":"1.1.0","behavior_changes":[
          {"what":"grid times move"},{"who_is_affected":"everyone"}]})");
    CHECK_THROWS(load(t.path() / "core" / "void.json"), ManifestError,
                 "release.behavior_changes[1] has no \"what\"");

    // Absent and null stay absent: this refuses wrong shapes, not silence.
    t.project("core", R"("name":"core","kind":"runtime","version":"1.2.0",
        "release":{"previous":"1.1.0","behavior_changes":null})");
    CHECK(load(t.path() / "core" / "void.json").release.behavior_changes.empty());
}

// A manifest Mago could not read is a project missing from every answer that
// follows, so the registry carries the hole rather than only the absence.
void an_unreadable_manifest_is_a_finding_and_not_a_silence() {
    TempDir t;
    write_family_with_a_release(t);
    t.project("broken", R"("name":"broken","kind":"runtime","version":"0.1.0",
        "release":{"previous":"0.0.9","behavior_changes":["a string"]})");

    const Registry reg = Registry::scan(t.path());
    CHECK(!reg.contains("broken"));
    CHECK(reg.load_errors().size() == 1);
    CHECK(reg.load_errors().front().level == "error");
    CHECK(contains(reg.load_errors().front().message, "behavior_changes[0] is a string"));
    // And `mago doctor` says it rather than printing a shorter family.
    bool said = false;
    for (const auto& f : reg.check()) {
        if (contains(f.message, "behavior_changes[0] is a string")) said = true;
    }
    CHECK(said);
}

// New surface is advertised, and advertised as optional.
void new_surface_is_listed_and_nobody_is_told_to_adopt_it() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const std::vector<Message> msgs = compose(reg, gather(reg, "core"));
    CHECK(contains(msgs.front().body, "`glyph`"));
    CHECK(contains(msgs.front().body, "Nobody has to adopt"));
}

// The hole. Mago knows what it cannot compute and says so in the document
// rather than quietly shipping a message with a section missing.
void the_per_project_section_is_left_empty_and_labelled() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const auto msgs = compose(reg, gather(reg, "core"));  // m points into it
    const Message* m = message_to(msgs, "view");
    CHECK(m != nullptr);
    CHECK(contains(m->body, "What this means for View specifically"));
    CHECK(contains(m->body, "_Not yet written._"));
    CHECK(contains(m->body, "Mago leaves the hole rather than filling it"));
}

// Each message is addressed: it lands in the recipient's own root, and the
// verdict at the top is that recipient's, not a table of everyone's.
void a_message_is_addressed_to_one_project() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const std::vector<Message> msgs = compose(reg, gather(reg, "core"), {"topic", "2026-01-01"});
    const Message* m = message_to(msgs, "app");
    CHECK(m != nullptr);
    CHECK(m->path.parent_path() == reg.at("app").path);
    CHECK(m->path.filename().string() == "MESSAGE_FOR_APP_topic-2026-01-01.md");
    CHECK(m->verdict == Verdict::Reship);
    CHECK(contains(m->body, "**Your verdict:** **RESHIP**"));
    CHECK(!contains(m->body, "View"));  // App's message is about App
}

// A document the sender names is rewritten from where the reader stands. A
// message that says `SPEC.md` is telling a reader in another repository to
// open a file they do not have.
void where_to_read_the_rest_is_relative_to_the_recipient() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const auto msgs = compose(reg, gather(reg, "core"));  // m points into it
    const Message* m = message_to(msgs, "app");
    CHECK(m != nullptr);
    CHECK(contains(m->body, "../core/SPEC.md"));
    CHECK(contains(m->body, "../core/okf/concepts/quantity.md"));
}

// "Nothing changed for you" is worth sending deliberately, not by default.
void untouched_projects_are_not_written_to_unless_asked() {
    TempDir t;
    write_family_with_a_release(t);
    t.project("elsewhere", R"("name":"elsewhere","display_name":"Elsewhere",
        "kind":"application","version":"1.0.0")");
    const Registry reg = Registry::scan(t.path());
    const Broadcast b = gather(reg, "core");
    CHECK(message_to(compose(reg, b), "elsewhere") == nullptr);

    NotifyOptions all;
    all.include_untouched = true;
    const auto msgs = compose(reg, b, all);  // m points into it
    const Message* m = message_to(msgs, "elsewhere");
    CHECK(m != nullptr);
    CHECK(m->verdict == Verdict::Untouched);
    CHECK(contains(m->body, "nothing to do"));
}

// A project that declares no release still gets a broadcast; it just has
// nothing to say that `mago blast` had not already said.
void a_project_without_a_release_block_still_works() {
    TempDir t;
    mago::test::write_toy(t);
    const Registry reg = Registry::scan(t.path());
    const Broadcast b = gather(reg, "core", Version::parse("1.3.0"));
    CHECK(!b.declared);
    CHECK(!b.has_narrative());
    CHECK(b.to && b.to->str() == "1.3.0");
    CHECK(!compose(reg, b).empty());
}

void the_json_carries_the_same_facts() {
    TempDir t;
    write_family_with_a_release(t);
    const Registry reg = Registry::scan(t.path());
    const Broadcast b = gather(reg, "core");
    const std::string json = to_json(b, compose(reg, b));
    CHECK(contains(json, "\"report\": \"mago-notify/0.1\""));
    CHECK(contains(json, "\"declared_by_release\": true"));
    CHECK(contains(json, "\"who_is_affected\""));
    CHECK(contains(json, "\"verdict\": \"reship\""));
}

}  // namespace

int main() {
    a_release_block_answers_without_any_flags();
    behavior_changes_reach_every_message();
    a_behavior_change_of_the_wrong_shape_is_refused_rather_than_dropped();
    an_unreadable_manifest_is_a_finding_and_not_a_silence();
    new_surface_is_listed_and_nobody_is_told_to_adopt_it();
    the_per_project_section_is_left_empty_and_labelled();
    a_message_is_addressed_to_one_project();
    where_to_read_the_rest_is_relative_to_the_recipient();
    untouched_projects_are_not_written_to_unless_asked();
    a_project_without_a_release_block_still_works();
    the_json_carries_the_same_facts();
    mago::test::pass("notify");
    return 0;
}
