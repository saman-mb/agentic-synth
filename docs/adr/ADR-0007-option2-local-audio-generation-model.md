# ADR-0007: Local audio-generation model for Option 2 (text-to-audio)

## Status

Proposed

## Date

2026-05-23

## Deciders

- Saman (project owner)

## Context and Problem Statement

Option 2 (`epic-option-2`) is the neural **text-to-audio** track: prompt →
prompt processor → latent-diffusion audio model → DAC decode → PCM, with a
variation engine (#110), latent-interpolation morphing (#129), and a Python
backend behind a desktop shell. Unlike Option 1 (DSP/parametric synthesis on
`main`), Option 2 generates *actual audio samples* rather than synth patches.

The generation model is the load-bearing dependency. Issue
**#98 ("Stable Audio Open 2 inference pipeline")** names the intended model —
but **no open-weights "Stable Audio Open 2.0" exists.** Stability AI's 2.x line
was trained on AudioSparx data and is contractually barred from an open
release. So #98 targets a model that cannot be self-hosted, and Option 2 needs
a real, available, locally-runnable model selected on the merits.

This ADR records that selection. (Findings are grounded in vendor/HF sources as
of 2026-05-23; see Links. Some are secondary and should be re-confirmed before
implementation.)

## Decision Drivers

- **Open-weight and self-hostable** — no API-only dependency for the core path.
- **Suited to sound design / synth sounds** (TIMBRE's domain), not full songs
  with vocals.
- **Latent diffusion with an accessible autoencoder latent space** — required
  by #99 (latent → PCM decode) and #129 (latent-interpolation morphing).
- **Runs on available hardware** — Framework Desktop (AMD Ryzen AI Max+ 395,
  Radeon 8060S / gfx1151, 124 GB unified RAM). Must run at least on CPU.
- **Latency is non-critical** — generation happens in bursts and offline /
  overnight rendering is acceptable, which favours quality over real-time speed
  and de-risks the AMD-GPU path.

## Considered Options

- Stable Audio Open 1.0
- Stable Audio Open Small
- **Stable Audio 3 — Small SFX (0.6B)** / Small / Medium (open weights)
- Stable Audio 3 — Large (2.7B, **API-only**, not self-hostable)
- Audio Palette (controllable Foley DiT, arXiv 2510.12175)
- MusicGen / AudioLDM2 / EzAudio (other open latent/AR audio models)

## Decision Outcome

Chosen option: **Stable Audio 3 (open-weight tier), released 2026-05-20.**

- **`stabilityai/stable-audio-3-small-sfx` (0.6B)** as the primary generator for
  sound-design / one-shots / textures — TIMBRE's actual domain. (Note: music
  was *filtered out* of its training; it is SFX-focused.)
- **`stabilityai/stable-audio-3-medium`** when *musical* content is wanted, since
  it retains musicality and is still open-weight.
- Run via Stability's **`stable-audio-3` / `stable-audio-tools`** PyTorch
  libraries — **not** HF `diffusers`. The model ships its own autoencoder, which
  supplies the latent space for #99 (decode) and #129 (morphing).

**Excluded:** Stable Audio 3 **Large (2.7B)** — best musicality but **API-only**,
so it cannot satisfy the self-hostable driver. It may be considered later only
as an optional cloud tier (cf. #126, #132).

**Supersedes the model choice in #98.** Issue #98 should be retargeted from the
non-existent "Stable Audio Open 2" to SA3 Small SFX / Medium.

### Hardware feasibility (Framework Desktop)

- **Memory is not a constraint.** SA3 open weights are 0.6B–~1.x B; 124 GB
  unified RAM is far more than enough (even the API-only Large is 2.7B).
- **CPU inference is a confirmed, reliable path** — the SA3 Small SFX card
  documents inference on consumer hardware (e.g. a MacBook Pro M4). Combined
  with the "overnight render is fine" driver, CPU alone is sufficient.
- **ROCm on gfx1151 is an optional accelerator, not a requirement.** PyTorch on
  Strix Halo is now usable via TheRock 7.11 nightlies (LTX-2 audio+video has
  been run on gfx1151), with `HSA_OVERRIDE_GFX_VERSION=11.0.0`; hipBLASLt still
  falls back to hipBLAS, so it is not peak performance and remains
  bleeding-edge. Treat as a speedup to validate, not a dependency.
- Note: this single-box reality (one Framework Desktop, currently serving
  Qwen3-Next-80B-A3B over llama.cpp) differs from the two-box assumption in
  `docs/issues/ISSUE-003-option2-gpu-architecture.md`; that doc and its model
  sizes (e.g. "small-music ~1.5B") should be reconciled with the SA3 figures
  here.

### Consequences / follow-up

- **Gated weights**: SA3 checkpoints require accepting the Stability AI
  Community License and an HF token to download — provisioning must account for
  this (cf. #108 model-download flow, #137 privacy/licensing).
- Integration uses the `stable-audio-3` repo (PyTorch), feeding the DAC/latent
  decode (#99) and exposing the autoencoder latent for morphing (#129).
- The latency-tolerant stance enables high step counts and large seed/CFG
  sweeps (#110) for quality.

## Pros and Cons of the Options

### Stable Audio 3 — Small SFX (0.6B) — CHOSEN (primary)

- Pros: Newest (2026-05-20); open weights; **SFX/sound-design focused** (best
  domain fit); tiny (CPU-friendly, runs on an M4); supports inpainting;
  latent-diffusion with autoencoder latent for #99/#129; ships own inference code.
- Cons: Music filtered from training (poor for musical content); gated weights;
  not `diffusers` (own repo); GPU accel on AMD unverified.

### Stable Audio 3 — Medium — CHOSEN (musical content)

- Pros: Open weights; retains musicality; same family/tooling and latent space.
- Cons: Larger/slower than Small SFX; gated; AMD-GPU path unverified.

### Stable Audio 3 — Large (2.7B)

- Pros: Best musicality; up to ~6-minute tracks.
- Cons: **API-only / not self-hostable** — fails the core driver. Cloud-tier only.

### Stable Audio Open 1.0 / Open Small

- Pros: Open weights; Open 1.0 has mature `diffusers` (`StableAudioPipeline`)
  support; well-documented; 44.1 kHz stereo (≤47 s).
- Cons: Superseded by SA3; older quality. Keep as fallback if SA3's repo proves
  troublesome.

### Audio Palette (arXiv 2510.12175)

- Pros: Extends Stable Audio Open with time-varying control (loudness, pitch,
  spectral centroid, timbre) — attractive for NL "darker/brighter/more movement"
  steering.
- Cons: **Public weights/code unconfirmed**; LoRA adapter for *Foley* on AudioSet
  (not general synth/music). Inspiration for a future control layer, not a
  drop-in.

### MusicGen / AudioLDM2 / EzAudio

- Pros: Open; latent or AR; viable for morphing (AudioLDM2/EzAudio are latent).
- Cons: Weaker sound-design fit than the Stable Audio family; older.

## Links

- Issues: #98 (SA3 inference — retarget), #99 (latent → PCM), #129 (latent
  morphing), #110 (variation engine), #108 (model download), #133 (fine-tune),
  #137 (privacy/licensing)
- `docs/issues/ISSUE-003-option2-gpu-architecture.md` (reconcile model sizes /
  single-box reality)
- [stabilityai/stable-audio-3-small-sfx](https://huggingface.co/stabilityai/stable-audio-3-small-sfx)
- [stabilityai/stable-audio-3-medium](https://huggingface.co/stabilityai/stable-audio-3-medium)
- [Stability AI — Stable Audio 3.0 (open-weight family)](https://stability.ai/news-updates/meet-stable-audio-3-the-model-family-built-for-artistic-experimentation-with-open-weight-models)
- [Stable Audio Open 1.0 — "where is 2.0?" discussion](https://huggingface.co/stabilityai/stable-audio-open-1.0/discussions/20)
- [Stability-AI/stable-audio-3 (inference code)](https://github.com/Stability-AI/stable-audio-3)
- [Audio Palette (arXiv 2510.12175)](https://arxiv.org/abs/2510.12175)
- [PyTorch on gfx1151 / Strix Halo — TheRock nightlies, LTX-2 audio+video](https://github.com/ROCm/TheRock/discussions/2845)
