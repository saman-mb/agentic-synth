cmake_minimum_required(VERSION 3.24)

project(AgenticSynth VERSION 0.1.0 LANGUAGES C CXX)

option(AGENTIC_SYNTH_BUILD_TESTS  "Build unit tests"                ON)
option(AGENTIC_SYNTH_BUILD_PLUGIN "Build VST3/AU plugin and app"    ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/ProjectOptions.cmake)

if(AGENTIC_SYNTH_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)
    add_subdirectory(src)
endif()

if(AGENTIC_SYNTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
