cmake_minimum_required(VERSION 3.20)
project(voidmago CXX C) # C for the vendored cJSON, which only the loader uses

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VOIDMAGO_VERSION "0.1.8")

# Consumers could add_subdirectory() this file, so nothing below may assume it
# is the top-level project — no CMAKE_BINARY_DIR, and tests only when we ARE
# the top. Same rule the rest of the family follows.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VOIDMAGO_IS_TOP TRUE)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
  set(VOIDMAGO_IS_TOP FALSE)
endif()

option(VOIDMAGO_BUILD_TESTS "Build the test suite" ${VOIDMAGO_IS_TOP})

# The GUI, and everything heavy. OFF by default and that is architectural: the
# CLI takes nothing from the family, so a clone with no siblings checked out
# still configures and builds it. See desktop/CMakeLists.txt.
option(VOIDMAGO_BUILD_DESKTOP "Build Void Mago Desktop (needs Void Maiz + Void Core)" OFF)

# ── The library ──────────────────────────────────────────────────────────────
#
# STATIC and DEPENDENCY-FREE, and this one is not merely a preference.
#
# Void Mago is the tool that repairs a broken install. Anything it links is
# something that can be broken underneath it, so it takes NOTHING from the
# family: not Void Core, not Void Maiz, not Void Palabra. The only third-party
# code is the same cJSON every other Void repo vendors, compiled in here rather
# than found on the machine.
#
# The consequence worth stating: `mago` is a single native binary with no
# runtime to install first. That is the property that lets it run on a machine
# where the thing it is fixing does not.
add_library(voidmago STATIC
  src/semver.cpp
  src/home.cpp
  src/versions.cpp
  src/vendors.cpp
  src/sha256.cpp
  src/imports.cpp
  src/stage.cpp
  src/feed.cpp
  src/manifest.cpp
  src/registry.cpp
  src/resolve.cpp
  src/family.cpp
  src/matrix.cpp
  src/blast.cpp
  src/notify.cpp
  src/emit_nsis.cpp
  src/bootstrap.cpp
  vendor/cJSON.c
)
target_include_directories(voidmago PUBLIC include PRIVATE vendor)
target_compile_definitions(voidmago
  PRIVATE CJSON_HIDE_SYMBOLS
  PUBLIC  MAGO_VERSION_STR="${VOIDMAGO_VERSION}")
if(NOT MSVC)
  target_compile_options(voidmago PRIVATE -Wall -Wextra)
endif()

# GCC before 9.1 puts std::filesystem in a separate library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
  target_link_libraries(voidmago PUBLIC stdc++fs)
endif()

# `mago` has to run on a machine with nothing installed on it — that is the
# entire point of the tool that repairs installs, and it is why this project is
# C++ rather than something with a runtime to install first.
#
# MinGW does not give you that by default: it links libstdc++, libgcc and
# winpthread as DLLs out of the toolchain's own directory, so the binary runs
# in the developer's shell and nowhere else. Link them in and ship one file.
if(MINGW)
  target_link_options(voidmago PUBLIC -static)
endif()

# ── The CLI ──────────────────────────────────────────────────────────────────
#
# The real front end. A Void Maiz desktop host comes later and sits ON TOP of
# this library — never the other way round, because an updater that needs the
# GUI library it is updating cannot repair a broken install.
add_executable(mago_cli src/main.cpp)
target_link_libraries(mago_cli PRIVATE voidmago)
set_target_properties(mago_cli PROPERTIES OUTPUT_NAME "mago")
if(NOT MSVC)
  target_compile_options(mago_cli PRIVATE -Wall -Wextra)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
#
# Small smoke executables, as everywhere else in the family. Each builds a
# miniature family in a temp directory, so a real version bump in Void Core
# cannot turn this suite red; family_smoke is the deliberate exception and
# asserts shape rather than versions.
if(VOIDMAGO_BUILD_TESTS)
  enable_testing()

  foreach(t semver resolve blast doctor notify matrix vendors ship nsis family bootstrap)
    add_executable(mago_${t}_smoke tests/${t}_smoke.cpp)
    target_link_libraries(mago_${t}_smoke PRIVATE voidmago)
    target_include_directories(mago_${t}_smoke PRIVATE tests)
    if(NOT MSVC)
      target_compile_options(mago_${t}_smoke PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${t}_smoke COMMAND mago_${t}_smoke)
  endforeach()

  # family_smoke reads the real checkouts beside this repo, and skips itself
  # when they are not there.
  set_tests_properties(family_smoke PROPERTIES
    ENVIRONMENT "VOIDMAGO_FAMILY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

# ── The desktop host ─────────────────────────────────────────────────────────
#
# LAST, and outside everything above. Nothing in this file's targets links it,
# and it links `voidmago` the same way any other consumer would.
if(VOIDMAGO_BUILD_DESKTOP)
  add_subdirectory(desktop)
endif()
