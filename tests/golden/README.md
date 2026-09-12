# Golden-file corpus (`#307`)

Reference renders for WASM ↔ native ↔ WebAudio parity. Generated from the
**current WebAudio graph** (`VoiceManager` → `createEffectRack` → `GainNode`),
not from `WebSynthEngine` and not from the C++ core.

## Tolerances (fixed a priori)

These bounds were chosen from the known graph mismatch **before** any PCM was
produced. They are not tuned against a failing comparator.

### WASM ↔ native

Bit-identical: `memcmp` on the interleaved `f32le` buffers. Same C++ translation
unit, IEEE 754, seeded RNGs. A divergence here is a real bug, not FMA noise —
do not relax this without a recorded reason.

**Recorded exception — `fm` only:** the 2-op FM oscillator calls `std::sin` on
both the modulator and the carrier every sample (`VoiceManager.cpp`). glibc
(CI `build-test`) and Emscripten's libm (CI `build-wasm`) do not share an
argument-reduction implementation, so those two `sin` results are not
bit-identical. Bound: `max |wasm − native| ≤ 1e-3` (~60 dB below FS). That is
well above a few ULPs and well below the WebAudio peak bound of 2. Every other
fixture stays `memcmp`. Do not copy this bound onto PolyBLEP / wavetable /
noise paths.

### Native/WASM vs WebAudio reference

Compare candidate `x` to reference `ref` (this corpus):

| Check | Bound |
| --- | --- |
| Length | `x.length === ref.length` (44100 frames × 2 channels) |
| Finite | no NaN / Inf in `x` |
| Level | `RMS(x) / RMS(ref) ∈ [0.25, 4]` |
| Envelope | 10-bucket cosine ≥ 0.85 (below) |
| Error energy | `RMS(x − ref) / RMS(ref)` ≤ class bound (below) |
| Peak error | `max \|x − ref\| ≤ 2` |

Length, NaN, level, envelope, and peak are **shared**. Only error-energy
varies by oscillator class. Missing `err_rms_ratio_by_id` in the manifest
falls back to 1.6.

| Class | `err_rms_ratio` | Why |
| --- | --- | --- |
| sine, saw, square, noise, `lp-filter-env`, `lfo-pitch-cutoff`, `delay-reverb-wet`, `acid-303-like` | 1.6 | Named `OscillatorNode` waves vs PolyBLEP still share the same harmonic series; filter/envelope/FX differ in slope and tail, not in how the wave is built. |
| pulse, tri, wavetable, fm | 2.5 | WebAudio uses a **different recipe** than the C++ oscillator, so time-domain residual energy is a larger fraction of the signal. |

**Why 2.5 for that class (not FMA, not a failing run):**

- **Pulse** — `PeriodicWave` from a truncated duty-cycle Fourier series
  (finite harmonics) vs a PolyBLEP pulse at the actual width. The spectrum
  is a harmonic cap, not a naive vs band-limited version of one wave.
- **Wavetable** — `PeriodicWave` from two short Fourier frames vs the
  engine's mipmapped table interpolation. Different tables, different
  interpolation, no shared mip chain.
- **FM** — two sine `OscillatorNode`s into `frequency` vs the engine FM
  operator. Sidebands and phase come from different modulators.
- **Triangle** — `OscillatorNode` `triangle` is a closed-form naive wave;
  PolyBLEP triangle is an integrated square with BLEP correction. Unlike
  saw/square, those are not the same harmonic family (odd-harmonic phase
  and the BLEP residual on two slope breaks).

WASM ↔ native stays `memcmp`. Do not copy the 2.5 class bound onto that
path.

**10-bucket cosine:** split the interleaved stream into 10 equal-length
partitions (last bucket takes the remainder). Compute **RMS of each bucket**
(energy envelope), then cosine similarity of the two 10-D RMS vectors.
Must be ≥ 0.85. Silent/silent (both envelopes all-zero) counts as 1.

### Why these numbers are this loose (not FMA)

WebAudio and the C++ engine are **different topologies**, not two compilations
of one graph:

| WebAudio | C++ |
| --- | --- |
| `OscillatorNode` (naive band-unlimited saw/square/tri) | PolyBLEP VA oscillators |
| Pulse / wavetable via `PeriodicWave` (finite harmonics) | Dedicated pulse + mipmapped wavetable |
| FM as two sine `OscillatorNode`s into `frequency` | Engine FM operator |
| `BiquadFilterNode` | Moog-style ladder / SVF |
| `ConvolverNode` + decaying-noise IR (`Math.random`) | Freeverb-style Schroeder reverb |
| Dual `DelayNode` ping-pong | Engine delay |
| `setTargetAtTime` mix / cutoff ramps | Per-sample param smoothing |
| Render quantum (~128 frames) on `OfflineAudioContext.suspend` | Sample-accurate `ags_event.sample_offset` |
| Noise / S&H / IR use unseeded `Math.random` | Seeded `(patch_id, voice, slot)` RNGs |

Phase, aliasing, filter slope, and wet tails will not line up sample-for-sample.
A time-domain error RMS on the order of the signal, and a 12 dB level window,
are expected. FMA / denormal differences are in the noise floor of `memcmp`
between WASM and native; they are **not** the reason WebAudio needs slack.

## Capture contract

| | |
| --- | --- |
| Context | Chromium `OfflineAudioContext` via Playwright |
| Graph | `VoiceManager` + `createEffectRack` + master `GainNode` → destination |
| Rate / length | 44100 Hz, 44100 frames, stereo, interleaved little-endian f32 |
| MIDI | note 60 velocity 100 at sample 0; note-off at 22050 |
| Extra (sine only) | second note-on 60 vel 100 at sample 10000 (retrigger) |
| Velocity | `ags_event.velocity` is MIDI 0–127; WebAudio `noteOn` gets `vel / 127` |
| Patch ABI | `packPatchParams` from `libs/engine-bridge` (828-byte `PatchStruct`) |
| Events ABI | packed `ags_event` (12 bytes LE: `u32 kind`, `u8 note`, `u8 vel`, `u8 cc`, `u8 pad`, `u32 sample_offset`) |

`createEffectRack` rebuilds the reverb IR on a 50 ms debounce. The generator
waits 100 ms after `setReverb` / `setDelay` **before** `startRendering` so the
impulse is in the graph (including dry patches, whose first `setReverb` still
arms the timer). Chromium `OfflineAudioContext` ignores `setTargetAtTime` at
t=0 before render; the generator snaps those params (LFO depth, FX sends)
during setup only, then restores the real exponential for note envelopes.

## Corpus (12)

Waveforms: `sine`, `tri`, `saw`, `square`, `pulse`, `wavetable`, `fm`, `noise`.
Then `lp-filter-env`, `lfo-pitch-cutoff`, `delay-reverb-wet`, `acid-303-like`.

JSON sources live in `patches/<id>.json`. Packed ABI + PCM in `ref/<id>.{patch.bin,events.bin,f32le}`. Index: `manifest.json`.

## Generate

From the repo root (needs `npm install` so Vite's `esbuild` is on disk):

```bash
node tests/golden/generate.mjs
```

If Playwright or Chromium is missing the script runs `npx playwright install chromium` **once**, then `npm install --no-save playwright`. On failure it still writes JSON patches, `.patch.bin`, `.events.bin`, and `manifest.json` with `pcm_generated: []`. It does not synthesise PCM.

Layout check (no browser):

```bash
node --test tests/golden/layout.test.mjs
```

`compare-wasm.mjs` (WASM vs these refs; `--native-dir` for WASM↔native `memcmp`)
is a separate target.
