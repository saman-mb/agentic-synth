# Agentic Synth

An agent-driven VST plugin / desktop app that lets producers describe synth sounds in natural language and get a playable patch.

## Vision

Speak or type a sound idea like *"a dark, wide pad with movement in D minor"* — the agent interprets it, builds the patch, you tweak, it adapts. From simple descriptions to complex ethereal soundscapes with evolving rhythmic elements.

## Getting Started

See [docs/architecture.md](docs/architecture.md) for the high-level system architecture.

### Build Placeholder Target

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Project Structure

```
agentic-synth/
├── cmake/              # CMake build modules
├── docs/               # Architecture, design documents
├── src/                # C++ engine and agent bridge
├── tests/              # C++ placeholder tests
├── third_party/        # JUCE and llama.cpp submodules
├── ui/                 # React + TypeScript + Vite companion UI
├── CMakeLists.txt      # Root CMake entry point
├── LICENSE             # Project license
└── README.md
```
