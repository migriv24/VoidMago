#include "mago/semver.hpp"

#include "toy_family.hpp"

using namespace mago;

namespace {

Version V(const char* s) {
    const auto v = Version::parse(s);
    CHECK(v.has_value());
    return *v;
}

Range R(const char* s) {
    const auto r = Range::parse(s);
    CHECK(r.has_value());
    return *r;
}

void below_one_the_minor_is_the_break() {
    CHECK(breaks_compat(V("0.2.12"), V("0.3.0")));
    CHECK(!breaks_compat(V("0.2.7"), V("0.2.12")));
}

void at_or_above_one_the_major_is_the_break() {
    CHECK(!breaks_compat(V("1.2.0"), V("1.9.9")));
    CHECK(breaks_compat(V("1.9.9"), V("2.0.0")));
}

void ranges() {
    CHECK(R("*").allows(V("0.0.1")));
    CHECK(R(">=0.2.7").allows(V("0.2.12")));
    CHECK(!R(">=0.2.12").allows(V("0.2.7")));
    CHECK(R("^0.2.7").allows(V("0.2.12")));
    CHECK(!R("^0.2.7").allows(V("0.3.0")));
    CHECK(R("~1.2.0").allows(V("1.2.9")));
    CHECK(!R("~1.2.0").allows(V("1.3.0")));
    CHECK(R("1.0.0").allows(V("1.0.0")));
    CHECK(!R("1.0.0").allows(V("1.0.1")));
}

// ">=" must never be read as ">" with a stray character left over.
void the_longest_operator_wins() {
    CHECK(R(">=1.0.0").allows(V("1.0.0")));
    CHECK(!R(">1.0.0").allows(V("1.0.0")));
    CHECK(R("<=1.0.0").allows(V("1.0.0")));
}

void malformed_input_is_refused_rather_than_guessed_at() {
    CHECK(!Version::parse("1.2"));
    CHECK(!Version::parse("1.2.x"));
    CHECK(!Version::parse(""));
    CHECK(!Version::parse("v1.2.3"));
    CHECK(!Range::parse(">=nonsense"));
    CHECK(Version::parse("1.2.3-rc1").has_value());  // a tag is ignored, not fatal
}

void the_strictest_floor_binds() {
    CHECK_EQ(strictest({R("*"), R(">=0.2.7"), R(">=0.2.12")}).text(), std::string(">=0.2.12"));
    CHECK_EQ(strictest({R("*"), R("*")}).text(), std::string("*"));
    CHECK_EQ(strictest({}).text(), std::string("*"));
    // An upper bound is not a floor, so it never binds one.
    CHECK_EQ(strictest({R("<2.0.0")}).text(), std::string("*"));
}

void round_trip() {
    CHECK_EQ(V("0.2.12").str(), std::string("0.2.12"));
    CHECK(V("1.0.0") < V("1.0.1"));
    CHECK(V("1.9.0") < V("2.0.0"));
}

}  // namespace

int main() {
    below_one_the_minor_is_the_break();
    at_or_above_one_the_major_is_the_break();
    ranges();
    the_longest_operator_wins();
    malformed_input_is_refused_rather_than_guessed_at();
    the_strictest_floor_binds();
    round_trip();
    mago::test::pass("semver");
    return 0;
}
