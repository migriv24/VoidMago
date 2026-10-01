#include "mago/semver.hpp"

#include <algorithm>
#include <cctype>

namespace mago {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// One run of digits, advancing `i` past it. nullopt if there are none.
std::optional<int> take_int(std::string_view s, std::size_t& i) {
    const std::size_t start = i;
    long long n = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        n = n * 10 + (s[i] - '0');
        if (n > 1000000000LL) return std::nullopt;  // a version, not a timestamp
        ++i;
    }
    if (i == start) return std::nullopt;
    return static_cast<int>(n);
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) {
    const std::string_view s = trim(text);
    std::size_t i = 0;

    const auto major = take_int(s, i);
    if (!major || i >= s.size() || s[i] != '.') return std::nullopt;
    ++i;
    const auto minor = take_int(s, i);
    if (!minor || i >= s.size() || s[i] != '.') return std::nullopt;
    ++i;
    const auto patch = take_int(s, i);
    if (!patch) return std::nullopt;

    // A trailing pre-release or build tag is accepted and ignored: nothing in
    // the family uses one yet, and refusing the file over it would be worse
    // than not ordering by it.
    if (i != s.size() && s[i] != '-' && s[i] != '+') return std::nullopt;

    return Version{*major, *minor, *patch};
}

std::string Version::str() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
}

bool Version::same_line_as(const Version& other) const {
    if (major != other.major) return false;
    // Below 1.0 the minor position carries the promise; at or above it, the
    // major does and the minor is free to move.
    return major != 0 || minor == other.minor;
}

bool breaks_compat(const Version& from, const Version& to) {
    return !from.same_line_as(to);
}

std::optional<Range> Range::parse(std::string_view text) {
    std::string_view s = trim(text);
    Range r;
    r.text_ = std::string(s);

    if (s.empty() || s == "*") {
        r.text_ = "*";
        return r;
    }

    // Longest operators first, so ">=" is never read as ">".
    static const char* kOps[] = {">=", "<=", "==", ">", "<", "=", "^", "~"};
    std::string op = "==";
    for (const char* candidate : kOps) {
        const std::string_view c(candidate);
        if (s.size() > c.size() && s.substr(0, c.size()) == c) {
            op = candidate;
            s.remove_prefix(c.size());
            break;
        }
    }

    const auto v = Version::parse(s);
    if (!v) return std::nullopt;

    r.any_ = false;
    r.op_ = op;
    r.v_ = *v;
    return r;
}

bool Range::allows(const Version& v) const {
    if (any_) return true;
    if (op_ == ">=") return v >= v_;
    if (op_ == ">") return v > v_;
    if (op_ == "<=") return v <= v_;
    if (op_ == "<") return v < v_;
    if (op_ == "==" || op_ == "=") return v == v_;
    if (op_ == "^") return v >= v_ && v.same_line_as(v_);
    if (op_ == "~") return v >= v_ && v.major == v_.major && v.minor == v_.minor;
    return false;
}

std::optional<Version> Range::floor() const {
    if (any_ || op_ == "<" || op_ == "<=") return std::nullopt;
    return v_;
}

Range strictest(const std::vector<Range>& ranges) {
    const Range* best = nullptr;
    std::optional<Version> best_floor;
    for (const auto& r : ranges) {
        const auto f = r.floor();
        if (!f) continue;
        if (!best_floor || *f > *best_floor) {
            best_floor = f;
            best = &r;
        }
    }
    return best ? *best : Range{};
}

}  // namespace mago
