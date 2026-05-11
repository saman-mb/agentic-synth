# ADR-0006: Single-Box Default, Two-Box LAN as an Enthusiast Extension

## Status

Accepted

## Date

2026-05-11

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

`docs/local-inference.md` originally framed a two-machine setup as the supported configuration: a Framework Desktop (Ryzen AI Max+ 395, 128 GB) running `llama-server`, talking over LAN to a Mac Mini running the DAW. A product review flagged that requiring a second dedicated inference box collapses the addressable user base to a tiny enthusiast slice. We need a default any user with one capable machine can run, while still letting enthusiasts split the workload.

## Decision Drivers

- Most prospective users have one machine, not two.
- The two-box setup is useful for users with a high-VRAM box and a low-noise DAW box.
- CI must not depend on LAN topology.
- The plugin should not branch on deployment mode.

## Considered Options

- Single-box default, two-box reachable via server URL (chosen)
- Two-box default, single-box fallback
- Embedded-only single-box (link the LLM into the plugin)

## Decision Outcome

Chosen option: `Single-box default, two-box reachable via server URL`

`GrammarSamplerConfig::server_url` defaults to `http://127.0.0.1:8080` (see `src/mapper/GrammarSampler.h`). Installing `llama-server` and the plugin on the same machine is fully supported with no extra steps. Inference on a LAN host requires only editing the URL — same code path, no recompile, no feature flag. `docs/local-inference.md` is positioned as a two-box *guide* with a single-box note in the first paragraph, not a hard requirement.

This composes cleanly with ADR-0004: the transport is identical in both deployments, so test coverage applies to both, and CI runs everything against a local server.

## Pros and Cons of the Options

### Single-box default, two-box opt-in

- Pros: Works for the single-machine majority; advanced users get split deployment free; one code path; CI stays local.
- Cons: Docs must make the single-box path obvious; under-spec single boxes need clear minimum-hardware guidance.

### Two-box default

- Pros: Best ceiling for users with a second machine.
- Cons: Exclusionary; requires LAN in docs and CI; bad first impression.

### Embedded-only single-box

- Pros: No setup; no HTTP.
- Cons: Conflicts with ADR-0004; removes the LAN option; ties the plugin to one loader and quant strategy.

## Links

- [docs/local-inference.md](../local-inference.md)
- [src/mapper/GrammarSampler.h](../../src/mapper/GrammarSampler.h)
- [ADR-0004](./ADR-0004-llamacpp-http-only-integration.md)
