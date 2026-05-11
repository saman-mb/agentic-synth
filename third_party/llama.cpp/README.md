# llama.cpp submodule integration

# To initialize:
#   git submodule update --init third_party/llama.cpp

# The submodule URL is: https://github.com/ggerganov/llama.cpp
# Pinned to a stable commit for build reproducibility.
#
# Build integration in CMakeLists.txt:
#   add_subdirectory(third_party/llama.cpp)
#   target_link_libraries(agentic_synth_engine PRIVATE llama)
#
# At runtime, the model is loaded via:
#   src/agent/LlamaClient.h/.cpp
