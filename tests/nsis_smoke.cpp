#include "mago/emit_nsis.hpp"

#include <algorithm>
#include <sstream>
#include <string>

#include "toy_family.hpp"

using namespace mago;
using mago::test::TempDir;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<Plan> toy_plans(const TempDir& t) {
    mago::test::write_toy(t);
    return {resolve(Registry::scan(t.path()), "app", "windows-x64")};
}

void the_script_ships_shared_libraries_and_not_static_ones() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));

    // The INSTALLED name, not the release asset's. This assertion used to read
    // `libcore-win-x64.dll` and it was asserting a bug: the platform tag keeps
    // three builds apart on a release page, and an executable linked against
    // `libcore.dll` does not start next to a file that still carries it.
    CHECK(contains(s, "libcore.dll"));
    CHECK(!contains(s, "libcore-win-x64.dll"));

    CHECK(contains(s, "compiled in, so no file of its own: View 0.4.0"));
    // The static library must not appear as a File line.
    CHECK(!contains(s, "File \"..\\dist\\view"));
}

void windows_executables_get_their_extension() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "\\app.exe"));
    CHECK(contains(s, "\\app-cli.exe"));
}

void a_trailing_slash_means_a_recursive_folder() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "File /r \"..\\dist\\app\\docs\\*.*\""));
}

void a_nested_folder_is_written_with_the_separator_nsis_reads() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "SetOutPath \"$INSTDIR\\app-2.0.0\\vendor\\fonts\""));
    CHECK(contains(s, "File /r \"..\\dist\\app\\vendor\\fonts\\*.*\""));
    // The declared form is portable and correct in the manifest; it just may
    // not survive into the script unchanged.
    CHECK(!contains(s, "vendor/fonts"));
}

// A file declared inside a folder installs INTO that folder. The script read
// it from `guide\start.md` and wrote it to the install root, so Hormiga's
// `okf/index.md` would have landed beside the executable (2026-10-26).
void a_file_inside_a_folder_is_installed_into_it() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    const auto into = s.find("SetOutPath \"$INSTDIR\\app-2.0.0\\guide\"");
    const auto file = s.find("File \"..\\dist\\app\\guide\\start.md\"");
    CHECK(into != std::string::npos && file != std::string::npos && into < file);
}

// The general form of the same bug, asserted over the whole script rather
// than over the one line that had it -- because the fix is a chokepoint, and a
// chokepoint is only worth having if something notices when a path routes
// around it. `SetOutPath` reads a forward slash as an ordinary character in a
// directory NAME while `File /r` accepts it as a separator, so the two
// directives put the same declaration in two different places and nothing
// fails loudly.
void no_emitted_path_argument_contains_a_forward_slash() {
    // Every directive whose argument NSIS resolves as a path. `File /r` has a
    // switch before its path, so the check is on the QUOTED arguments.
    static const char* const kPathDirectives[] = {
        "SetOutPath", "File ", "OutFile ", "InstallDir ", "CreateShortcut",
        "Delete ",    "RMDir", "WriteUninstaller"};

    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    std::istringstream lines(s);
    std::string line;
    int checked = 0;
    while (std::getline(lines, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        const std::string directive = line.substr(start);
        if (directive[0] == ';') continue;

        bool is_path = false;
        for (const char* d : kPathDirectives) {
            if (directive.rfind(d, 0) == 0) is_path = true;
        }
        if (!is_path) continue;

        for (auto q = directive.find('"'); q != std::string::npos;) {
            const auto end = directive.find('"', q + 1);
            if (end == std::string::npos) break;
            CHECK(directive.substr(q + 1, end - q - 1).find('/') == std::string::npos);
            ++checked;
            q = directive.find('"', end + 1);
        }
    }
    // An assertion that inspected nothing passes for the wrong reason.
    CHECK(checked > 0);
}

void each_version_installs_into_its_own_folder() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "$INSTDIR\\app-2.0.0"));
    CHECK(contains(s, "RMDir /r \"$INSTDIR\\app-2.0.0\""));
}

void one_selectable_component_per_application() {
    TempDir t;
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "Section \"App 2.0.0\" SEC_app"));
    CHECK(contains(s, "MUI_PAGE_COMPONENTS"));
    // A runtime is never a checkbox: it is folded into the component that
    // needs it, which is what makes it free.
    CHECK(!contains(s, "SEC_core"));
    CHECK(!contains(s, "SEC_view"));
}

void quotes_and_dollars_in_a_description_cannot_break_out() {
    TempDir t;
    mago::test::write_toy(t);
    t.project("app", R"("name":"app","display_name":"App","kind":"application",
        "version":"2.0.0","platforms":["windows-x64"],
        "description":"a \"quoted\" $INSTDIR thing",
        "requires":[{"name":"core","version":">=1.0.0","link":"shared"}],
        "provides":{"gui":"app"})");
    const std::string s =
        nsis::render({resolve(Registry::scan(t.path()), "app", "windows-x64")});
    CHECK(contains(s, "a $\\\"quoted$\\\" $$INSTDIR thing"));
}

void an_empty_plan_list_is_refused() {
    CHECK_THROWS(nsis::render({}), std::invalid_argument, "nothing to install");
}

void the_receipt_is_valid_readable_json() {
    TempDir t;
    const std::string r = nsis::receipt(toy_plans(t), "Void Suite");
    CHECK(contains(r, "\"receipt\": \"mago/0.1\""));
    CHECK(contains(r, "\"install_dir\": \"app-2.0.0\""));
    CHECK_EQ(std::count(r.begin(), r.end(), '{'), std::count(r.begin(), r.end(), '}'));
    CHECK_EQ(std::count(r.begin(), r.end(), '['), std::count(r.begin(), r.end(), ']'));
    // It is a sidecar file, so the script references it by name rather than
    // carrying the whole document through NSIS string escaping.
    const std::string s = nsis::render(toy_plans(t));
    CHECK(contains(s, "File \"mago-receipt.json\""));
}

}  // namespace

int main() {
    the_script_ships_shared_libraries_and_not_static_ones();
    windows_executables_get_their_extension();
    a_trailing_slash_means_a_recursive_folder();
    a_nested_folder_is_written_with_the_separator_nsis_reads();
    a_file_inside_a_folder_is_installed_into_it();
    no_emitted_path_argument_contains_a_forward_slash();
    each_version_installs_into_its_own_folder();
    one_selectable_component_per_application();
    quotes_and_dollars_in_a_description_cannot_break_out();
    an_empty_plan_list_is_refused();
    the_receipt_is_valid_readable_json();
    mago::test::pass("nsis");
    return 0;
}
