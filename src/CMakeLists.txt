# ── Mapper Library ───────────────────────────────────────────────────────────
# Pure C++ keyword → PatchStruct mapping; no JUCE dependency
add_library(agentic_synth_mapper STATIC
    mapper/HeuristicParser.cpp
    mapper/GrammarSampler.cpp
    mapper/SemanticMapper.cpp
)
target_include_directories(agentic_synth_mapper PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_mapper)

# ── Engine Library ───────────────────────────────────────────────────────────
# Core synth DSP only — no agent code.  Depends on mapper and JUCE audio types.
add_library(agentic_synth_engine STATIC
    engine/SynthEngine.cpp
    engine/ADSREnvelope.cpp
    engine/WavetableOscillator.cpp
    engine/VAOscillator.cpp
    engine/Filter.cpp
    engine/LFO.cpp
    engine/VoiceManager.cpp
    engine/PatchValidator.cpp
    engine/VariationEngine.cpp
    engine/MidiHandler.cpp
    engine/MorphEngine.cpp
    engine/StyleTransfer.cpp
    engine/PresetExporter.cpp
    engine/PresetDatabase.cpp
    engine/PatchStateManager.cpp
    engine/MultiModalInput.cpp
)
target_include_directories(agentic_synth_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_engine)

target_link_libraries(agentic_synth_engine
    PUBLIC
        agentic_synth_mapper
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_data_structures
)
# ── Agent Library ────────────────────────────────────────────────────────────
# AI agent layer: depends on engine_lib, never the other way around.
add_library(agentic_synth_agent STATIC
    agent/AgentBridge.cpp
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
    PRODUCT_NAME "Agentic Synth"
    VERSION       ${PROJECT_VERSION}
    COMPANY_NAME  "Nous Research"
    COMPANY_WEBSITE "https://nousresearch.com"
    COMPANY_EMAIL "hello@nousresearch.com"
    COMPANY_COPYRIGHT "© 2024 Nous Research"
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
        AGENTIC_SYNTH_PROJECT_NAME="Agentic Synth"
        AGENTIC_SYNTH_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(AgenticSynth
    PRIVATE
        agentic_synth_agent
        agentic_synth_ui_assets
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── Plugin (shared code) ─────────────────────────────────────────────────────
juce_add_plugin(AgenticSynth_Plugin
    PRODUCT_NAME            "Agentic Synth"
    VERSION                 ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Nous
    PLUGIN_CODE             AgSy
    PLUGIN_NAME             "Agentic Synth"
    COMPANY_NAME            "Nous Research"
    COMPANY_WEBSITE         "https://nousresearch.com"
    COMPANY_EMAIL           "hello@nousresearch.com"
    COMPANY_COPYRIGHT       "© 2024 Nous Research"
    DESCRIPTION             "An agent-driven VST plugin for synth patch generation"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME             "Agentic Synth"
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
        AGENTIC_SYNTH_PROJECT_NAME="Agentic Synth"
        AGENTIC_SYNTH_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(AgenticSynth_Plugin
    PRIVATE
        agentic_synth_agent
        agentic_synth_ui_assets
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

endif() # AGENTIC_SYNTH_BUILD_PLUGIN
