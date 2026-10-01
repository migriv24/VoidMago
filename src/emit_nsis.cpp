#include "mago/emit_nsis.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>

#include "mago/json_out.hpp"

namespace mago::nsis {
namespace {

std::string now_iso8601() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}+00:00", now);
}

// NSIS string literal contents: `$` opens a variable, `"` ends the string.
std::string nsis_str(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '$') {
            out += "$$";
        } else if (c == '"') {
            out += "$\\\"";
        } else if (c == '\n' || c == '\r') {
            out += ' ';
        } else {
            out += c;
        }
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

// Manifests name the target, not the file. Windows adds the extension.
std::string exe_name(const std::string& name, const std::string& platform) {
    if (platform.rfind("windows", 0) == 0 && name.find('.') == std::string::npos) {
        return name + ".exe";
    }
    return name;
}

bool is_folder(const std::string& f) {
    return !f.empty() && f.back() == '/';
}

// NSIS is not a filesystem API. `fs::path` forgives a forward slash on
// Windows, so a manifest that declares `vendor/fonts/` stages into a real
// nested folder and `mago stage` reports it truthfully -- but `SetOutPath`
// reads the slash as an ordinary character in a directory NAME and installs
// into `vendorfonts`, while `File /r` happens to accept it. The two
// directives disagree, so the staging tree is right and the script that
// reproduces it is wrong, and nothing that inspects the tree can see it.
//
// Every path written into the script goes through here rather than being
// normalized at each concatenation, which makes the class of bug unreachable
// instead of fixing one instance of it. `File /r` is normalized too: a
// directive that happens to forgive an input is not a contract.
std::string nsis_path(std::string p) {
    std::replace(p.begin(), p.end(), '/', '\\');
    return p;
}

std::string section(const Plan& plan, const Options& opt) {
    const std::string install_dir = nsis_path("$INSTDIR\\" + plan.install_dir_name());
    const std::string display = nsis_str(plan.display_name);
    std::string s;

    s += "Section \"" + display + " " + plan.version.str() + "\" SEC_" + plan.application + "\n";
    s += "  SetOutPath \"" + install_dir + "\"\n";
    s += "  DetailPrint \"Installing " + display + " " + plan.version.str() + "\"\n";

    for (const auto& entry : plan.payload) {
        if (entry.files.empty()) {
            s += "  ; " + entry.display_name + " " + entry.version.str() +
                 ": nothing to ship (no artifacts declared in its void.json)\n";
            continue;
        }
        s += "  ; " + (entry.role == Role::Root
                           ? std::string("the application itself")
                           : entry.display_name + " " + entry.version.str() + ", linked " +
                                 entry.link) +
             "\n";
        for (const auto& f : entry.files) {
            const std::string src = nsis_path(opt.source_root + "\\" + entry.name);
            if (is_folder(f)) {
                const std::string folder = nsis_path(f.substr(0, f.size() - 1));
                s += "  SetOutPath \"" + install_dir + "\\" + folder + "\"\n";
                s += "  File /r \"" + src + "\\" + folder + "\\*.*\"\n";
                s += "  SetOutPath \"" + install_dir + "\"\n";
            } else {
                // The INSTALLED name, not the release asset's: an executable
                // linked against `libvoidcore.dll` does not start next to a
                // file called `libvoidcore-win-x64.dll`. `mago stage` writes
                // the staging tree under the same rule, so the two halves of
                // this contract are one function.
                const std::string name = nsis_path(entry.role == Role::Root
                                                       ? exe_name(f, plan.platform)
                                                       : installed_name(f, plan.platform));
                s += "  File \"" + src + "\\" + name + "\"\n";
            }
        }
    }

    if (!plan.absorbed.empty()) {
        std::string absorbed;
        for (const auto& e : plan.absorbed) {
            if (!absorbed.empty()) absorbed += ", ";
            absorbed += e.display_name + " " + e.version.str();
        }
        s += "  ; compiled in, so no file of its own: " + absorbed + "\n";
    }

    for (const auto& entry : plan.payload) {
        if (entry.role != Role::Root) continue;
        for (const auto& f : entry.files) {
            if (is_folder(f)) continue;
            s += "  CreateShortcut \"$SMPROGRAMS\\" + display + ".lnk\" \"" + install_dir + "\\" +
                 nsis_path(exe_name(f, plan.platform)) + "\"\n";
            break;
        }
        break;
    }

    s += "SectionEnd\n";
    return s;
}

}  // namespace

std::string receipt(const std::vector<Plan>& plans, const std::string& suite_name) {
    JsonWriter w(2);
    w.begin_object();
    w.field("receipt", "mago/0.1");
    w.field("suite", suite_name);
    w.field("written", now_iso8601());
    w.key("installed").begin_array();
    for (const auto& p : plans) {
        // The plan already knows how to describe itself; re-encoding it here
        // would be a second definition of the same document to keep in step.
        p.write_json(w);
    }
    w.end_array();
    w.end_object();
    return w.str();
}

std::string render(const std::vector<Plan>& plans, const Options& opt) {
    if (plans.empty()) throw std::invalid_argument("nothing to install: no plans given");

    std::string s;
    s += "; Generated by Void Mago -- do not edit.\n";
    s += "; Source of truth is every project's void.json; regenerate with `mago wizard`.\n";
    s += "; Generated " + now_iso8601() + "\n\n";
    s += "Unicode true\n";
    s += "ManifestDPIAware true\n\n";
    s += "!include \"MUI2.nsh\"\n";
    s += "!include \"LogicLib.nsh\"\n\n";
    s += "Name \"" + nsis_str(opt.suite_name) + "\"\n";
    s += "OutFile \"" + nsis_path(opt.outfile) + "\"\n";
    s += "InstallDir \"$LOCALAPPDATA\\" + nsis_path(opt.suite_dir) + "\"\n";
    s += "InstallDirRegKey HKCU \"Software\\" + nsis_path(opt.suite_dir) +
         "\" \"InstallDir\"\n";
    s += "SetCompressor /SOLID lzma\n\n";
    s += "; Per-user by default: elevation is a cost, and nothing here needs a machine.\n";
    s += "; To install for every user, set this to `admin` and InstallDir to\n";
    s += "; \"$PROGRAMFILES64\\" + nsis_path(opt.suite_dir) +
         "\" -- but only if something genuinely\n";
    s += "; needs to write outside the user's profile, which nothing here does today.\n";
    s += "RequestExecutionLevel user\n\n";
    s += "!define MUI_ABORTWARNING\n";
    s += "!define MUI_COMPONENTSPAGE_SMALLDESC\n";
    s += "!insertmacro MUI_PAGE_WELCOME\n";
    s += "!insertmacro MUI_PAGE_LICENSE \"" + nsis_path(opt.license_file) + "\"\n";
    s += "!insertmacro MUI_PAGE_COMPONENTS\n";
    s += "!insertmacro MUI_PAGE_DIRECTORY\n";
    s += "!insertmacro MUI_PAGE_INSTFILES\n";
    s += "!insertmacro MUI_PAGE_FINISH\n\n";
    s += "!insertmacro MUI_UNPAGE_CONFIRM\n";
    s += "!insertmacro MUI_UNPAGE_INSTFILES\n\n";
    s += "!insertmacro MUI_LANGUAGE \"English\"\n\n";

    for (const auto& p : plans) s += section(p, opt) + "\n";

    for (const auto& p : plans) {
        s += "LangString DESC_" + p.application + " ${LANG_ENGLISH} \"" +
             nsis_str(p.description.empty() ? p.display_name : p.description) + "\"\n";
    }
    s += "\n!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN\n";
    for (const auto& p : plans) {
        s += "  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_" + p.application + "} $(DESC_" +
             p.application + ")\n";
    }
    s += "!insertmacro MUI_FUNCTION_DESCRIPTION_END\n\n";

    s += "Section \"-receipt\"\n";
    s += "  ; What is on this machine, and why. `mago status` reads it instead of\n";
    s += "  ; probing files, and the updater diffs the next plan against it.\n";
    s += "  SetOutPath \"$INSTDIR\"\n";
    s += "  File \"" + nsis_path(opt.receipt_source) + "\"\n\n";
    s += "  WriteRegStr HKCU \"Software\\" + nsis_path(opt.suite_dir) +
         "\" \"InstallDir\" \"$INSTDIR\"\n";
    s += "  WriteUninstaller \"$INSTDIR\\uninstall.exe\"\n\n";
    s += "  !define UNINST_KEY \\\n";
    s += "    \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
         nsis_path(opt.suite_dir) + "\"\n";
    s += "  WriteRegStr HKCU \"${UNINST_KEY}\" \"DisplayName\" \"" + nsis_str(opt.suite_name) +
         "\"\n";
    s += "  WriteRegStr HKCU \"${UNINST_KEY}\" \"DisplayVersion\" \"" + opt.suite_version +
         "\"\n";
    s += "  WriteRegStr HKCU \"${UNINST_KEY}\" \"Publisher\" \"" + nsis_str(opt.publisher) +
         "\"\n";
    s += "  WriteRegStr HKCU \"${UNINST_KEY}\" \"UninstallString\" "
         "\"$\\\"$INSTDIR\\uninstall.exe$\\\"\"\n";
    s += "  WriteRegDWORD HKCU \"${UNINST_KEY}\" \"NoModify\" 1\n";
    s += "SectionEnd\n\n";

    s += "Section \"Uninstall\"\n";
    s += "  ; Side-by-side means each version owns its own folder. Removing one leaves\n";
    s += "  ; every other version working, which is the entire point of installing that\n";
    s += "  ; way -- so this removes exactly the versions this installer wrote.\n";
    for (const auto& p : plans) {
        s += "  Delete \"$SMPROGRAMS\\" + nsis_str(p.display_name) + ".lnk\"\n";
        s += "  RMDir /r \"$INSTDIR\\" + nsis_path(p.install_dir_name()) + "\"\n";
    }
    s += "  Delete \"$INSTDIR\\" + nsis_path(std::string(kReceiptName)) + "\"\n";
    s += "  Delete \"$INSTDIR\\uninstall.exe\"\n";
    s += "  RMDir \"$INSTDIR\"\n";
    s += "  DeleteRegKey HKCU \\\n";
    s += "    \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
         nsis_path(opt.suite_dir) + "\"\n";
    s += "  DeleteRegKey /ifempty HKCU \"Software\\" + nsis_path(opt.suite_dir) + "\"\n";
    s += "SectionEnd\n";
    return s;
}

}  // namespace mago::nsis
