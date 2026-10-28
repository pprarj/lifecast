cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Lifecast - a modern interface for RaceMenu.
#
# The panel is drawn by one of three web UI frameworks - Prisma UI, Magelight
# UI or Meridian UI - chosen at run time (src/Panel/Panel.h); RaceMenu is
# reached through SKEE.
# ---------------------------------------------------------------------------

project(
    Lifecast
    VERSION 0.1.0
    LANGUAGES CXX
)

# CommonLibSSE-NG requires C++23.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Live tuning of the character view's six numbers (Panel/Stage.h).
#
# OFF by default, and the default is the point. Those numbers are part of the
# view's design, not settings: a shipped build must not be able to change them,
# and "remember to turn it off before packaging" is not a gate - it is a thing
# to forget. With OFF the handler and the readout are not compiled at all, so
# forgetting produces a DLL with no tuning in it rather than one with tuning
# left on.
#
# Turn it on deliberately, for a development build where knobs are going to be
# turned:
#
#   cmake --preset vs2022-windows -S . -DLIFECAST_TUNING=ON
#
# One build buys every value: the six are then changed from the panel, in the
# same boot, as many times as he likes. What this option removes is the build
# PER VALUE, which is what made trying one expensive.
# ---------------------------------------------------------------------------
option(LIFECAST_TUNING "Let the panel retune the character view's six numbers at run time" OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
    @ONLY
)

# CommonLibSSE-NG comes from the git submodule at extern/commonlibsse-ng
# (alandtse/CommonLibVR, branch ng), pinned to v6.7.1 = 70c1acd. The pin is
# this project's own and is stated here as a version, so that it can be read
# off this file instead of being inferred from whatever is checked out.
#
# ENABLE_SKYRIM_VR is forced OFF: VR is out of the 1.0 scope by design.
# It also avoids needing the nested openvr submodule, which is not
# initialized here.
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE-NG tests in consumer" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Lifecast does not target VR" FORCE)
add_subdirectory(extern/commonlibsse-ng EXCLUDE_FROM_ALL)

include(extern/commonlibsse-ng/cmake/CommonLibSSE.cmake)

# add_commonlibsse_plugin creates the DLL target, wires up CommonLibSSE
# linkage, and auto-generates the SKSE version boilerplate.
# Output file will be Lifecast.dll.
add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Awrawra"
    SOURCES
        src/plugin.cpp
        src/Core/ListSig.cpp
        src/Core/Log.cpp
        src/Core/MeridianLink.cpp
        src/Core/ObodyLink.cpp
        src/Core/RaceMenuSync.cpp
        src/Core/TintRefresh.cpp
        src/Core/SkeeLink.cpp
        src/Core/Settings.cpp
        src/Edit/Appearance.cpp
        src/Edit/Body.cpp
        src/Edit/Compare.cpp
        src/Edit/FilmPaint.cpp
        src/Edit/FilmSliders.cpp
        src/Edit/Freeze.cpp
        src/Edit/Finish.cpp
        src/Edit/Light.cpp
        src/Edit/Overlays.cpp
        src/Edit/RacePresets.cpp
        src/Edit/RaceSex.cpp
        src/Edit/Session.cpp
        src/Edit/Tints.cpp
        src/Edit/Undress.cpp
        src/Panel/Bridge.cpp
        src/Panel/MagelightBackend.cpp
        src/Panel/MeridianBackend.cpp
        src/Panel/Pad.cpp
        src/Panel/Panel.cpp
        src/Panel/PrismaBackend.cpp
        src/Panel/RaceMenuFilm.cpp
        src/Panel/RaceMenuTabs.cpp
        src/Panel/Smf/DeadKeys.cpp
        src/Panel/Smf/View.cpp
        src/Panel/SmfBackend.cpp
        src/Panel/Stage.cpp
        src/Panel/Switcher.cpp
        src/Preset/Content.cpp
        src/Preset/Guard.cpp
        src/Preset/Jslot.cpp
        src/Preset/Library.cpp
        src/Preset/Load.cpp
        src/Preset/Save.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if(LIFECAST_TUNING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LIFECAST_TUNING)
    message(WARNING
        "LIFECAST_TUNING is ON: the panel can retune the character view's six numbers at run time. "
        "This is a development build. Do NOT package it - reconfigure with -DLIFECAST_TUNING=OFF.")
else()
    message(STATUS "LIFECAST_TUNING is OFF - the view's numbers are compile-time constants.")
endif()

# The panel bridge parses and writes the JSON messages the page exchanges
# with the DLL. nlohmann-json is already a manifest dependency.
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

# /utf-8 is required by the MeridianUI headers, not a style choice: they ship
# without a UTF-8 BOM and carry UTF-8 punctuation in their doc comments, so
# MSVC decodes them with the system codepage and emits C4819 for every one of
# them without this. Stated in MeridianUI/docs/MeridianUI-AuthorGuide.md,
# section "What you ship".
target_compile_options(${PROJECT_NAME} PRIVATE "/utf-8")

# extern/ on the include path is what makes the vendored third-party headers
# addressable the way their own guides document them:
#   #include "MeridianUIAPI/API.h"
#   #include "PrismaUI/PrismaUI_API.h"
target_include_directories(${PROJECT_NAME} PRIVATE
    # src itself, so that a module in a subfolder says "Core/Log.h" like every
    # other module does. Without it an include only resolves relative to the
    # including file, and the same line means different things depending on
    # which folder it is written in.
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# PCH.h is mandatory for CommonLibSSE-NG. Force-included in every file of the
# target, it brings in the CommonLib headers before Relocation.h gets parsed.
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# ---------------------------------------------------------------------------
# Copies the DLL to this project's own SKSE/Plugins/ after each build.
#
# The Lifecast deploys to itself: the mod folder that MO2 reads mirrors Data/,
# and it is two levels above this file - this source tree is nested one level
# deeper inside it, so the deploy path climbs two levels rather than one.
#
# LIFECAST_OUTPUT_FOLDER is overridable: pass -DLIFECAST_OUTPUT_FOLDER=... on
# the CMake command line, or set it in the environment. Set it to an empty
# string to skip deploy entirely.
# ---------------------------------------------------------------------------
if(NOT DEFINED LIFECAST_OUTPUT_FOLDER AND DEFINED ENV{LIFECAST_OUTPUT_FOLDER})
    set(LIFECAST_OUTPUT_FOLDER "$ENV{LIFECAST_OUTPUT_FOLDER}")
endif()

# ---------------------------------------------------------------------------
# Reproducible build of the shipped DLL.
#
# Without /Brepro the linker stamps the wall clock into the PE TimeDateStamp -
# once in the COFF header and once more in the IMAGE_DEBUG_DIRECTORY entry that
# mirrors it. Two clean builds of identical source then differ, which breaks
# any comparison by hash: a different hash stops proving a different source.
#
# It also defeats the copy_if_different below. Those bytes differ on every
# link, so the deploy never skips - every build rewrites the installed DLL,
# including a build that changed nothing. With the game running that copy hits
# a locked file, and with a measurement in progress it swaps the binary under
# the session that is measuring it.
#
# /Brepro replaces the timestamp with a value derived from the link inputs, so
# rebuilding a given commit reproduces the same bytes.
#
# Applied only to the plugin DLL: it is the distributed artifact. A test
# executable never ships and nobody compares its hash.
#
# Measured (2026-08): two clean builds of identical source differed by 4 bytes
# out of 724992, every one of them timestamp. The measurement and the reasoning
# behind this flag are recorded in the private workspace.
# ---------------------------------------------------------------------------
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/Brepro")
endif()

if(NOT DEFINED LIFECAST_OUTPUT_FOLDER)
    set(LIFECAST_OUTPUT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/../../SKSE/Plugins")
endif()

if(LIFECAST_OUTPUT_FOLDER)
    message(STATUS "DLL deploy folder: ${LIFECAST_OUTPUT_FOLDER}")
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIFECAST_OUTPUT_FOLDER}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>" "${LIFECAST_OUTPUT_FOLDER}"
        VERBATIM
    )
    # A Debug build deployed here links the debug CRT (ucrtbased.dll,
    # MSVCP140D.dll, VCRUNTIME140D.dll, VCRUNTIME140_1D.dll), which normal
    # players never have installed - it fails to load with SKSE Plugin Loader
    # error 0000007E (ERROR_MOD_NOT_FOUND). Print the config on every deploy
    # so a Debug build never silently becomes the shipped file.
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
            "*** Lifecast.dll deploy: config=$<CONFIG> -> ${LIFECAST_OUTPUT_FOLDER} -- if config is Debug, this build links the debug CRT and must NEVER be packaged for release. ***"
        VERBATIM
    )
endif()

# Optional local-only sources, added LAST so they inherit everything the target
# was configured with above.
#
# Development instrumentation - a measurement probe, scratch logging - has to
# compile into this DLL, which means it has to sit in this source tree. It is
# not part of what the mod ships, so it does not belong in this repository:
# src/_local/ is gitignored, and the file that lists those sources lives with
# it, outside. When the folder is absent - the normal state of a clean clone -
# this line does nothing and the build is unaffected.
include(src/_local/local.cmake OPTIONAL)
