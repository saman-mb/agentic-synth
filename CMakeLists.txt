cmake_minimum_required(VERSION 3.24)

project(AgenticSynth LANGUAGES CXX)

option(AGENTIC_SYNTH_BUILD_TESTS "Build Agentic Synth tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(AGENTIC_SYNTH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
