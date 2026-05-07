add_library(
  agentic_synth_engine
  engine/SynthEngine.cpp
  agent/AgentBridge.cpp)

target_include_directories(agentic_synth_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
agentic_synth_configure_target(agentic_synth_engine)

add_executable(agentic-synth-placeholder main.cpp)
target_link_libraries(agentic-synth-placeholder PRIVATE agentic_synth_engine)
agentic_synth_configure_target(agentic-synth-placeholder)
