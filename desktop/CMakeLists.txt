# Void Mago Desktop — the GUI, and everything that makes it heavy.
#
# OFF by default, and that is the architecture rather than a convenience.
# `libvoidmago` and the `mago` CLI take NOTHING from the family: no Void Core,
# no Void Maiz, no Void Palabra, because anything they link is something that
# can be broken underneath them. This directory is where that stops being true
# — it pulls in Void Maiz, which pulls in Void Core and GLFW and Dear ImGui —
# so it is a separate target behind a separate switch, and a clone with none of
# those siblings still configures and builds the CLI with zero dependencies.
#
#   cmake -S . -B build -G Ninja -DVOIDMAGO_BUILD_DESKTOP=ON
#
# Nothing here links back into the CLI, and nothing in the CLI knows this
# directory exists.

set(VOIDMAIZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../VoidMaiz" CACHE PATH
    "Path to the Void Maiz checkout (it locates Void Core via its own VOIDCORE_ROOT)")

if(NOT EXISTS "${VOIDMAIZ_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "VOIDMAGO_BUILD_DESKTOP is ON but no Void Maiz checkout at ${VOIDMAIZ_ROOT}.\n"
    "Set -DVOIDMAIZ_ROOT=<path>, or leave the desktop off — the CLI needs none of it.")
endif()

add_subdirectory("${VOIDMAIZ_ROOT}" voidmaiz)

add_executable(voidmago_desktop
  src/main.cpp
  src/app.cpp
  src/tab_family.cpp
  src/tab_graph.cpp
  src/tab_registry.cpp
  src/tab_release.cpp
  src/help.cpp
  src/platform.cpp
)
set_target_properties(voidmago_desktop PROPERTIES OUTPUT_NAME "voidmago-desktop")

# `voidmago` is the whole point: Desktop calls the same library functions the
# CLI calls, which is how it manages to contain no logic of its own.
# `voidmaiz_view` brings the canvas, the projection engine, Dear ImGui and the
# Void Core embedding.
target_link_libraries(voidmago_desktop PRIVATE voidmago voidmaiz_view)

# The folder picker and the process runner are shell and kernel calls; nothing
# else here touches the platform.
if(WIN32)
  target_link_libraries(voidmago_desktop PRIVATE shell32 ole32)
endif()
target_include_directories(voidmago_desktop PRIVATE src)

if(NOT MSVC)
  target_compile_options(voidmago_desktop PRIVATE -Wall -Wextra)
endif()

# Void Core ships as a shared library on desktop, so it has to sit next to the
# executable. Void Maiz already found it; reuse its answer rather than looking
# a second time and possibly disagreeing.
if(WIN32 AND VOIDCORE_DLL)
  add_custom_command(TARGET voidmago_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VOIDCORE_DLL}" "$<TARGET_FILE_DIR:voidmago_desktop>")
endif()

# The window's smoke test: everything it does except the window. A GUI whose
# whole job is rendering can be checked without a display, and a build machine
# has no display.
if(VOIDMAGO_BUILD_TESTS)
  add_test(NAME desktop_check COMMAND voidmago_desktop --check)
endif()
