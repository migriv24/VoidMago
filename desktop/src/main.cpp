// Void Mago Desktop -- a Void Maiz host over libvoidmago.
//
// Second, and deliberately so: the CLI is the one front end that must always
// work, because the tool that repairs a broken install cannot need a working
// install to run. This binary links Void Maiz, which links Void Core, so
// everything in here is unavailable on exactly the machine Mago exists to
// diagnose -- which is why none of the answers live here.
//
//     Delete Desktop and no answer changes. Delete the CLI and nothing works.
//
// The window is a tab bar over four surfaces and one strip. The strip is not a
// tab on purpose: doctor's findings are context, not a destination -- you want
// to see "2 errors" while looking at the pair that caused them.

#include "app.hpp"
#include "platform.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

ImVec4 level_colour(const std::string& level) {
    if (level == "error") return ImVec4(0.85f, 0.30f, 0.30f, 1.0f);
    if (level == "warn") return ImVec4(0.80f, 0.60f, 0.20f, 1.0f);
    if (level == "ok") return ImVec4(0.40f, 0.70f, 0.50f, 1.0f);
    return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
}

// The command line, inside the window.
//
// It runs the REAL `mago` binary rather than calling the library directly, and
// that is the point: the desktop links `libvoidmago` and could dispatch a verb
// itself, but then it would need its own argument parser -- and a second
// parser is a second idea of what the commands are. Shelling out makes "the
// GUI can do nothing the CLI cannot" true by construction.
//
// It also answers the obvious question about a tool whose whole argument is
// that the terminal comes first: why would its window not have one.
void draw_command_tab(magod::App& app) {
    const std::filesystem::path binary = magod::find_mago();
    if (binary.empty()) {
        ImGui::TextColored(level_colour("error"),
                           "The `mago` binary is not beside this window.");
        ImGui::TextDisabled("cmake -S . -B build && cmake --build build");
        return;
    }

    ImGui::TextDisabled("%s", binary.string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|  scan  graph  doctor  matrix  pair  blast  notify  plan  roots  wizard");

    ImGui::SetNextItemWidth(-160);
    const bool submitted =
        ImGui::InputTextWithHint("##command", "mago ...  (try: matrix, or doctor)",
                                 app.command_buffer, sizeof(app.command_buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool clicked = ImGui::Button("Run");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        app.command_output.clear();
        app.command_ran.clear();
    }

    if (submitted || clicked) {
        const std::string args = app.command_buffer;
        if (!args.empty()) {
            const magod::CommandResult r = magod::run_mago(args);
            app.command_ran = args;
            app.command_exit = r.exit_code;
            app.command_output = r.output;
            app.say(r.exit_code == 0 ? "ok" : "error",
                    "mago " + args + "  ->  exit " + std::to_string(r.exit_code));
            // Anything the CLI wrote, this window has to re-read: the two are
            // looking at the same documents and only one of them just moved.
            app.reload();
        }
        if (submitted) ImGui::SetKeyboardFocusHere(-1);
    }

    if (!app.command_ran.empty()) {
        ImGui::TextColored(level_colour(app.command_exit == 0 ? "ok" : "error"),
                           "$ mago %s   (exit %d)", app.command_ran.c_str(), app.command_exit);
    }
    ImGui::BeginChild("command-output", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(app.command_output.c_str());
    ImGui::EndChild();
}

// The doctor strip. Everything `mago doctor` prints, always on screen, because
// a family that disagrees with itself should not need to be asked.
void draw_doctor_strip(magod::App& app, float height) {
    ImGui::BeginChild("doctor", ImVec2(0, height), true);

    ImGui::TextColored(level_colour(app.errors ? "error" : "ok"), "%zu errors", app.errors);
    ImGui::SameLine();
    ImGui::TextColored(level_colour("warn"), "%zu warnings", app.warnings);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu notes", app.notes);
    ImGui::SameLine();
    ImGui::TextDisabled("|  mago doctor");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
    ImGui::TextDisabled("%s", app.family_path.filename().string().c_str());

    ImGui::Separator();
    if (ImGui::BeginTabBar("strip")) {
        if (ImGui::BeginTabItem("Findings")) {
            ImGui::BeginChild("findings", ImVec2(0, 0));
            for (const auto& f : app.findings) {
                ImGui::TextColored(level_colour(f.level), "%-6s", f.level.c_str());
                ImGui::SameLine(70);
                ImGui::TextDisabled("%-16s", f.project.c_str());
                ImGui::SameLine(220);
                ImGui::TextWrapped("%s", f.message.c_str());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Command")) {
            draw_command_tab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            ImGui::BeginChild("log", ImVec2(0, 0));
            for (const auto& line : app.log) {
                ImGui::TextColored(level_colour(line.level), "%s", line.text.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

}  // namespace

// `voidmago-desktop --check` -- everything the window does except the window.
//
// Reads the documents, projects the family mantle, prints what the library
// said and exits non-zero if anything complained. A GUI whose whole job is
// rendering can be smoke-tested without a display, and on a build machine
// there is no display.
int self_check() {
    magod::App app;
    app.core.set_log_sink(
        [&app](std::string_view level, std::string_view op, std::string_view msg) {
            if (level == "error" || level == "warn") {
                app.say("error", std::string(op) + ": " + std::string(msg));
            }
        });
    app.reload();

    int failures = 0;
    for (const auto& line : app.log) {
        std::printf("%-6s %s\n", line.level.c_str(), line.text.c_str());
        if (line.level == "error") ++failures;
    }
    std::printf("\n%zu projects, %zu bonded pairs, %zu runes, %zu wires, %zu doctor findings\n",
                app.matrix.size(), app.matrix.bonded(), app.scene.nodes.size(),
                app.scene.wires.size(), app.findings.size());
    if (app.scene.nodes.size() != app.matrix.size()) {
        std::printf("error  the canvas has %zu runes for %zu projects\n", app.scene.nodes.size(),
                    app.matrix.size());
        ++failures;
    }
    // The Command tab is only honest if the binary it shells out to is
    // actually there, so the smoke test proves the bridge rather than the
    // intention behind it.
    const std::filesystem::path cli = magod::find_mago();
    if (cli.empty()) {
        std::printf("error  the `mago` binary was not found beside this window\n");
        ++failures;
    } else {
        const magod::CommandResult r = magod::run_mago("roots");
        std::printf("ok     cli bridge: %s -> exit %d, %zu bytes\n", cli.string().c_str(),
                    r.exit_code, r.output.size());
        if (!r.ran || r.output.empty()) {
            std::printf("error  the cli bridge produced nothing\n");
            ++failures;
        }
    }

    std::printf("%s\n", failures ? "check FAILED" : "check ok");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--check") return self_check();

#ifdef _WIN32
    // A console-subsystem binary so `--check` can print on a build machine,
    // but nobody double-clicking a GUI wants a black rectangle behind it.
    // Detaching after the headless path has already returned gets both.
    FreeConsole();
#endif

    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "glfw error %d: %s\n", code, desc);
    });
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window =
        glfwCreateWindow(1400, 880, "Void Mago Desktop", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    magod::App app;
    auto apply_theme = [&] {
        if (app.light_mode) {
            ImGui::StyleColorsLight();
            app.canvas_style.theme = maiz::CanvasTheme::light();
        } else {
            ImGui::StyleColorsDark();
            app.canvas_style.theme = maiz::CanvasTheme::dark();
        }
    };
    apply_theme();

    app.core.set_log_sink(
        [&app](std::string_view level, std::string_view op, std::string_view msg) {
            // Only the core's complaints: its successes are the picture, and
            // the picture is already on screen.
            if (level == "error" || level == "warn") {
                app.say("error", std::string(op) + ": " + std::string(msg));
            }
        });
    app.reload();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEvents();
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Ctrl+wheel and Ctrl+/- scale the interface. The canvas swallows a
        // plain wheel for its camera, so the modifier is what keeps "show me
        // more graph" and "make the words bigger" from being the same gesture.
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                if (io.MouseWheel != 0.0f) app.ui_scale += io.MouseWheel * 0.1f;
                if (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
                    app.ui_scale += 0.1f;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
                    app.ui_scale -= 0.1f;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_0)) app.ui_scale = 1.0f;
            }
            app.ui_scale = app.ui_scale < 0.6f ? 0.6f : (app.ui_scale > 3.0f ? 3.0f : app.ui_scale);
            ImGui::GetStyle().FontScaleMain = app.ui_scale;
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("VoidMagoDesktop", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text("Void Mago Desktop");
        ImGui::SameLine();
        ImGui::TextDisabled("a Void Maiz host over libvoidmago -- it renders, it does not decide");

        // Zoom: one control for the whole interface, because "the text is too
        // small" is never a complaint about one tab. The canvas keeps its own
        // camera; that is a different question and a different wheel.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
        if (ImGui::SmallButton("-")) app.ui_scale -= 0.1f;
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) app.ui_scale += 0.1f;
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) app.ui_scale = 1.0f;
        ImGui::SameLine();
        ImGui::TextDisabled("%d%%", static_cast<int>(app.ui_scale * 100.0f + 0.5f));
        ImGui::SameLine();
        if (ImGui::SmallButton(app.light_mode ? "Dark" : "Light")) {
            app.light_mode = !app.light_mode;
            apply_theme();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("?")) app.show_help = !app.show_help;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("What a seam is, what shares are, and how these projects are\n"
                              "meant to relate to each other. Also the keys.");
        }
        ImGui::Separator();

        const float strip_h = 190.0f;
        ImGui::BeginChild(
            "tabs", ImVec2(0, ImGui::GetContentRegionAvail().y - strip_h * app.ui_scale - 8));
        if (ImGui::BeginTabBar("surfaces")) {
            if (ImGui::BeginTabItem("Family")) {
                magod::draw_family(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Graph")) {
                magod::draw_graph(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Registry")) {
                magod::draw_registry(app);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Release")) {
                magod::draw_release(app);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        draw_doctor_strip(app, strip_h * app.ui_scale);
        ImGui::End();

        magod::draw_help(app);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        const float shade = app.light_mode ? 0.93f : 0.10f;
        glClearColor(shade, shade, shade + 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
