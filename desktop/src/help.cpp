// The help window, and the little (?) markers beside the words that carry the
// most weight.
//
// The vocabulary here is not obvious and it is not guessable: `seam`,
// `shares`, `bond`, `reship` and `retest` all mean something specific, and
// somebody using this window six months from now -- including its author --
// should not have to open the knowledge bundle to find out what.
//
// Everything below is a condensation of okf/concepts/. It is written here
// rather than loaded from those files on purpose: the concepts are long-form
// arguments, and what a person needs while staring at a cell is two sentences.

#include "app.hpp"
#include "imgui.h"

namespace magod {

void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

namespace {

void section(const char* title, const char* body) {
    ImGui::SeparatorText(title);
    ImGui::TextWrapped("%s", body);
    ImGui::Spacing();
}

}  // namespace

void draw_help(App& app) {
    if (!app.show_help) return;

    ImGui::SetNextWindowSize(ImVec2(660, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("How this works", &app.show_help)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Void Mago knows which versions of which Void projects work together, and what a "
        "change to one of them would cost the others. This window is where you write down "
        "the half of that no manifest can hold.");
    ImGui::Spacing();

    if (ImGui::BeginTabBar("help")) {
        if (ImGui::BeginTabItem("How projects relate")) {
            section("Three kinds of project",
                    "A RUNTIME (Void Core, Void Maiz, Void Allomone) is never a row in "
                    "anybody's list of installed software -- it ships inside an application. "
                    "A CAPABILITY (Void Palabra) is something you develop with. An "
                    "APPLICATION (Void Hormiga, FaultSack) is the thing a person installs, "
                    "updates and sees. The sorting is by what a USER experiences, not by how "
                    "the code is shaped.");

            section("requires -- a real dependency",
                    "One project builds against another, and `link` says how: SHARED (a file "
                    "ships beside the binary), STATIC / SOURCE / VENDORED (the code is inside "
                    "the consumer's build output), or IMPORT (an interpreted consumer imports "
                    "it at runtime -- nothing is compiled and nothing of the dependency's "
                    "ships). That one field decides everything else.");

            section("interacts_with -- a seam, which is not a dependency",
                    "Void Reyna emits a transcript that Void Hormiga replays. Neither links "
                    "the other; a JSON document crosses between them. That is a SEAM, and "
                    "keeping it apart from `requires` is what stops the compatibility matrix "
                    "from inventing edges. A seam costs a retest; it can never cost a "
                    "rebuild.\n\n"
                    "A seam belongs to whoever DEFINES the document that crosses it. "
                    "`void.json` is Void Mago's format, so an edge at that seam is a retest "
                    "when Mago changes and nothing at all when anybody else does.");

            section("What it means for two applications to relate",
                    "The goal is applications that share whole functionality rather than "
                    "merely opening each other's files -- but a boundary drawn today becomes "
                    "a process boundary later, so it is worth being strict. A video editor "
                    "reading a Hormiga project's assets is reasonable. A video editor "
                    "deciding what a `clearance:public` tag MEANS is not, because that "
                    "judgement is Hormiga's domain and does not travel. When you write a "
                    "pairing note, that distinction is the useful thing to record.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("The table")) {
            section("Why it is symmetric",
                    "The relationship between two projects is the same read from either side "
                    "-- AB = BA. The DIRECTION is not: Void Maiz requires Void Core and not "
                    "the other way round. Every square matrix splits uniquely into a "
                    "symmetric part and an antisymmetric one, so the relationship is stored "
                    "once and the arrow is flipped when you read the cell from below the "
                    "diagonal. Only the upper triangle exists: 21 records for 7 projects, "
                    "not 49.");

            section("The glyphs",
                    "#   the project itself -- the diagonal is not an interaction\n"
                    ".   no edge either way\n"
                    ">   the row requires the column\n"
                    "<   the column requires the row\n"
                    "-   a seam: data crosses, nothing links\n"
                    "+   authored only -- a person recorded a relationship no manifest does\n"
                    "*   there is a note on this pair\n"
                    "!   the two manifests disagree about the seam's name");

            section("bond -- how tightly two projects touch",
                    "none < authored < seam < requires. A pair described BOTH as a dependency "
                    "and as a seam takes the tighter of the two, because that is what it "
                    "costs. The cell is a join over every piece of evidence from both "
                    "manifests, which is why it cannot depend on which project you asked "
                    "about first.");

            section("cost -- what an additive change at either end costs",
                    "UNTOUCHED  no edge, nothing to do.\n"
                    "RETEST     reached at a seam, or imported: re-run the contract, nothing "
                    "rebuilds.\n"
                    "RESHIP     linked shared and the change stayed compatible: drop in the "
                    "new file, the binary is untouched.\n"
                    "REBUILD    the code is compiled in, or the line broke: build it again.\n"
                    "BLOCKED    the new version violates a floor the consumer declared.\n\n"
                    "Only REBUILD propagates, because it is the only verdict where a "
                    "project's own build output changes. That single rule is the difference "
                    "between 'a Core patch means rebuilding thirty applications' and 'a Core "
                    "patch means shipping one DLL'.");

            section("The empty cells are the result",
                    "A family whose matrix fills in has stopped being a family of separable "
                    "projects. The count of pairs with no edge at all is the number this "
                    "table exists to keep large.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Writing it down")) {
            section("Why family.json exists",
                    "Void Core says it meets Void Palabra at the 'c-abi' seam. Void Palabra "
                    "says the seam is 'state document + command journal'. Both manifests are "
                    "internally consistent and honestly written, and neither is wrong from "
                    "inside its own repository -- because a fact about a PAIR belongs to "
                    "neither repository. `family.json` is the one place it can live.");

            section("note",
                    "The sentence. What these two actually have to do with each other, in "
                    "your words -- the thing Mago cannot compute and never will. Everything "
                    "else on the screen is arithmetic over manifests; this is the part worth "
                    "the most.");

            section("seam",
                    "The AGREED name for what crosses between them. Filling this in on a "
                    "disputed pair settles it: the cell stops showing '!' and `mago doctor` "
                    "stops arguing and starts saying 'family.json settles it as X, so the fix "
                    "is known' -- the manifests are still wrong, but now with the answer "
                    "attached.");

            section("shares",
                    "What actually crosses, as a comma-separated list. Not the mechanism -- "
                    "the CONTENT. 'OKF bundles', 'the glyph vocabulary', 'an assets folder'. "
                    "It is the field that answers 'what would break if we stopped talking?' "
                    "and the one that turns into a port mapping when a boundary becomes a "
                    "process boundary.");

            section("Everything here is also a command",
                    "Nothing this window does is unavailable from the terminal, and that is "
                    "structural rather than polite: the Command tab at the bottom runs the "
                    "real `mago` binary. The window computes nothing of its own -- delete it "
                    "and no answer changes.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Keys")) {
            section("Zoom",
                    "Ctrl + mouse wheel     anywhere, scales the whole interface\n"
                    "Ctrl + = / Ctrl + -    the same, from the keyboard\n"
                    "Ctrl + 0               back to 100%\n\n"
                    "On the Graph tab the wheel alone zooms the CANVAS, which is a separate "
                    "camera -- the interface scale and the drawing's scale are different "
                    "things and are controlled separately on purpose.");

            section("The canvas",
                    "drag a node        move it (view state; not saved anywhere)\n"
                    "right-drag         pan\n"
                    "wheel              zoom the camera\n\n"
                    "The graph is the same content as the table, projected onto a Void Maiz "
                    "canvas. Its positions are not facts about anything and are rebuilt from "
                    "the documents on every rescan.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace magod
