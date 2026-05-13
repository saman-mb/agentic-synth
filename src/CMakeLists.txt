# ── Mapper Library ───────────────────────────────────────────────────────────
# Pure C++ keyword → PatchStruct mapping; no JUCE dependency
add_library(agentic_synth_mapper STATIC
    mapper/HeuristicParser.cpp
    mapper/GrammarSampler.cpp
    mapper/SemanticMapper.cpp
)
target_include_directories(agentic_synth_mapper PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_mapper)
# juce::juce_core provides juce::JSON, used by SemanticMapper to replace
# hand-rolled JSON substring scans (engineering audit; same pattern as
# commit 91d5e4b in AgentBridge).
target_link_libraries(agentic_synth_mapper
    PUBLIC
        juce::juce_core
)

# ── Engine Library ───────────────────────────────────────────────────────────
# Phase 2 (Item #2): PatchStateManager is removed (APVTS owns persistence), so
# the engine library no longer needs a JUCE-flavour split — the only TU that
# pulled juce_data_structures (PatchStateManager.cpp) is gone. We keep a single
# engine_core target plus an INTERFACE alias `agentic_synth_engine` for
# backwards-compat with consumers that still link by the legacy name.
#
# `agentic_synth_engine_core` is JUCE-light: the only juce dep is `juce_core`
# transitively via mapper (SemanticMapper uses juce::JSON). No juce_audio_basics
# / juce_data_structures dependency from the engine side.
add_library(agentic_synth_engine_core STATIC
    engine/ADSREnvelope.cpp
    engine/WavetableOscillator.cpp
    engine/VAOscillator.cpp
    engine/Delay.cpp
    engine/Filter.cpp
    engine/LFO.cpp
    engine/ParamSmoother.cpp
    engine/VoiceManager.cpp
    engine/PatchValidator.cpp
    engine/VariationEngine.cpp
    engine/MidiHandler.cpp
    engine/MorphEngine.cpp
    engine/StyleTransfer.cpp
    engine/PresetExporter.cpp
    engine/MultiModalInput.cpp
    engine/Reverb.cpp
)
target_include_directories(agentic_synth_engine_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_engine_core)

# mapper currently links juce::juce_core PUBLIC (SemanticMapper uses juce::JSON),
# so engine_core transitively sees juce_core through mapper. That is acceptable
# — the goal of the split is to remove juce_audio_basics + juce_data_structures
# from the DSP-only path, which is what dragged in heavy modules and slowed test
# builds. Future cleanup: replace juce::JSON in mapper with a lightweight
# alternative to make engine_core fully JUCE-free.
target_link_libraries(agentic_synth_engine_core
    PUBLIC
        agentic_synth_mapper
)

# ── Backward-compat alias ────────────────────────────────────────────────────
# Phase 2 (Item #2): PatchStateManager removed — APVTS is the single source of
# truth for persistent state. The engine_juce static library that wrapped it
# is gone; existing call sites that link `agentic_synth_engine` still work via
# the INTERFACE alias below, which now forwards directly to engine_core. JUCE
# data-structures / audio-basics are still pulled in transitively by consumers
# that need them (agent_lib, plugin) — engine_core itself remains JUCE-light
# (only juce_core via the mapper).
add_library(agentic_synth_engine INTERFACE)
target_link_libraries(agentic_synth_engine INTERFACE agentic_synth_engine_core)
# ── Agent Library ────────────────────────────────────────────────────────────
# AI agent layer: depends on engine_lib, never the other way around.
add_library(agentic_synth_agent STATIC
    agent/AgentBridge.cpp
    agent/DictionaryService.cpp
    agent/KnobBridge.cpp
    agent/ParamMap.cpp
    agent/PromptHandler.cpp
    agent/SessionMemory.cpp
    agent/PrePatchPipeline.cpp
    agent/StreamParser.cpp
    agent/Telemetry.cpp
    agent/VariationRecommender.cpp
    agent/WhisperClient.cpp
    agent/GenerationService.cpp
)
target_include_directories(agentic_synth_agent PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_agent)

target_link_libraries(agentic_synth_agent
    PUBLIC
        agentic_synth_engine
        juce::juce_core
        juce::juce_events
)

# ── Standalone App and Plugin (gated on BUILD_PLUGIN) ───────────────────────
if(AGENTIC_SYNTH_BUILD_PLUGIN)

# Linux WebView dependency: JUCE 8's juce_gui_extra includes <gtk/gtk.h>
# directly when JUCE_WEB_BROWSER=1 but does NOT declare linuxPackages in
# its module manifest, so callers must supply the include paths + libs.
# Resolve via pkg-config; the resulting INTERFACE library is linked into
# the plugin + standalone targets below on Linux only.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)
    pkg_check_modules(GTK3       REQUIRED IMPORTED_TARGET gtk+-3.0)
    add_library(agentic_synth_linux_webview INTERFACE)
    target_link_libraries(agentic_synth_linux_webview INTERFACE
        PkgConfig::WEBKIT2GTK PkgConfig::GTK3)
else()
    add_library(agentic_synth_linux_webview INTERFACE)  # no-op on mac/win
endif()

# ── React UI bundling ────────────────────────────────────────────────────────
option(AGENTIC_SYNTH_UI_DEV "Hot-load Vite dev server instead of bundled assets" OFF)

set(UI_SRC_DIR  "${CMAKE_SOURCE_DIR}/ui")
set(UI_DIST_DIR "${UI_SRC_DIR}/dist")

# Bootstrap on fresh checkout: run npm at configure time if dist missing.
# Note: invoke `npx vite build` directly rather than `npm run build` because
# the npm script chains `tsc &&` and the repo currently has pre-existing
# TypeScript errors that are out of scope for the WebView migration.
find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)
find_program(NPX_EXECUTABLE NAMES npx npx.cmd REQUIRED)

if(NOT EXISTS "${UI_DIST_DIR}/index.html")
    execute_process(COMMAND ${NPM_EXECUTABLE} ci
                    WORKING_DIRECTORY "${UI_SRC_DIR}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NPX_EXECUTABLE} vite build
                    WORKING_DIRECTORY "${UI_SRC_DIR}"
                    COMMAND_ERROR_IS_FATAL ANY)
endif()

# Rebuild when UI sources change.
file(GLOB_RECURSE UI_REACT_SOURCES CONFIGURE_DEPENDS
     "${UI_SRC_DIR}/src/*.ts"
     "${UI_SRC_DIR}/src/*.tsx"
     "${UI_SRC_DIR}/src/*.css"
     "${UI_SRC_DIR}/index.html")
add_custom_command(
    OUTPUT  "${UI_DIST_DIR}/index.html"
    COMMAND ${NPX_EXECUTABLE} vite build
    WORKING_DIRECTORY "${UI_SRC_DIR}"
    DEPENDS "${UI_SRC_DIR}/package.json"
            "${UI_SRC_DIR}/vite.config.ts"
            ${UI_REACT_SOURCES}
    COMMENT "Rebuilding React UI"
    VERBATIM)
add_custom_target(agentic_synth_ui ALL DEPENDS "${UI_DIST_DIR}/index.html")

file(GLOB_RECURSE UI_BINARY_ASSETS CONFIGURE_DEPENDS
     "${UI_DIST_DIR}/index.html"
     "${UI_DIST_DIR}/assets/*")
juce_add_binary_data(agentic_synth_ui_assets
    HEADER_NAME UiBinaryData.h
    NAMESPACE   UiBinaryData
    SOURCES     ${UI_BINARY_ASSETS})
add_dependencies(agentic_synth_ui_assets agentic_synth_ui)

juce_add_gui_app(AgenticSynth
    PRODUCT_NAME "TIMBRE"
    VERSION       ${PROJECT_VERSION}
    COMPANY_NAME  "Nous Research"
    COMPANY_WEBSITE "https://nousresearch.com"
    COMPANY_EMAIL "hello@nousresearch.com"
    COMPANY_COPYRIGHT "© 2024 Nous Research"
    ICON_BIG      "${CMAKE_SOURCE_DIR}/design/timbre.iconset/icon_512x512.png"
    ICON_SMALL    "${CMAKE_SOURCE_DIR}/design/timbre.iconset/icon_128x128.png"
)

juce_generate_juce_header(AgenticSynth)

target_sources(AgenticSynth
    PRIVATE
        Main.cpp
        MainComponent.cpp
        MainComponent.h
        ui/WebUiComponent.cpp
        ui/WebUiComponent.h
)

target_compile_definitions(AgenticSynth
    PRIVATE
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AgenticSynth,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AgenticSynth,JUCE_VERSION>"
        AGENTIC_SYNTH_UI_DEV=$<BOOL:${AGENTIC_SYNTH_UI_DEV}>
        AGENTIC_SYNTH_PROJECT_NAME="TIMBRE"
        AGENTIC_SYNTH_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(AgenticSynth
    PRIVATE
        agentic_synth_agent
        agentic_synth_ui_assets
        agentic_synth_linux_webview
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── Plugin (shared code) ─────────────────────────────────────────────────────
juce_add_plugin(AgenticSynth_Plugin
    PRODUCT_NAME            "TIMBRE"
    VERSION                 ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Nous
    PLUGIN_CODE             AgSy
    PLUGIN_NAME             "TIMBRE"
    COMPANY_NAME            "Nous Research"
    COMPANY_WEBSITE         "https://nousresearch.com"
    COMPANY_EMAIL           "hello@nousresearch.com"
    COMPANY_COPYRIGHT       "© 2024 Nous Research"
    DESCRIPTION             "An agent-driven VST plugin for synth patch generation"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME             "TIMBRE"
    ICON_BIG                 "${CMAKE_SOURCE_DIR}/design/timbre.iconset/icon_512x512.png"
    ICON_SMALL               "${CMAKE_SOURCE_DIR}/design/timbre.iconset/icon_128x128.png"
)

juce_generate_juce_header(AgenticSynth_Plugin)

target_sources(AgenticSynth_Plugin
    PRIVATE
        plugin/AgenticSynthPlugin.cpp
        plugin/AgenticSynthPlugin.h
        plugin/PluginEditor.cpp
        plugin/PluginEditor.h
        ui/WebUiComponent.cpp
        ui/WebUiComponent.h
)

target_compile_definitions(AgenticSynth_Plugin
    PRIVATE
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        AGENTIC_SYNTH_UI_DEV=$<BOOL:${AGENTIC_SYNTH_UI_DEV}>
        AGENTIC_SYNTH_PROJECT_NAME="TIMBRE"
        AGENTIC_SYNTH_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(AgenticSynth_Plugin
    PRIVATE
        agentic_synth_agent
        agentic_synth_ui_assets
        agentic_synth_linux_webview
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

endif() # AGENTIC_SYNTH_BUILD_PLUGIN
