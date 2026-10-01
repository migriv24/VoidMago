// Read what is actually on disk.
//
// The deliberate opposite of the other suites: this one loads the real Void
// checkouts beside this repository. It asserts SHAPE and never versions, so a
// release of Void Core cannot turn it red -- but a manifest that stops parsing,
// or a Hormiga install that suddenly ships four runtimes instead of one, will.
//
// It skips itself when the siblings are not there, so the suite still passes in
// a bare clone.

#include <cstdlib>
#include <set>

#include "mago/blast.hpp"
#include "mago/resolve.hpp"
#include "toy_family.hpp"

using namespace mago;

namespace {

std::set<std::string> names(const std::vector<Resolved>& xs) {
    std::set<std::string> out;
    for (const auto& x : xs) out.insert(x.name);
    return out;
}

}  // namespace

int main() {
    const char* env = std::getenv("VOIDMAGO_FAMILY_ROOT");
    const std::filesystem::path root = env ? std::filesystem::path(env)
                                           : std::filesystem::current_path().parent_path();

    std::error_code ec;
    if (!std::filesystem::exists(root / "VoidCore" / "void.json", ec)) {
        std::cout << "skip  family (no Void checkouts beside this repo)\n";
        return 0;
    }

    const Registry reg = Registry::scan(root);

    // Every manifest on disk parses.
    for (const auto& f : reg.check()) {
        if (f.level == "error" && f.message.find("not valid JSON") != std::string::npos) {
            std::cerr << "FAIL a real manifest does not parse: " << f.message << "\n";
            return 1;
        }
    }
    CHECK(reg.contains("voidcore"));
    CHECK(reg.contains("voidhormiga"));

    // Hormiga builds five projects and ships exactly one runtime file: Void
    // Core's DLL. Maiz, Allomone and Palabra are compiled in.
    const Plan plan = resolve(reg, "voidhormiga", "windows-x64");
    std::set<std::string> shipped_runtimes;
    for (const auto& r : plan.payload) {
        if (r.kind == "runtime") shipped_runtimes.insert(r.name);
    }
    CHECK(shipped_runtimes == (std::set<std::string>{"voidcore"}));
    CHECK(names(plan.absorbed) ==
          (std::set<std::string>{"voidmaiz", "voidallomone", "voidpalabra"}));

    // An additive Void Core release rebuilds nothing. This is the claim the
    // whole project rests on, checked against the real graph.
    const Version core = reg.at("voidcore").version;
    const Version next{core.major, core.minor, core.patch + 1};
    const BlastReport report = analyse(reg, "voidcore", next);
    CHECK(report.change == Change::Compatible);
    CHECK(report.by_verdict(Verdict::Rebuild).empty());
    CHECK(!report.by_verdict(Verdict::Reship).empty());

    // A breaking one does not.
    const Version broken{core.major, core.minor + 1, 0};
    CHECK(!analyse(reg, "voidcore", broken).by_verdict(Verdict::Rebuild).empty());

    mago::test::pass("family");
    return 0;
}
