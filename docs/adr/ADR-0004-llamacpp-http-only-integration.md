# ADR-0004: llama.cpp Integration via HTTP Only

## Status

Accepted

## Date

2026-05-11

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

The agent needs an LLM to produce grammar-constrained JSON patches. `llama.cpp` ships both an embeddable library and a `llama-server` binary exposing `/completion`, `/embedding`, `/health`. We had to decide between linking the library into the plugin or talking to `llama-server` over HTTP.

## Decision Drivers

- Keep the plugin's build graph and runtime small.
- Allow model swap without rebuild.
- Keep licence and distribution boundaries clean.
- Support single-box and two-box deployments via one code path.
- Reduce CI cost: no model weights or GPU toolchains.

## Considered Options

- HTTP-only integration with an external `llama-server`
- In-process linkage against `libllama`

## Decision Outcome

Chosen option: `HTTP-only integration with an external llama-server`

`GrammarSampler` (`src/mapper/GrammarSampler.h`, `.cpp`) holds a `GrammarSamplerConfig` whose `server_url` defaults to `http://127.0.0.1:8080`. Patch generation issues a blocking `POST /completion` with a GBNF grammar body and parses the `content` field. `llama-server` is a *runtime* dependency: the user starts it themselves, on the same machine or on a LAN host. The plugin links no `llama.cpp` code.

This avoids inheriting `llama.cpp`'s build system, GPU toolchains, and licence surface. Model swaps need no rebuild — only an edit to the server's launch flags. The same binary serves single-box and two-box deployments via one URL. CI runs without model weights.

The cost is HTTP latency on top of inference — milliseconds, acceptable because patch generation is already in the seconds range, and the heuristic parser handles the < 200 ms time-to-first-audio path (see `PrePatchPipeline`).

## Pros and Cons of the Options

### HTTP-only integration

- Pros: Small build graph; no GPU toolchain in CI; clean licence boundary; runtime model swap; one code path for single- and two-box; server crashes do not crash the DAW.
- Cons: Adds an HTTP round-trip; users must install and run `llama-server` themselves; new failure modes ("server not running", "wrong port"); needs a setup doc (`docs/local-inference.md`).

### In-process linkage

- Pros: No setup for the user; no HTTP overhead; tighter model-lifecycle control.
- Cons: Pulls `llama.cpp`'s build and GPU back-ends into CMake; model swaps need a rebuild or dynamic loader; licence/distribution constraints leak into the plugin; a model crash takes the DAW down.

## Links

- [src/mapper/GrammarSampler.h](../../src/mapper/GrammarSampler.h)
- [src/mapper/GrammarSampler.cpp](../../src/mapper/GrammarSampler.cpp)
- [docs/local-inference.md](../local-inference.md)
