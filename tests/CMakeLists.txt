include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(agentic_synth_tests
    placeholder_test.cpp
    WavetableOscillatorTest.cpp
    ../src/engine/WavetableOscillator.cpp
)
target_include_directories(agentic_synth_tests PRIVATE ../src)
target_link_libraries(agentic_synth_tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(agentic_synth_tests)
