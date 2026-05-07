include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# Standalone engine lib for tests — no JUCE dependency (root CMake doesn't add src/).
add_library(voice_manager_test_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/engine/VoiceManager.cpp
)
target_include_directories(voice_manager_test_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(voice_manager_test_lib PUBLIC cxx_std_20)

add_executable(agentic_synth_tests
    placeholder_test.cpp
    VoiceManagerTest.cpp
)
target_link_libraries(agentic_synth_tests
    PRIVATE
        Catch2::Catch2WithMain
        voice_manager_test_lib
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(agentic_synth_tests)
