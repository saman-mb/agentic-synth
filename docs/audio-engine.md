# Audio Engine Technical Reference

This document describes the current audio engine implementation in `src/engine`
and its plugin-facing control path in `src/plugin`. It is source-grounded: where
older architecture docs describe the original placeholder target shape, this
page describes the code that currently renders audio.

For a sound-design view of what this engine can produce, see
[Timbre Profile Map](timbre-profile-map.md).

## Short Answer

Agentic Synth is a hybrid polyphonic synthesizer. Its primary shape is virtual
analog and subtractive synthesis, with wavetable and simple two-operator FM
oscillator modes available per oscillator slot.

It is not a dedicated FM synth. FM is one selectable oscillator type inside a
larger engine that also includes VA oscillators, wavetable morphing, white noise,
ADSR envelopes, LFO routing, filters, stereo delay, and reverb.

The default patch is one enabled sawtooth oscillator into a wide-open low-pass
filter with a fast amplitude envelope.

## Source Map

| Area | Main files | Role |
| --- | --- | --- |
| Patch contract | `src/engine/PatchStruct.h` | Fixed-size patch data shared by the agent, UI, plugin state, and audio engine. |
| Plugin control path | `src/plugin/AgenticSynthPlugin.*` | JUCE processor lifecycle, APVTS parameters, MIDI adaptation, and audio callback entrypoint. |
| Voice engine | `src/engine/VoiceManager.*` | Polyphonic allocation, patch application, per-sample voice rendering, effects bus. |
| VA oscillators | `src/engine/VAOscillator.*` | PolyBLEP-style saw/square, integrated triangle, analog-style drift. |
| Wavetable oscillator | `src/engine/WavetableOscillator.*` | Multi-frame wavetable morphing with FFT-built mip levels. |
| Envelopes | `src/engine/ADSREnvelope.*` | Exponential ADSR for amplitude and filter modulation. |
| LFOs | `src/engine/LFO.*` | Per-voice modulation sources with multiple waveforms and BPM-sync support. |
| Filters | `src/engine/Filter.*` | Moog-style ladder low-pass plus state-variable HP/BP/notch modes. |
| Effects | `src/engine/Delay.*`, `src/engine/Reverb.*` | Stereo delay/ping-pong topology and Freeverb-style Schroeder reverb. |
| MIDI | `src/engine/MidiHandler.*` | JUCE-free raw MIDI message handling and standard CC mapping. |
| Safety and validation | `src/engine/PatchValidator.*`, `src/engine/ParamSmoother.*`, `src/engine/SPSCQueue.h` | Parameter clamping, finite checks, smoothing, lock-free patch queue primitives. |
| Patch utilities | `src/engine/MorphEngine.*`, `VariationEngine.*`, `StyleTransfer.*`, `PresetExporter.*`, `MultiModalInput.*` | Patch generation, interpolation, style transfer, export, and analysis helpers. |

The CMake target for the DSP side is `agentic_synth_engine_core`. It is kept
JUCE-light: the audio engine itself is ordinary C++ and is not built around
JUCE audio classes.

## Runtime Control Flow

The plugin state path is:

```text
UI, host automation, or agent patch
  -> APVTS parameters
  -> AgenticSynthPlugin::buildPatchFromApvts()
  -> PatchStruct
  -> VoiceManager::applyPatch()
  -> VoiceManager::renderBlock()
  -> DAW or standalone audio output
```

`AgenticSynthPlugin::processBlock()` performs the audio callback work:

1. Read host tempo from the JUCE playhead when available.
2. Build a `PatchStruct` from the current APVTS parameter values.
3. Apply the patch to `VoiceManager`.
4. Drain queued audition-keyboard MIDI.
5. Adapt host `juce::MidiMessage` events to `RawMidiMsg` and pass them to
   `MidiHandler`.
6. Clear the JUCE audio buffer.
7. Render mono or stereo audio through `VoiceManager`.

Agent patches are not applied directly to the DSP graph from the message
thread. They are written into APVTS first; the audio thread then reads APVTS at
the top of the next block. This makes APVTS the single source of truth for
plugin state, automation, recall, UI edits, and agent edits.

## Patch Contract

`PatchStruct` is the engine's complete patch snapshot. It is a POD-style,
trivially copyable structure made of fixed-size sub-structures:

- `OscParams osc[3]`
- `FilterParams filter`
- `EnvParams filter_env`
- `EnvParams amp_env`
- `LfoParams lfo[2]`
- `ReverbParams reverb`
- `DelayParams delay`
- global `master_gain`, `portamento_s`, and `voice_count`

The important constants are:

```cpp
static constexpr int kMaxOscillators = 3;
static constexpr int kMaxLfos = 2;
```

Agent-side generation paths validate patch fields with `validate_patch()`
before pushing generated patches onward. Validation clamps non-finite values,
oscillator ranges, filter cutoff/resonance, envelope times, LFO rate/depth,
effect ranges, master gain, portamento, and voice count. The APVTS path also
uses parameter ranges and `VoiceManager::applyPatch()` boundary clamps before
values affect DSP state.

The default patch is created by `make_default_patch()`:

- Oscillator 0 enabled
- Oscillator 0 type: `Sawtooth`
- Oscillator 0 volume: `1.0`
- Filter type: `LowPass`
- Filter cutoff: `18000 Hz`
- Filter resonance: `0`
- Amp envelope: `0.005 s` attack, `0.1 s` decay, `1.0` sustain, `0.1 s` release
- Filter envelope: fast shape, no modulation by default
- LFOs off
- Delay and reverb mostly dry
- Reverb width full stereo
- Master gain `1.0`
- Voice count `8`

## Audio Graph

The stereo render path in `VoiceManager::renderBlock(left, right, numSamples)`
is:

```text
MIDI note state
  -> voice allocator
  -> per-voice oscillator slots
  -> mono oscillator sum
  -> per-voice filter
  -> stereo split from aggregate pan weights
  -> amp envelope, velocity, amplitude LFO
  -> per-voice DC blocker
  -> sum all voices
  -> master gain
  -> stereo delay
  -> stereo reverb
  -> post-reverb M/S width blend
  -> output
```

Each sample advances the global smoothers for cutoff, resonance, master gain,
and reverb width. Each active voice then renders one stereo sample.

The mono `renderBlock(float* output, int numSamples)` path renders the dry voice
sum multiplied by master gain. The stereo effects bus is only used by the stereo
render overload.

## Voice Architecture

`VoiceManager` owns a fixed vector of `Voice` instances. The default maximum is
16 voices, and `PatchStruct::voice_count` acts as an active polyphony cap.

Each `Voice` contains:

- MIDI bookkeeping: note number, held/releasing state, velocity, note-on order
- Frequency state: target frequency and smoothed current frequency
- 3 oscillator slots
- amp and filter ADSR envelopes
- 2 LFO instances
- a Moog ladder filter and an SVF filter, both pre-allocated
- an active filter pointer and optional old-filter pointer for filter-type
  crossfade
- per-voice DC blockers
- per-voice filter drive smoother
- voice-steal fade state
- a deterministic voice-level pan offset

Voice allocation behavior:

- `noteOn()` first reuses an active voice already playing the same MIDI note.
- If no matching voice exists, it uses the first free voice within
  `voice_count`.
- If no free voice exists, it steals the oldest releasing voice first, then the
  oldest active voice.
- Stolen voices fade out for about 5 ms to avoid clicks.
- Reducing `voice_count` while notes are active fades out voices above the new
  cap instead of hard-stopping them.

Portamento is implemented as a one-pole glide:

```text
currentFrequency = alpha * currentFrequency + (1 - alpha) * targetFrequency
```

`alpha` is computed from `portamento_s` and sample rate when the portamento
setting or sample rate changes.

## Oscillator System

Each voice has 3 independent oscillator slots. Each slot can be enabled or
disabled and has its own:

- type
- semitone offset
- detune in cents
- wavetable position
- FM ratio
- FM depth
- volume
- pan
- pulse width

The oscillator frequency is:

```text
voiceFrequency
  * pitch-LFO multiplier
  * semitone/detune multiplier
```

### Sine

`OscType::Sine` uses the `WavetableOscillator` with the default table at morph
position `0.0`. The default table's first frame is a pure sine.

### Virtual Analog Saw, Square, Triangle

`OscType::Sawtooth`, `Square`, and `Triangle` use `VAOscillator`.

The VA oscillator implements:

- saw: naive ramp corrected by a PolyBLEP residual at the wrap discontinuity
- square: PolyBLEP correction at both phase discontinuities
- triangle: leaky integration of the square wave
- slow oscillator drift: random target drift in cents, smoothed over time

This is the main virtual-analog path.

### Pulse

`OscType::Pulse` uses the square oscillator path, then biases the sample by
`(pulse_width - 0.5) * 2.0` and clamps to `[-1, 1]`.

This is a simple duty-asymmetry approximation. It is not a full PolyBLEP PWM
implementation with a movable second discontinuity.

### Wavetable

`OscType::Wavetable` uses `WavetableOscillator`.

The default wavetable has 4 frames:

1. sine
2. triangle-like harmonic series
3. sawtooth harmonic series
4. square harmonic series

The wavetable implementation supports:

- `kWavetableSize = 256` samples per frame
- up to `kMaxWavetableFrames = 256`
- 7 mip levels
- FFT-based offline band-limiting for mip construction
- interpolation between samples
- interpolation between frames
- crossfading between safe mip levels
- direct frame loading and WAV loading APIs

Current patch state exposes `wavetable_pos`, but not a wavetable-table ID. In
normal patch flow, oscillators use the default table unless another subsystem
explicitly loads a table into the oscillator instance.

### FM

`OscType::FM` implements a simple two-operator sine FM-style oscillator:

```text
modulator frequency = carrier frequency * fm_ratio
modulator sample = sin(modulator phase)
carrier phase = carrier phase + modulator sample * fm_depth
output = sin(carrier phase)
```

Implementation details:

- The carrier and modulator have separate phase accumulators.
- The carrier runs at the note frequency.
- The modulator runs at `note frequency * fm_ratio`.
- `fm_depth` controls phase deviation.
- The internal ratio clamp is wider than the APVTS range: the engine clamps to
  roughly `0.05..32`, while the UI/plugin parameter range is `0.5..16`.
- LFOs can modulate `FmRatio`.

Technically this is phase modulation in code, exposed and used as an FM
oscillator mode. It is close enough for the intended two-operator FM sound
design surface, but it is not a DX-style multi-operator FM architecture.

### Noise

`OscType::Noise` uses a small xorshift white-noise generator per oscillator
slot. Each slot is seeded differently so multiple noise voices do not produce
identical correlated noise.

## Mixing and Pan

The current filter path is mono per voice. Enabled oscillator slots are summed
to a mono signal before filtering.

To preserve per-oscillator pan intent, the voice computes aggregate left/right
pan weights before filtering:

1. For each enabled oscillator, combine the oscillator pan with the voice's
   round-robin pan offset.
2. Add LFO pan modulation if routed.
3. Convert the effective pan to constant-power left/right gains.
4. Weight the gains by oscillator volume.
5. Normalize by total enabled oscillator volume.
6. After mono filtering, split the filtered mono sample back to stereo using
   those aggregate weights.

This gives useful stereo placement without running a separate stereo filter per
voice or per oscillator.

## Filter System

The patch exposes these filter types:

- `LowPass`
- `HighPass`
- `BandPass`
- `Notch`
- `Peak`

Current dispatch:

- `LowPass` uses `MoogLadder`
- `HighPass`, `BandPass`, `Notch`, and `Peak` use `SVFilter`
- `Peak` currently maps to the SVF notch output because there is no dedicated
  peak filter implementation yet

### Moog Ladder Low-pass

`MoogLadder` is the main low-pass filter. It uses:

- 4 cascaded one-pole stages
- a zero-delay-feedback-style coefficient formulation
- a linear prediction of the fourth stage output
- `tanh()` saturation in the feedback path
- pre-filter drive
- output compensation based on drive gain
- resonance that can reach self-oscillation behavior near maximum values

Drive is meaningful on the Moog ladder path. For SVF modes, `setDrive()` is a
no-op through the base `Filter` interface.

### State-variable Filter

`SVFilter` is a ZDF state-variable filter with low-pass, high-pass, band-pass,
and notch outputs. Resonance maps to the damping term. The implementation caps
damping to avoid unstable self-oscillation in SVF modes.

### Filter Type Changes

Every voice pre-allocates both filter implementations. A filter type change
therefore swaps a pointer instead of allocating.

To avoid clicks, type changes crossfade for about 5 ms:

- the outgoing filter and incoming filter both process the same mono input
- their outputs are linearly blended
- the outgoing filter is reset when the fade completes

Rapid back-to-back filter changes snap any existing crossfade before starting a
new one.

## Envelopes

The engine uses two ADSR envelopes per voice:

- amp envelope
- filter envelope

`ADSREnvelope` uses exponential segments implemented as a leaky-integrator
recurrence. Attack, decay, and release coefficients are calibrated so the
segment reaches the intended threshold after the configured number of samples.

Release is recalibrated from the current output level on `noteOff()`. This
prevents releases during attack or decay from ending too early.

Amp envelope output controls voice gain. Filter envelope output modulates
filter cutoff through `filter.env_mod`, scaled by note velocity.

Envelope parameter changes are smoothed at the `VoiceManager` level before
being pushed into each envelope. This reduces clicks when patch or automation
updates change envelope timings during held notes.

## LFO System

Each voice has 2 LFOs. Patch fields expose:

- waveform
- target
- free rate in Hz
- depth
- phase offset
- BPM sync flag

Supported LFO waveforms:

- sine
- triangle
- saw
- square
- sample and hold

Supported current routing targets:

- none
- pitch
- filter cutoff
- amplitude
- pan
- wavetable position
- FM ratio

Routing behavior:

- Pitch modulation is summed in semitones and converted to a frequency
  multiplier.
- Filter cutoff modulation is multiplicative around the patch cutoff.
- Amplitude modulation is applied around unity and clamped so it cannot invert
  phase.
- Pan modulation is added to every enabled oscillator's effective pan.
- Wavetable-position modulation is added to every enabled wavetable oscillator.
- FM-ratio modulation scales every enabled FM oscillator's ratio.

The LFO's internal depth is set to `1.0`; patch depth is applied at the routing
stage. LFO rate and depth are smoothed before being pushed to voices.

BPM sync currently uses the LFO class's default quarter-note cycle because
`PatchStruct` exposes only `bpm_sync`, not a rhythmic division field.

Two fields are persisted but not currently audible in the main render path:

- `LfoParams::phase_offset`
- `LFO::targetSlot`

## Effects Bus

Stereo rendering uses one shared effects bus after voice summing:

```text
voice sum
  -> master gain
  -> Delay
  -> Reverb
  -> M/S width
```

### Delay

`Delay` is a stereo fractional delay line:

- time range: `0.001..2.0 s`
- feedback range: `0..0.99`
- wet/dry mix: `0..1`
- stereo topology: `0` is parallel L/R delay, `1` is full ping-pong crossfeed
- linear interpolation for fractional read positions
- ring buffers allocated in `prepare()`, no allocation in `process()`

When `delay.bpm_sync` is true, `delay.time_s` is reinterpreted as beats:

- `1.0` = quarter note
- `0.5` = eighth note
- `0.25` = sixteenth note
- `2.0` = half note
- `4.0` = whole note

The conversion uses the current host BPM when available, otherwise 120 BPM.

### Reverb

`Reverb` is a Freeverb-style stereo Schroeder reverb:

- 4 comb filters per channel
- 2 allpass diffusers per channel
- channel-specific delay lengths using stereo spread
- one-pole damping in each comb feedback path
- `size` maps to comb feedback
- `damping` maps to feedback-path low-pass amount
- `mix` blends dry and wet internally

After reverb, `VoiceManager` applies an M/S width blend:

```text
mid = (L + R) * 0.5
side = (L - R) * 0.5
L = mid + side * width
R = mid - side * width
```

`width = 0` collapses the post-reverb signal to mono. `width = 1` leaves the
stereo image unchanged.

## MIDI Handling

`MidiHandler` is JUCE-free. The plugin adapts JUCE MIDI messages into
`RawMidiMsg`, then passes them to the handler.

Supported MIDI events:

- note on
- note off
- velocity-0 note-on treated as note off
- CC messages

Mapped CCs:

| CC | Meaning | Current behavior |
| --- | --- | --- |
| 1 | Mod wheel | Raises cutoff by up to 3x the cached CC cutoff. |
| 7 | Volume | Stored for inspection, not currently applied to master gain. |
| 64 | Sustain pedal | Pedal-off releases all held notes. |
| 71 | Resonance/timbre | Sets filter resonance. |
| 72 | Amp release | Log-scaled release time. |
| 73 | Amp attack | Log-scaled attack time. |
| 74 | Brightness/cutoff | Log-scaled cutoff from 20 Hz to about 18 kHz. |
| 75 | Amp decay | Log-scaled decay time. |
| 120 | All sound off | Calls `allNotesOff()`. |
| 123 | All notes off | Calls `allNotesOff()`. |

Host tempo is forwarded to `VoiceManager` and then into every voice LFO.

## Parameter Smoothing

The engine avoids zipper noise with `ParamSmoother`, a one-pole low-pass:

```text
state += coeff * (target - state)
```

Smoothers are used for:

- filter cutoff
- filter resonance
- master gain
- reverb width
- amp envelope target values
- filter envelope target values
- LFO rate
- LFO depth
- per-voice filter drive

First patch application after `prepare()` snaps smoothers to target values so
state recall or patch load does not audibly glide up from defaults.

## Real-time Safety Model

The active audio callback path is designed around these rules:

- no LLM work on the audio thread
- no network work on the audio thread
- no Python calls on the audio thread
- no unbounded allocation in per-sample DSP
- patch updates are copied into fixed-size structures
- oscillators, filters, envelopes, delay lines, and reverb buffers are prepared
  before rendering
- filter implementations are pre-allocated per voice
- delay and reverb allocate their buffers in `prepare()`
- effects and voice state are reset on lifecycle changes

`RealtimeSafety.h` contains debug helpers for asserting realtime context and a
lock-free ring buffer template. The concrete patch state path also uses
`SPSCQueue` elsewhere in the agent bridge, but the plugin's current single
source of truth is APVTS.

## Patch Utilities

The engine library includes helper systems that operate on `PatchStruct` but do
not directly render audio:

- `VariationEngine`: creates deterministic variations by sweeping, perturbing,
  and morphing patch fields.
- `MorphEngine`: stores multiple patch targets and interpolates between them.
  Frequency-like and time-like fields use log-domain interpolation.
- `StyleTransfer`: extracts high-level style descriptors from one patch and
  applies them to another.
- `MultiModalInput`: maps spectral or tempo information into patch fields.
- `PresetExporter`: exports approximate Serum/Vital-oriented chunks and reports
  unmapped parameters.

These systems should be treated as patch-generation or patch-transformation
tools, not part of the per-sample synthesis graph.

## Current Audible Feature Matrix

| Patch field | Audible today | Notes |
| --- | --- | --- |
| `osc[i].enabled` | Yes | Gates each oscillator slot. |
| `osc[i].type` | Yes | Selects sine, VA, pulse, wavetable, FM, or noise path. |
| `osc[i].semitone_offset` | Yes | Per-oscillator pitch offset. |
| `osc[i].detune_cents` | Yes | Per-oscillator fine detune. |
| `osc[i].wavetable_pos` | Yes | Used by wavetable oscillator and LFO target. |
| `osc[i].fm_ratio` | Yes | Used by FM oscillator and LFO target. |
| `osc[i].fm_depth` | Yes | FM phase deviation depth. |
| `osc[i].volume` | Yes | Scales oscillator contribution and pan weighting. |
| `osc[i].pan` | Yes | Used in aggregate post-filter stereo split. |
| `osc[i].pulse_width` | Yes | Simple pulse bias approximation. |
| `filter.type` | Yes | Low-pass/Moog or SVF mode; peak maps to notch. |
| `filter.cutoff_hz` | Yes | Smoothed and modulated by filter env/LFO. |
| `filter.resonance` | Yes | Smoothed. |
| `filter.env_mod` | Yes | Filter envelope depth. |
| `filter.key_track` | No | Persisted and validated, but not consumed in render. |
| `filter.drive` | Partly | Audible on Moog low-pass; no-op for SVF modes. |
| `amp_env` | Yes | Per-voice amplitude envelope. |
| `filter_env` | Yes | Drives cutoff through `filter.env_mod`. |
| `lfo[i].waveform` | Yes | Shape selected per LFO. |
| `lfo[i].target` | Yes | Routes to supported targets. |
| `lfo[i].rate_hz` | Yes | Used when `bpm_sync` is false. |
| `lfo[i].depth` | Yes | Routing-stage modulation depth. |
| `lfo[i].phase_offset` | No | Persisted but not applied to LFO phase. |
| `lfo[i].bpm_sync` | Yes | Uses quarter-note sync behavior. |
| `reverb.size` | Yes | Comb feedback. |
| `reverb.damping` | Yes | Comb feedback damping. |
| `reverb.width` | Yes | Post-reverb M/S width. |
| `reverb.mix` | Yes | Reverb wet/dry. |
| `delay.time_s` | Yes | Seconds or beats depending on sync flag. |
| `delay.feedback` | Yes | Clamped below unity. |
| `delay.mix` | Yes | Delay wet/dry. |
| `delay.stereo` | Yes | Parallel to ping-pong topology crossfade. |
| `delay.bpm_sync` | Yes | Host BPM or 120 BPM fallback. |
| `master_gain` | Yes | Smoothed post-voice gain before FX. |
| `portamento_s` | Yes | One-pole pitch glide. |
| `voice_count` | Yes | Active polyphony cap. |

## Known Technical Limits

The current implementation is functional, but these details matter when
extending it:

- The filter is mono per voice. Per-oscillator pan is approximated by splitting
  the filtered mono signal after the filter.
- `Peak` filter type is not a true peak filter yet; it maps to notch.
- `filter.key_track` is present in patch state and UI parameters but is not
  applied in the render path.
- `lfo.phase_offset` is persisted but not applied to `LFO::mPhase`.
- LFO BPM sync has no patch-level division selector yet.
- Pulse width is a simple square-wave bias approximation, not full PWM.
- Wavetable table selection is not represented in `PatchStruct`.
- CC7 volume is cached but not applied to output gain.
- `src/engine/DCBlocker.h` contains an older `DCBlocker` class; the active voice
  path uses `DcBlocker` from `PatchValidator.h`.
- `docs/architecture.md` still contains older placeholder-era descriptions and
  should be refreshed separately if it is meant to be canonical again.

## Test Coverage Pointers

Relevant tests live under `tests/`:

- `VoiceManagerTest.cpp`: voice rendering, envelopes, filter/effects behavior.
- `Phase3FollowupTest.cpp`: oscillator routing, FM behavior, LFO targets,
  filter type behavior, stereo/effects follow-ups.
- `WavetableOscillatorTest.cpp`: mip selection, morphing, and wavetable
  behavior.
- `VAOscillatorTest.cpp`: VA oscillator behavior.
- `ADSREnvelopeTest.cpp`: envelope segment behavior.
- `LFOTest.cpp`: LFO waveforms and sync behavior.
- `FilterTest.cpp`: filter behavior.
- `DelayTest.cpp` and `ReverbTest.cpp`: effects behavior.
- `PatchValidatorTest.cpp`: patch clamping, finite checks, and active
  `DcBlocker` behavior.
- `PluginLifecycleTest.cpp`: APVTS state recall, lifecycle reset, and patch
  roundtrip behavior.

When changing DSP behavior, prefer tests that render deterministic buffers and
assert clear properties: non-silence, relative energy, dominant frequency,
finite output, state reset, or bounded discontinuity.
