// Version arithmetic for the Void family.
//
// Deliberately small. The family uses plain three-part versions and a handful
// of range forms; a full semver implementation would be more machinery than
// there are versions to compare.
//
// The one rule worth naming: BELOW 1.0, THE MINOR POSITION IS THE BREAK.
// Void Core says so itself -- "the C ABI is additive-only within a 0.2.x line"
// -- so 0.2.7 -> 0.2.12 is compatible and 0.2.x -> 0.3.0 is not.

#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mago {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const Version&) const = default;

    static std::optional<Version> parse(std::string_view text);
    std::string str() const;

    // The versions that promise not to break each other. 1.4.2 and 1.9.0
    // share a line; 0.2.7 and 0.3.0 do not.
    bool same_line_as(const Version& other) const;
};

// Would moving from `from` to `to` break a consumer that built against `from`?
bool breaks_compat(const Version& from, const Version& to);

// One version constraint, as written in a `requires[].version` field.
class Range {
public:
    Range() = default;  // "*" -- admits anything

    // Returns nullopt on syntax this cannot represent, rather than guessing.
    static std::optional<Range> parse(std::string_view text);

    bool is_any() const { return any_; }
    bool allows(const Version& v) const;

    // The lowest version this range admits, if it has one.
    std::optional<Version> floor() const;

    const std::string& text() const { return text_; }

private:
    std::string text_ = "*";
    std::string op_;
    Version v_{};
    bool any_ = true;
};

// Of several constraints on one project, the one that binds.
//
// Only floors are compared, because every constraint the family has written so
// far is a floor or `*`. If that stops being true this needs a real interval
// intersection, and `mago doctor` should say so rather than guess.
Range strictest(const std::vector<Range>& ranges);

}  // namespace mago
