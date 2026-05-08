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
    ../src/engine/ADSREnvelope.cpp
    WavetableOscillatorTest.cpp
    ../src/engine/WavetableOscillator.cpp
    VAOscillatorTest.cpp
    ../src/engine/VAOscillator.cpp
    FilterTest.cpp
    ../src/engine/Filter.cpp
    LFOTest.cpp
    ../src/engine/LFO.cpp
    VoiceManagerTest.cpp
    ../src/engine/VoiceManager.cpp
    MidiHandlerTest.cpp
    ../src/engine/MidiHandler.cpp
    HeuristicParserTest.cpp
    ../src/mapper/HeuristicParser.cpp
    SessionMemoryTest.cpp
    ../src/agent/SessionMemory.cpp
    ../src/agent/AgentBridge.cpp
    PatchValidatorTest.cpp
    ../src/engine/PatchValidator.cpp
    PrePatchTest.cpp
    ../src/agent/PrePatchPipeline.cpp
    VariationEngineTest.cpp
    ../src/engine/VariationEngine.cpp
    GrammarSamplerTest.cpp
    ../src/mapper/GrammarSampler.cpp
    SemanticMapperTest.cpp
    ../src/mapper/SemanticMapper.cpp
    StreamParserTest.cpp
    ../src/agent/StreamParser.cpp
    ../src/agent/Telemetry.cpp
)
target_include_directories(agentic_synth_tests PRIVATE ../src)
target_link_libraries(agentic_synth_tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(agentic_synth_tests)
