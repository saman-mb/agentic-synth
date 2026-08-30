<div align="center">

# 🎛️ TIMBRE

### **Say it. Hear it.**

*An agent-driven **VST3 / AU / Standalone Synthesizer** powered by Gemini 2.5, JUCE 8, and C++20.*

[![CI Status](https://img.shields.io/github/actions/workflow/status/saman-mb/agentic-synth/ci.yml?branch=main&style=for-the-badge&logo=github&logoColor=white&label=CI)](https://github.com/saman-mb/agentic-synth/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge&logo=opensourceinitiative&logoColor=white)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?style=for-the-badge&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![JUCE 8](https://img.shields.io/badge/JUCE-8-8DC63F.svg?style=for-the-badge&logo=juce&logoColor=white)](https://juce.com/)
[![React 18](https://img.shields.io/badge/React-18-61DAFB.svg?style=for-the-badge&logo=react&logoColor=black)](https://react.dev/)

[![Platforms](https://img.shields.io/badge/Platforms-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg?style=flat-square&logo=apple&logoColor=white)](#1-requirements)
[![Formats](https://img.shields.io/badge/Formats-VST3%20%7C%20AU%20%7C%20Standalone-7C4DFF.svg?style=flat-square&logo=audio-technica&logoColor=white)](#-build-outputs)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)](CONTRIBUTING.md)

---

</div>

> [!TIP]
> 🎬 **Demo Video Available**: Check out the recorded demo video in [`timbre-demo.mp4`](file:///Users/saman/Dev/agentic-synth/timbre-demo.mp4) showing real-time prompt generation with Gemini 2.5 Flash!

> [!WARNING]
> **Early and experimental.** Builds from source only. The patch format, native bridge API, and UI are actively evolving. See [Known issues](#-known-issues) before filing a bug.

---

## What it does

Type or speak a sound idea — *"a dark, wide pad with movement"* — and an LLM
agent translates it into concrete synthesizer parameters. You get a patch you
can play immediately, tweak by hand, and refine conversationally: *"brighter,
more air"* nudges the existing sound rather than starting over.

Under the hood it is a real subtractive/wavetable synth, not a sample player:
three oscillators, a modulation matrix, envelopes, LFOs, chorus, tube
saturation, delay, and reverb — all implemented in C++ and driven from the
audio thread.

### Highlights

| | |
|---|---|
| 🗣️ **Natural-language patching** | Describe a sound; the agent builds it. Refine in follow-up messages with full context. |
| 🎹 **Playable immediately** | On-screen QWERTY keyboard, hardware MIDI input, and MIDI learn on any knob. |
| 🔀 **Morph & explore** | Generate variations of a patch and morph continuously between them. |
| 🎛️ **Nothing is hidden** | Every generated parameter is a real control you can grab. "Open the hood" for the full synth. |
| 🧠 **RAG + delta-nudging** | Retrieves the closest curated archetype, then asks the LLM for small parameter nudges — more reliable than one-shot generation. |
| 💾 **Keep what you make** | Commit patches to a preset library, or bounce them to 24-bit `.wav`. |
| 🔌 **One binary** | The React UI ships *inside* the plugin window via JUCE 8 `WebBrowserComponent`. No browser tab, no local server. |

---

---

## 🌐 Live demo (web)

The same React UI runs in a browser at the Netlify demo site, backed by a
Netlify Function that calls Gemini 2.5 server-side. The plugin binaries are
untouched — the browser shim swaps the JUCE native bridge for
`fetch("/api/generate")`, and audio renders through a WebAudio approximation
of the C++ engine.

Known approximations vs the native plugin:

- `bpm_sync` is fixed at 120 BPM (WebAudio tempo sync)
- Filter / effect colors differ from the C++ DSP implementations
- Rate limits: 3 generations/minute and 200/day per IP (soft guardrail —
  counters reset on function cold start)

The Gemini key is server-side only — it lives in the Netlify site env vars
and is never shipped to the client.

### Owner deploy checklist

1. Create a Netlify site linked to this repo (deploy from `ui/dist`)
2. Set `NETLIFY_SITE_ID` and `NETLIFY_AUTH_TOKEN` as GitHub repo secrets
3. Set `GEMINI_KEY` in the Netlify site env vars — use a dedicated key on a
   project where billing is never enabled (free tier only)
4. Raise the function timeout to 26 s in the Netlify UI (free-tier default
   is 10 s; the handler enforces its own 24 s deadline)

### Run the web demo locally

```sh
node scripts/sync-prompts.mjs        # generates gitignored prompt constants
cd ui && npm ci && npm run dev       # UI + browser shim on http://localhost:5173
```

The Vite-only server above does **not** serve `/api/generate` — generation
fails there. To exercise the real endpoint locally, install the Netlify CLI
(`npm i -g netlify-cli`) and run from the repo root — one process serves the
UI and the function together (see `[dev]` in `netlify.toml`):

```sh
node scripts/sync-prompts.mjs
cd ui && npm ci && cd ..
GEMINI_KEY=your-key netlify dev      # http://localhost:8888
```

---

## 🚀 Quick start

### 1. Requirements

| Requirement | Notes |
|---|---|
| **CMake** ≥ 3.24 | Build system |
| **C++20 toolchain** | Clang, MSVC, or GCC |
| **Node.js 20** + npm | Builds the React UI |
| **Gemini API key** | Required for LLM patch generation — see [step 3](#3-configure-your-api-key) |
| **WebView runtime** | macOS: WKWebView (built in) · Windows: [WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) · Linux: `libwebkit2gtk-4.1-0` + `-dev` |

### 2. Build

```sh
git clone https://github.com/saman-mb/agentic-synth.git
cd agentic-synth
git submodule update --init --recursive

# Build the React UI first — it is embedded into the binary, so it must
# exist before the CMake build runs.
cd ui && npm ci && npx vite build && cd ..

cmake -S . -B build -DAGENTIC_SYNTH_BUILD_PLUGIN=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### 3. Configure your API key

TIMBRE calls Google Gemini to turn language into patches. Without a key the
agent falls back to a keyword heuristic — it works, but it is much blunter.

```sh
cp .env.example .env
# then edit .env and set GEMINI_KEY=your-key-here
```

`.env` is gitignored. The loader also accepts `GEMINI_KEY` straight from the
environment, and searches for a `.env` in the working directory and up to
three parent directories.

### 4. Run it

> [!IMPORTANT]
> **Run the plugin-format standalone, not the `AgenticSynth` target.**
> `AgenticSynth` is a UI-only shell with no audio device — it renders the
> interface but is silent by design. The target below is the one that makes
> sound.

```sh
cmake --build build --target AgenticSynth_Plugin_Standalone
open build/src/AgenticSynth_Plugin_artefacts/Debug/Standalone/TIMBRE.app
```

On first launch, open **Settings → Audio device → Open** to choose your output
device, sample rate, and MIDI input.

---

## 📦 Build outputs

| Path | What it is |
|---|---|
| `build/src/AgenticSynth_Plugin_artefacts/<config>/Standalone/TIMBRE.app` | **Standalone app with audio** — start here |
| `build/src/AgenticSynth_Plugin_artefacts/<config>/VST3/` | VST3 plugin |
| `build/src/AgenticSynth_Plugin_artefacts/<config>/AU/` | Audio Unit (macOS) |
| `build/src/AgenticSynth_artefacts/<config>/TIMBRE.app` | UI-only shell — **no audio**, for front-end work |

---

## 🛠️ Development

### UI hot-reload

For fast React iteration inside the live JUCE window, point the WebView at the
Vite dev server:

```sh
# Terminal 1
cd ui && npm run dev            # http://localhost:5173

# Terminal 2
cmake -B build -DAGENTIC_SYNTH_UI_DEV=ON
cmake --build build --target AgenticSynth_Plugin_Standalone
open build/src/AgenticSynth_Plugin_artefacts/Debug/Standalone/TIMBRE.app
```

Component edits hot-reload in place. The native bridge behaves exactly as in
production.

### Record a demo

`scripts/record-demo.sh` captures the screen plus app audio into a single mp4.
It needs a virtual audio device (`brew install blackhole-2ch` on macOS) and
Screen Recording permission for your terminal:

```sh
DURATION=60 scripts/record-demo.sh demo.mp4
```

### Project layout

```
agentic-synth/
├── cmake/          # CMake modules
├── docs/           # Architecture, guides, ADRs
├── scripts/        # Model download, plugin validation, demo capture
├── src/
│   ├── agent/      # LLM bridge, telemetry, MIDI learn, morph loop
│   ├── cli/        # Headless patch generation
│   ├── engine/     # DSP: oscillators, filters, envelopes, effects
│   ├── mapper/     # NL → parameters: RAG, samplers, heuristics
│   ├── plugin/     # JUCE AudioProcessor + editor
│   └── ui/         # WebView host and native↔JS bridge
├── tests/          # Catch2 suite
├── third_party/    # JUCE + llama.cpp submodules
└── ui/             # React + TypeScript + Vite front-end
```

---

## 📚 Documentation

| Guide | For |
|---|---|
| [Getting Started](docs/getting-started.md) | Install, launch, first patch |
| [Architecture](docs/architecture.md) | How the pieces fit together |
| [Audio Engine](docs/audio-engine.md) | Signal flow, patch contract, DSP internals |
| [Timbre Profile Map](docs/timbre-profile-map.md) | The sound-design range available |
| [Mod Matrix Guide](docs/mod-matrix-guide.md) | Routing modulation |
| [Local Inference](docs/local-inference.md) | Running an LLM on your own hardware |
| [Build & Release](docs/build-release.md) | Packaging and signing |
| [Privacy Statement](docs/privacy-statement.md) | What leaves your machine |

Full index: [`docs/index.md`](docs/index.md)

---

## 🐛 Known issues

- **`osc0_enabled` does not mute oscillator 0.** Toggling it off leaves the
  rendered audio unchanged — `tests/UnwiredParamsTest.cpp` covers this and
  currently fails. A DSP wiring gap, not a UI bug.
- **AU parameters lack version hints**, which trips a JUCE assertion at startup
  in debug builds. Harmless today; it matters when adding parameters to a
  shipped AU in Logic.
- **No binary releases.** Building from source is the only path right now.

---

## 🤝 Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for the
workflow, coding standards, and commit conventions, and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community expectations.

Good places to start:

- Anything under [Known issues](#-known-issues)
- Issues labelled [`good first issue`](https://github.com/saman-mb/agentic-synth/labels/good%20first%20issue)
- New patch archetypes in `src/mapper/ArchetypeLibrary.cpp` — no C++ audio
  experience needed, just synthesis taste

The project uses [Conventional Commits](https://www.conventionalcommits.org/)
and pre-commit hooks (`pre-commit install`).

---

## 📄 License

[MIT](LICENSE) © Nous Research

---

<div align="center">

*natural-language synthesizer · AI synth plugin · LLM audio · text-to-sound ·
VST3 · Audio Unit · JUCE · C++ synthesizer · generative sound design*

</div>
