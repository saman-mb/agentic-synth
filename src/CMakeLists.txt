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
# Core synth engine and agent bridge (shared by app and plugin)
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
    agent/AgentBridge.cpp
    agent/SessionMemory.cpp
    agent/PrePatchPipeline.cpp
    agent/StreamParser.cpp
    agent/Telemetry.cpp
)

target_include_directories(agentic_synth_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_engine)

target_link_libraries(agentic_synth_engine
    PUBLIC
        agentic_synth_mapper
        juce::juce_audio_basics
        juce::juce_audio_processors
)
# ── Standalone App ───────────────────────────────────────────────────────────
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
)

target_compile_definitions(AgenticSynth
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AgenticSynth,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AgenticSynth,JUCE_VERSION>"
)

target_link_libraries(AgenticSynth
    PRIVATE
        agentic_synth_engine
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
)

target_compile_definitions(AgenticSynth_Plugin
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AgenticSynth_Plugin
    PRIVATE
        agentic_synth_engine
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
