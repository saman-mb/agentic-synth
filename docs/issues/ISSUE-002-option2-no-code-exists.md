---
title: "Option 2 has an architecture doc but zero implementation"
labels: ["option-2", "enhancement", "architecture"]
---

## Description

The repo has a detailed Option 2 architecture document (text-to-audio synthesis
via generative audio models) but no code implementing it. The entire codebase
(currently ~38 source files) is Option 1: parameter-based synthesis with a C++ DSP
engine and an LLM that generates patch parameters.

Option 2 requires a fundamentally different stack:

1. **Python inference server** — wrapping `stable-audio-tools` or the SA3 model
   behind a FastAPI/WebSocket endpoint. Needs GPU acceleration (CUDA or ROCm).
2. **Audio file pipeline** — generated WAVs need storage, preview, loop stitching,
   and export.
3. **VST3 sample player** — a separate JUCE plugin (or a mode in the existing one)
   that loads and plays generated WAV files as a playable instrument.
4. **React UI additions** — generate/play/compare grid, editing layer, preset
   library browser for samples.
5. **Variation engine** — seed + CFG scale sampling, interpolation between samples.
6. **Session memory** — SQLite DB for prompt→sample→rating history.

This is effectively a second product alongside the existing Option 1 synth.
Decisions needed:

### Architecture decision: two plugins or one?

- **Separate VST3**: Clean separation, independent binaries. User chooses which to
  load in their DAW. More build complexity, duplicated UI shell.
- **Mode switch**: Single plugin with Option 1 / Option 2 toggle. Shared UI shell,
  shared MIDI handling. More complex state management, one binary.

### Deployment model

- Option 2's inference server (Python + model weights) could be:
  - **Local on DAW machine** — requires GPU on the user's machine. Limits audience.
  - **Remote on Framework Desktop** — like the current llama.cpp setup. Works if
    user has a second machine with a GPU.
  - **Cloud tier** — optional paid option for laptop users without GPU.

### Suggested first step

Phase 1 from the doc: standalone Python script that takes a text prompt and
generates a WAV file using SA3. Validate latency, quality, and hardware
requirements before investing in the plugin integration.
