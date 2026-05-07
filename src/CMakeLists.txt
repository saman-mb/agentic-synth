# ── Standalone App ───────────────────────────────────────────────────────────
juce_add_gui_app(AgenticSynth
    PRODUCT_NAME "Agentic Synth"
    VERSION       ${PROJECT_VERSION}
    COMPANY_NAME  "Nous Research"
    COMPANY_WEBSITE "https://nousresearch.com"
    COMPANY_EMAIL "hello@nousresearch.com"
    COMPANY_COPYRIGHT "© 2024 Nous Research"
)

# Generate JuceHeader.h for convenience
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
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
)

target_compile_definitions(AgenticSynth_Plugin
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AgenticSynth_Plugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
