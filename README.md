# Agentic Synth

An agent-driven VST plugin / desktop app that lets producers describe synth sounds in natural language and get a playable patch.

## Vision

Speak or type a sound idea like *"a dark, wide pad with movement in D minor"* — the agent interprets it, builds the patch, you tweak, it adapts. From simple descriptions to complex ethereal soundscapes with evolving rhythmic elements.

## Current Status

This repository is in early scaffold state.

- The placeholder C++ test target can be configured, built, and tested with CMake.
- The JUCE desktop/plugin app source exists under `src/`, but it is not wired into the root CMake build yet.
- `third_party/JUCE` is currently empty, so the JUCE app/plugin cannot build until JUCE is added.
- The `ui/` folder contains a Vite/React companion UI with `dev`, `build`, `preview`, and `lint` scripts.

## Getting Started

See [docs/architecture.md](docs/architecture.md) for the high-level system architecture.

### Build Placeholder Target

From the repository root:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

This validates the current placeholder C++ test setup.

### Run The App

The companion UI can be started locally:

```sh
cd ui
npm install
npm run dev
```

Vite will print the local URL, usually `http://localhost:5173/`.

The JUCE desktop/plugin app is not runnable yet.

The expected future commands are likely:

```sh
# Desktop/plugin build, after JUCE is added and src/ is wired into CMake
cmake -S . -B build
cmake --build build
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
