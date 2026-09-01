# RFC: C++ DSP Core C API + Bridges (JSI, WASM)

- **Status:** Approved — principal-engineer / captain sign-off on gate story #304 (2026-08-31)
- **Stories built on this:** #305 (C++ core + C API), #306 (WASM → web demo), #307 (golden-file parity), #308 (JSI bridge)
- **Supersedes nothing; grounded in:** ADR-0001 (JUCE 7), ADR-0008 (Nx boundaries, `engine-bridge` seam), `docs/audio-engine.md`

## 1. Starting point (what already exists)

The synthesis core is **not greenfield**. `src/engine/` already contains a working,
JUCE-light DSP engine behind CMake target `agentic_synth_engine_core`:

- `PatchStruct` — POD, fixed-size, trivially copyable, versioned (`kPatchStructVersion = 1`), SPSC-queue-safe. 3 osc slots, filter, 2 envelopes, 2 LFOs, reverb, delay, chorus, tube-sat, reverb-send HPF, master gain, portamento, voice count, rationale/action-log char buffers.
- `VoiceManager` — 16-voice polyphony, voice stealing, PolyBLEP VA + wavetable + 2-op FM + noise oscillators, Moog ladder + SVF filters, ADSR, LFOs, delay/reverb bus, param smoothing.
- `MidiHandler` — JUCE-free raw MIDI; `PatchValidator` — clamping/finite checks; `SPSCQueue`, `RealtimeSafety.h`, `ParamSmoother`.
- `OfflineRenderer::renderPatchToBuffer()` — synchronous deterministic off-thread bounce of a patch to a float buffer (currently pulls in `juce_core`).

The JS seam also exists: `libs/engine-bridge` exports the `SynthEngine` interface +
`createSynthEngine()` factory (WebAudio implementation today). ADR-0008 already
designates WASM and JSI as **additional implementations of that same interface**.

**Consequence:** this epic is about **stabilizing a C API over the existing core and
building two bridges**, not writing DSP. Risk concentrates in the API surface and the
JUCE-free decoupling, not in synthesis math.

## 2. C API surface (draft `agsynth.h`)

New target `agentic_synth_capi` (C library, links `agentic_synth_engine_core`),
new directory `src/capi/`. C ABI, opaque handle, no C++ types cross the boundary:

```c
// agsynth.h — stable C API. Semver'd; breaking changes bump major version.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ags_engine ags_engine;   // opaque
typedef struct ags_patch ags_patch_t;   // opaque; POD snapshot inside

// Lifecycle — engine construction may allocate; render may not.
ags_engine* ags_engine_create(double sample_rate, int max_block);
void ags_engine_destroy(ags_engine*);

// Patch: apply whole POD snapshot (validated+clamped internally).
// Returns AGS_OK or AGS_ERR_PARAM (never crashes on bad input).
int ags_engine_set_patch(ags_engine*, const void* patch_struct_bytes, uint32_t len);

// Individual param at UI/automation rate: dotted path, e.g. "osc.0.cutoff_hz".
// Same path vocabulary as libs/engine-bridge paramMap today.
int ags_engine_set_param(ags_engine*, const char* path, float value);

// Events: sample-accurate within the next rendered block.
typedef struct { uint32_t kind; /* AGS_EVENT_NOTE_ON/OFF/CC */ 
                 uint8_t note, velocity, cc; uint32_t sample_offset; } ags_event;
int ags_engine_push_events(ags_engine*, const ags_event* events, uint32_t count);

// Render. out is caller-owned interleaved stereo (or mono), frames long.
// Contract: no allocation, no locks, no syscalls inside this call.
int ags_engine_render(ags_engine*, float* out_interleaved, uint32_t frames, uint32_t channels);

// Deterministic offline render for parity tests (fresh engine per call).
int ags_render_offline(const void* patch_bytes, uint32_t patch_len,
                       const ags_event* events, uint32_t event_count,
                       double sample_rate, uint32_t frames,
                       float* out_interleaved);

// State save/restore (POD serialization — plugin state, app suspend).
int ags_state_size(const ags_engine*, uint32_t* len);
int ags_state_save(const ags_engine*, void* buf, uint32_t len);
int ags_state_load(ags_engine*, const void* buf, uint32_t len);

// Error codes
enum { AGS_OK = 0, AGS_ERR_PARAM = 1, AGS_ERR_SIZE = 2, AGS_ERR_STATE = 3, AGS_ERR_NULL = 4 };

#ifdef __cplusplus
}
#endif
```

Design rules:

1. **Mirror `PatchStruct` for the patch snapshot.** `ags_patch_t` wraps the existing
   POD struct; `set_patch` takes a versioned byte blob and validates it (header check,
   `PatchValidator` clamp) before applying. One SSOT — no second param model.
2. **Dotted-path single params** reuse the exact path vocabulary
   `libs/engine-bridge/src/lib/paramMap.ts` already uses (`osc.0.cutoff_hz`,
   `filter.cutoff_hz`, …). WebAudio and C-API engines accept identical paths.
3. **Errors are return codes, never exceptions/crashes.** Fuzzed/malformed input
   (bad version, wrong size, NaN/Inf fields) → `AGS_ERR_*`. AC of #305.
4. **No globals.** Every `ags_engine*` is independent → multiple instances per process
   (plugin multi-instance, RN view pool).
5. **Event queue, not callbacks.** Hosts/bridges push events with `sample_offset`
   before `render`; the engine consumes them sample-accurately inside the block.
   This is the VST3/CLAP-compatible shape (§6).

## 3. Param/state/message model — resolving the #291 mismatch

Three layers, one direction of truth:

| Layer | Contract | Notes |
| --- | --- | --- |
| TS (web + RN UI) | `PatchParams` JSON (`libs/shared-types`) | What agents/codec/UI produce today |
| C ABI | `ags_patch_t` = `PatchStruct` bytes | What the engine consumes |
| DSP | `PatchStruct` internal fields | Unchanged |

**Known drift, resolved as follows:**

- TS `PatchParams` **lacks** `chorus`, `tubesat`, `reverb_send_hpf_hz` (present in
  C++ `PatchStruct` since #265). Resolution: the bridge layer fills documented
  defaults (chorus mix 0, tubesat drive 0, HPF 0 = bypass) — bit-exact bypass, so
  older TS patches render identically. A follow-up story should extend
  `libs/shared-types` so the JSON contract is the full surface; **not** a #305
  blocker (defaults are already the engine's bypass semantics).
- `rationale` / `augmenter_actions` are engine-external metadata — the C API does
  **not** carry them in the audio patch; they stay in the TS/codec layer.
- Param ranges: `PatchValidator` remains the final clamp (defense in depth), same
  as the plugin path today.

E2's schema ownership (#291) is satisfied: the C API renders against the shared
patch contract as it exists in `libs/shared-types` + `PatchStruct`, with the mapping
living in exactly one place per bridge.

## 4. Sample-accurate processing contract

- `render` consumes the pushed event queue sorted by `sample_offset`; note on/off
  and CC take effect at the exact sample, matching the plugin's current
  per-block MIDI drain semantics.
- Param changes (`set_param`) apply at block boundary with the existing
  `ParamSmoother` de-zipper path — identical behavior to APVTS today.
- **Determinism:** fixed (patch bytes, event sequence, sample rate) → bit-identical
  renders. The engine already avoids nondeterminism in the render path; the only
  known risk is `analog-style drift` in `VAOscillator` (random target drift).
  Resolution: drift PRNG becomes a seeded xorshift initialized from
  `(patch_id, voice_index)` — deterministic per render, still audible as drift.
  This is an explicit, reviewed change (#305 scope).
- Offline render (`ags_render_offline`) is the golden-file entry point; it
  constructs a fresh engine per call (same pattern as `OfflineRenderer` today).

## 5. Real-time safety (no-alloc render path)

Rules (already documented in `docs/audio-engine.md`, now asserted):

- Inside `ags_engine_render`: no heap allocation, no locks, no syscalls, no
  exceptions. All buffers pre-allocated in `create`/`set_patch`.
- Debug builds compile a **custom scoped allocator** (or `malloc` hook shim) that
  aborts on allocation inside the render call — asserted in tests (#305 AC).
- `SPSCQueue` remains the only cross-thread primitive; the C API surface is
  single-thread-per-engine by contract (bridges serialize; documented).

## 6. Plugin viability (VST3/CLAP constraints)

Checked against the drafted API:

| Constraint | VST3/CLAP requirement | API shape | Verdict |
| --- | --- | --- | --- |
| Host-controlled block sizes | process(0..N frames) | `ags_engine_render(frames)` | OK |
| Sample-accurate events | event queues with offsets | `ags_event` + `sample_offset` | OK |
| Multi-instance | no globals/statics | opaque handle only | OK |
| State save/restore | chunk-based | `ags_state_save/load` (POD blob + version) | OK |
| Parameter IDs for automation | stable int IDs | dotted paths now; a path→ID table can be added additively | OK (additive) |
| No UI coupling in DSP | — | none exists | OK |

Nothing in the API precludes wrapping in JUCE (VST3/AU, ADR-0001) or CLAP later.
Future JUCE plugin can adopt `ags_engine_*` behind `PluginProcessor` instead of
calling `VoiceManager` directly — noted as allowed, not required (#292 non-goal:
no plugin work now; requirement: don't preclude it).

## 7. JSI bridging strategy (#308)

- New Nx lib (or sibling file in `libs/engine-bridge`, per ADR-0008 "or a sibling
  file"): `SynthEngine` implementation backed by JSI, factory-selected on native.
- C++ side: a thin JSI host object wrapping `ags_engine_*` — **the bridge calls the
  C API, never `VoiceManager` directly** (one boundary, both bridges).
- Marshalling: `setPatch` receives `PatchParams` JSON → converts to `PatchStruct`
  bytes **once per patch** (not per block) on the JS thread; `setParam` →
  `ags_engine_set_param` (cheap, UI-rate); note events → `ags_event` push.
- Audio path: native audio output (Expo AV / os-facing thread) calls
  `ags_engine_render` directly on the real-time thread; **no JS on the audio
  path**. JSI is control-rate only.
- Lifecycle: background/foreground → `ags_state_save`/`ags_state_load` around
  engine destroy/recreate on sample-rate change; no leaked resources (AC).
- Errors: JS-side exceptions/malformed JSON → typed JS error object; native layer
  never throws across the boundary (AC).
- Independently testable via a minimal RN harness before E5 integrates it (AC) —
  reuses #307's golden fixtures, no new parity scheme.

## 8. WASM toolchain plan (#306)

- **Single toolchain:** Emscripten compiles the existing `agentic_synth_capi`
  target unchanged. Core code stays ISO C++; any `#ifdef __EMSCRIPTEN__` is
  confined to the glue target (`src/wasm/`), never `src/engine/` or `src/capi/`.
  (AC: no Emscripten-only leakage.)
- `build-wasm` Nx/CMake target emits `agsynth.wasm` + `agsynth.js` glue
  reproducibly in CI; fixed Emscripten version pinned.
- WASM build must be **JUCE-free** — see §10 (decoupling).
- Glue implements the `SynthEngine` interface (`ensureStarted` instantiates the
  module; `setPatch`/`setParam`/`noteOn`/`noteOff` map 1:1 onto C API calls);
  `apps/web` swaps engines via the existing factory — **no UI changes** beyond
  factory selection.
- Missing WASM support / module-init failure → `ensureStarted()` rejects → demo
  shows a clear fallback/error state (AC: no silent broken audio, no blank
  screen). Graceful degradation follows the existing `addModule` failure pattern
  in `engine.ts`.
- Renders `PatchParams` JSON from the same patch-JSON contract as mobile (AC).

## 9. Golden-file parity (#307)

- Reference renders: generated from the **current WebAudio engine**
  (`libs/engine-bridge`) for a representative corpus (waveforms, filter sweeps,
  envelopes, LFO, typical presets) via a headless render script.
- Generation procedure documented + committed (`tests/golden/`).
- CI runs `ags_render_offline` (native) and the WASM module over the corpus;
  compares to goldens with **documented tolerance** (per-sample RMS + peak bounds).
  Tolerance rationale (float op ordering between WebAudio graph and C++ graph,
  FMA/denormal handling) written into the test fixture README — not tuned to pass.
- Deliberately divergent DSP change → parity fails CI, blocks merge (AC).
- WASM vs native: bit-for-bit expected (same C++ code, IEEE 754 both) — assert
  bit-identity first, relax to documented tolerance only with a recorded reason.

## 10. JUCE-free decoupling (prerequisite, inside #305 scope)

Current state: `agentic_synth_engine_core` is "JUCE-light" but `OfflineRenderer.h`
includes `juce_core` (WAV write uses `juce::File`); parts of `VoiceManager` pull
`juce_core` headers. For WASM (no JUCE) and the no-dep C library:

- Move WAV writing out of the core (test/tooling layer, `std::filesystem`).
- Replace residual `juce_core` uses in the render path with std equivalents
  (`std::atomic`, `std::min/max`, fixed math helpers).
- Acceptance: `agentic_synth_capi` compiles with `-Wall -Wextra -Werror` on gcc +
  clang, links **no external deps**, zero warnings (AC of #305). JUCE stays for
  plugin/standalone shells only.

## 11. Build / CI sequencing

- New CMake targets: `agentic_synth_capi` (C lib), `agsynth_tests` additions for
  fuzz/determinism/no-alloc tests. Nx project tags for `src/` per ADR-0008 tag
  scheme.
- **#282 (clang-format) and #283 (CI hardening) are still open** — C++ CI work for
  this epic is sequenced behind them. #305's PR must not introduce a parallel
  formatting/lint regime.
- CI additions (behind #283's framework): capi build matrix (gcc/clang),
  determinism test, fuzz smoke test, golden parity job, `build-wasm` job.

## 12. Open questions (recommendation each; sign-off may override)

1. **Drift determinism** — make `VAOscillator` drift seeded/deterministic as §4?
   *Recommend: yes*; audible drift preserved, determinism AC achievable.
2. **Param IDs for host automation** — ship a stable path→int-ID table now or
   defer to plugin phase? *Recommend: defer* (additive later, non-breaking).
3. **Audio output on RN** — Expo AV vs a small native AudioStream wrapper owning
   the render thread. *Recommend: native wrapper* (Expo AV latency/queue control
   is insufficient for glitch-free real-time; JSI control-rate only either way).
4. **TS schema extension for chorus/tubesat** — separate follow-up story vs fold
   into #306? *Recommend: follow-up story under #292 after #306 proves the
   bridge*; defaults keep current patches bit-exact until then.
5. **Corpus size** — how many patches in the golden corpus? *Recommend: 10–15
   covering each osc type, filter modes, both LFOs, FX extremes, plus 3 typical
   presets.*

## 13. What sign-off approves

- C API surface (§2) as the stable contract for #305/#306/#308
- Param model resolution (§3) incl. documented-default bridging
- Determinism approach incl. seeded drift (§4)
- No-alloc enforcement approach (§5)
- WASM toolchain shape + leakage rule (§8)
- JSI control-rate-only strategy (§7)
- CI sequencing behind #282/#283 (§11)

Change list instead of approval → re-review of the revised RFC before #305 starts.
