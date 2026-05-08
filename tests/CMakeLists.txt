include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(agentic_synth_tests
    placeholder_test.cpp
    ADSREnvelopeTest.cpp
    WavetableOscillatorTest.cpp
    VAOscillatorTest.cpp
    FilterTest.cpp
    LFOTest.cpp
    VoiceManagerTest.cpp
    MidiHandlerTest.cpp
    HeuristicParserTest.cpp
    SessionMemoryTest.cpp
    PatchValidatorTest.cpp
    PrePatchTest.cpp
    VariationEngineTest.cpp
    GrammarSamplerTest.cpp
    SemanticMapperTest.cpp
    StreamParserTest.cpp
    Phase1AcceptanceTest.cpp
    Phase2AcceptanceTest.cpp
    Phase3AcceptanceTest.cpp
    RealtimeSafetyTest.cpp
    PresetDatabaseTest.cpp
)

target_include_directories(agentic_synth_tests PRIVATE ../src)

target_link_libraries(agentic_synth_tests
    PRIVATE
        Catch2::Catch2WithMain
        agentic_synth_engine
        agentic_synth_agent
        agentic_synth_mapper
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(agentic_synth_tests)
