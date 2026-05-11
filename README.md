# Agentic Synth

An agent-driven VST plugin / desktop app that lets producers describe synth sounds in natural language and get a playable patch.

## Vision

Speak or type a sound idea like *"a dark, wide pad with movement in D minor"* — the agent interprets it, builds the patch, you tweak, it adapts. From simple descriptions to complex ethereal soundscapes with evolving rhythmic elements.

## Current Status

The React UI now ships **inside** the JUCE plugin window via JUCE 8's
`WebBrowserComponent`. One binary, no browser tab needed, no WebSocket
server. The VST3, AU, and Standalone targets all render the same React
front-end through a native↔JS bridge to the C++ agent.

- C++ engine + agent live under `src/`, built with CMake.
- React UI lives under `ui/` (Vite + TypeScript). `npx vite build` produces
  `ui/dist/` which is embedded into the plugin binary via
  `juce_add_binary_data`.
- WebView backends: WebView2 on Windows, WKWebView on macOS, WebKitGTK
  (4.1) on Linux.

## Getting Started

### Prerequisites

- CMake 3.24 or newer.
- A C++20-capable toolchain (Clang, MSVC, or GCC).
- Node.js 20 + npm (for the UI build).
- Platform WebView runtime:
  - **Windows**: WebView2 Runtime (usually preinstalled on Windows 11; the
    installer can auto-fetch on older systems).
  - **macOS**: WKWebView (system-provided, no entitlement needed for
    bundled assets).
  - **Linux**: `libwebkit2gtk-4.1-0` and the matching `-dev` package.

### Production Build

From the repository root:

```sh
git submodule update --init --recursive
cd ui && npm ci && npx vite build && cd ..
cmake -S . -B build -DAGENTIC_SYNTH_BUILD_PLUGIN=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build outputs:

- `build/src/AgenticSynth_artefacts/.../AgenticSynth` — standalone app.
- `build/src/AgenticSynth_Plugin_artefacts/Release/VST3/...` — VST3.
- `build/src/AgenticSynth_Plugin_artefacts/Release/AU/...` — AU (macOS).
- `build/src/AgenticSynth_Plugin_artefacts/Release/Standalone/...` —
  plugin-format standalone.

### UI Hot-Reload Dev Loop

For fast UI iteration with the live JUCE host, point the WebView at the
Vite dev server:

```sh
# Terminal 1
cd ui && npm run dev   # serves at http://localhost:5173

# Terminal 2
cmake -B build -DAGENTIC_SYNTH_UI_DEV=ON
cmake --build build --target AgenticSynth
./build/src/AgenticSynth_artefacts/AgenticSynth.app/Contents/MacOS/AgenticSynth
```

Edits to React components hot-reload inside the JUCE window. The native
bridge stays wired the same way as in production.

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
