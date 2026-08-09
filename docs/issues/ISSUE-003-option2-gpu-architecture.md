---
title: "Option 2 GPU requirements conflict with current two-box architecture"
labels: ["option-2", "infrastructure", "hardware"]
---

## Description

The current TIMBRE architecture (Option 1) uses a **two-box model**:
- **Mac Mini (DAW)** — runs the VST3 plugin, React UI, and lightweight LLM
  prompt processing (Phi-4/Llama 3.2 8B via llama.cpp HTTP).
- **Framework Desktop** — runs the heavy LLM inference (70B model via
  llama.cpp server) over LAN.

Option 2 (text-to-audio) introduces a **new GPU dependency**: Stable Audio 3
needs a GPU with significant VRAM (~6GB+ for medium model, less for small).

### The problem

- **Framework Desktop** (AMD Ryzen AI Max+ 395, Radeon 8060S, 128GB unified):
  Can run SA3. But it's already running the 70B LLM for Option 1.
  Can it run both simultaneously? The 70B Q4 takes ~40GB, SA3 medium maybe
  ~6-12GB. Total ~52GB — leaves ~76GB for context. Might work but needs
  testing for GPU memory contention on the Radeon 8060S.
- **Mac Mini**: M4 Pro has a shared GPU that might be too slow for real-time
  SA3 inference. The smallest model (SA3 small-music, ~1.5B params) might
  work but needs testing.

### Options for resolution

1. **Dedicated Framework Desktop pipeline** — Route all SA3 inference to the
   Framework Desktop alongside the LLM. Add a second inference server
   endpoint. Risk: GPU memory contention.
2. **Mac Mini runs small model** — SA3 small-music might run acceptably on
   M4 Pro. Need to benchmark.
3. **Cloud fallback** — Optional cloud GPU tier for when local hardware isn't
   enough. More complex but future-proof.
4. **Sequential inference** — Option 1 and Option 2 can't run simultaneously.
   When generating audio, pause LLM inference. Acceptable since generation
   happens in bursts during patch creation.

### Action needed

Benchmark SA3 small-music and medium on:
- Framework Desktop (Radeon 8060S, ROCm or CPU fallback)
- Mac Mini M4 Pro

Before committing to the architecture.
