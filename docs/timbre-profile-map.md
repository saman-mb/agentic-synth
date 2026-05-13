# Timbre Profile Map

This document maps the practical sound range of the current Agentic Synth audio
engine. It is a companion to [Audio Engine Technical Reference](audio-engine.md):
that page explains how the engine renders audio, while this page explains what
that engine can sound like.

Scope used for this map:

- Engine source: `src/engine/PatchStruct.h`, `VoiceManager.*`,
  `VAOscillator.*`, `WavetableOscillator.*`, `Filter.*`, `ADSREnvelope.*`,
  `LFO.*`, `Delay.*`, and `Reverb.*`.
- UI/modulation source: `ui/src/App.tsx`, `ui/src/data/modulation.ts`,
  `ui/src/components/ModulesGrid.tsx`, and `ui/src/components/MacroBar.tsx`.
- Agent prompt vocabulary: `src/mapper/system-prompt.md`.
- External synthesis references listed in [Grounding Sources](#grounding-sources).

Access note: some selectors are engine/APVTS/agent-capable even when the
current compact modules grid mostly shows numeric knobs. The full sonic range
therefore includes oscillator type, filter type, LFO waveform, LFO target,
delay sync, delay stereo, and voice-count fields from the patch contract, not
only the knobs visible in `ModulesGrid`.

## Core Sonic Identity

Agentic Synth is best understood as a hybrid subtractive synth with three
oscillator slots per voice, a mono-per-voice filter path, two ADSR envelopes,
two per-voice LFOs, stereo delay, and Freeverb-style reverb.

Its strongest territory:

- classic virtual-analog basses, leads, plucks, pads, brass, and strings
- evolving wavetable pads and digital motion patches
- two-operator FM bells, glass, metallic plucks, and FM basses
- noise-based hits, risers, wind, air, and percussion layers
- wide stereo delays, reverbs, moving pans, and ambient tails
- macro-controlled performance patches where a few parameters sweep together

Its weaker or unavailable territory:

- realistic multisampled acoustic instruments
- granular/sample playback textures
- oscillator sync, ring modulation, wavefolding, and hard wavetable effects
- full DX-style multi-operator FM algorithms
- true pulse-width modulation as an audio-rate/LFO target
- per-oscillator filter routing
- formant/vocoder sounds
- full modulation-matrix audio routing for every source listed in the UI

The engine can cover a broad electronic palette, but it is not an all-purpose
sampler or modular synth.

## Audible Timbre Axes

The useful sound space can be mapped across six axes:

| Axis | Low end | Middle | High end |
| --- | --- | --- | --- |
| Spectral density | sine, triangle, filtered tones | square, pulse, wavetable mids | saw, FM depth, noise, high resonance |
| Spectral order | harmonic, stable, pitched | detuned, chorused, animated | inharmonic FM, sample-and-hold, white noise |
| Brightness | closed low-pass, long damping | open low-pass, band-pass | high-pass, open saw, noise, resonant peak |
| Time contour | click, pluck, hit | lead, bass, key | pad, drone, swelling texture |
| Motion | static | LFO vibrato/tremolo/wah | wavetable morph, FM-ratio drift, macro sweeps |
| Space | mono/dry | stereo pan/delay | wide ping-pong and long reverb |

Sound design in this synth is mostly choosing a point on each axis, then using
envelopes, LFOs, macros, and effects to move through that point over time.

## Oscillator Range

Each voice has three oscillator slots. Slots can be layered, detuned, offset by
semitones, panned, and mixed before the mono filter. This means most patches are
made by combining one primary tone source, one support layer, and one texture or
octave layer.

| Oscillator type | Timbre profile | Best uses | Limits |
| --- | --- | --- | --- |
| `Sine` | pure fundamental, no upper harmonics | sub bass, whistles, flute cores, FM support layers, clean bells | sounds thin alone above low-mid range |
| `Triangle` | soft odd-harmonic tone | mellow pads, flute-like leads, soft bass, underlayers | less bite than saw/square |
| `Sawtooth` | bright, full harmonic series | analog bass, brass, strings, supersaw-like detuned stacks, pads | can get buzzy unless filtered |
| `Square` | hollow odd-harmonic tone | reeds, chip leads, organs, basses, nasal pads | less broad than saw |
| `Pulse` | square-like with duty asymmetry | nasal leads, reed/bass color, PWM-ish static tones | current implementation is a simple biased square, not true moving PolyBLEP PWM |
| `Wavetable` | morphs sine -> triangle-like -> saw -> square by default | evolving pads, digital leads, moving drones, shimmer layers | patch state exposes position, not a table ID, so normal patches use the default table |
| `FM` | two-operator sine phase modulation | bells, glass, metallic plucks, electric-piano tines, growling bass | one carrier/modulator pair only; not DX-style algorithmic FM |
| `Noise` | white-noise texture | snares, hats, wind, breath, surf, risers, noisy attacks | needs filter/envelope shaping to become musical |

Layering changes the map:

- Saw + saw with small detune gives wide analog mass.
- Saw + sine down one octave gives bass weight without extra buzz.
- Square/pulse + triangle gives hollow tone with a softer center.
- Wavetable + sine gives evolving pads with a stable fundamental.
- FM + sine or FM + triangle gives bells with controlled pitch center.
- Noise + saw/triangle gives breath, impact, bow, pick, or air.

## Filter And Drive Range

The filter is the main subtractive tone shaper. The engine sums oscillators to
mono per voice, filters that mono signal, then re-splits the result to stereo
using aggregate pan weights.

| Filter mode | Timbre profile | Best uses | Notes |
| --- | --- | --- | --- |
| `LowPass` | warm, dark, rounded, classic subtractive | bass, pads, brass, plucks, acid-like sweeps | uses the Moog ladder path with drive |
| `HighPass` | thin, airy, bright, body removed | glass pads, noise air, risers, thin leads | uses SVF path |
| `BandPass` | focused, nasal, radio-like, resonant | vocal-ish sweeps, percussion, narrow leads | uses SVF path |
| `Notch` | hollow, scooped, moving phase-like color | pads, motion effects, hollow textures | uses SVF path |
| `Peak` | currently same as notch | same as notch | no dedicated peak mode yet |

Filter controls:

- `cutoff_hz` sets the brightness boundary.
- `resonance` emphasizes the cutoff region and can become sine-like or whistly
  near the top of the Moog low-pass path.
- `env_mod` lets the filter envelope open or close the cutoff over the note.
- `drive` adds pre-ladder saturation and growl on the low-pass path only.
- `key_track` is validated and stored, but not currently audible.

The most distinctive subtractive sounds happen when a bright source is made to
move through the filter: saw bass with fast positive filter envelope, pulse lead
with medium resonance, noise hit through band-pass, or a pad with slow low-pass
LFO motion.

## Envelope Range

Every voice has an amplitude ADSR and a filter ADSR.

| Envelope shape | Parameters | Timbre outcome |
| --- | --- | --- |
| Percussive hit | fast attack, short decay, zero sustain | drums, plucks, blips, mallets |
| Bass/lead gate | fast attack, medium sustain, short release | tight playable mono/poly parts |
| Plucked filter | fast amp, filter decay, low filter sustain | classic subtractive pluck, acid snap |
| Slow pad | long attack, high sustain, long release | pads, swells, drones |
| Reverse-like swell | very long attack, low/medium sustain | risers, breathy pads, fade-ins |
| Bell tail | instant attack, zero sustain, long release | FM bells, chimes, glass |

The filter envelope is velocity-scaled in the render path. Harder notes can
sound brighter and more animated when `filter.env_mod` is non-zero. The amp
envelope shapes perceived instrument class: the same oscillator/filter patch can
read as a bass, pluck, pad, or drone only by changing ADSR.

## LFO And Motion Range

The engine has two per-voice LFOs. Supported shapes are sine, triangle,
sawtooth, square, and sample-and-hold. The current patch contract gives each LFO
one target.

| LFO target | Timbre result | Useful ranges |
| --- | --- | --- |
| `Pitch` | vibrato, siren, unstable analog pitch, audio-rate-ish sidebands near top rates | slow 0.1-0.5 Hz drift, 4-8 Hz vibrato, 12-20 Hz rough tone |
| `FilterCutoff` | wah, shimmer, pump, rhythmic brightness | slow pads, tempo pulse, square-gated filter |
| `Amplitude` | tremolo, rhythmic gating, shimmer | sine/triangle for tremolo, square for gate |
| `Pan` | auto-pan, stereo swirl, orbiting layers | slow for pads, faster for movement effects |
| `WavetablePos` | spectral morph, evolving digital motion | very slow for pads, sample-and-hold for grainy digital texture |
| `FmRatio` | moving sideband spacing, gong drift, metallic instability | very slow for bells/metals |
| `None` | no LFO effect | default/off |

BPM sync exists, but the patch only stores on/off. In the current engine,
tempo-synced LFOs use the LFO class default quarter-note cycle rather than a
stored rhythmic division.

Two LFO fields are stored but not currently audible in the main render path:

- `lfo[i].phase_offset`
- `LFO::targetSlot`

## Mod Matrix And Macros

The UI has a modulation matrix with these source names:

- `lfo1`, `lfo2`
- `env1`, `env2`
- `macro1` through `macro4`
- `velocity`
- `keytrack`

Current audible behavior is split:

- Engine-level LFO routing is audible through `PatchStruct::lfo[i].target`.
- Filter envelope routing is audible through `filter.env_mod`.
- MIDI velocity is audible through amplitude and filter-envelope scaling.
- Four macro knobs are audible in the UI path when their matrix connections
  project onto float patch destinations. Macro movement creates an effective
  patch and sends those changed parameters to the engine.
- Non-macro matrix rows for `lfo1`, `lfo2`, `env1`, `env2`, `velocity`, and
  `keytrack` are currently UI/persistence metadata unless another patch route
  also wires that source into the engine.

Macro projection destinations include oscillator level/tuning/pan/pulse/FM
fields, filter cutoff/resonance/env/drive, envelope times/levels, LFO rate/depth,
reverb, delay, master gain, and portamento. This makes macros useful for
performance gestures such as:

- `Brightness`: cutoff up, resonance down slightly, drive up slightly.
- `Bloom`: reverb mix/size up, amp release up, filter cutoff up.
- `Bite`: FM depth up, filter drive up, cutoff down slightly.
- `Motion`: wavetable position, LFO depth/rate, pan depth, delay mix.

## Effects And Space Range

Stereo rendering sends the summed voice output through delay, then reverb, then
an M/S width blend.

| Effect area | Timbre profile | Sound design use |
| --- | --- | --- |
| Per-osc pan | source placement before aggregate stereo split | spread layered oscillators |
| Voice pan offsets | deterministic polyphonic spread | chords feel wider than single notes |
| LFO pan | moving stereo image | auto-pan, shimmer, motion |
| Delay time | slap, echo, rhythmic repeat | short thickening to 2 s echoes |
| Delay feedback | repeat density | dub echoes, rhythmic tails, build-ups |
| Delay stereo | parallel to ping-pong crossfeed | center echo to wide bouncing echo |
| Reverb size | room to hall tail | pads, bells, ambience |
| Reverb damping | darker or brighter decay | warm rooms vs glassy spaces |
| Reverb width | mono collapse to full stereo image | center focus to wide wash |

Reverb is Freeverb-style: comb-filter energy plus allpass diffusion. It is good
for classic synthetic rooms, halls, and wide ambient tails, not convolution-like
real spaces.

## Main Sound Families

| Family | Patch center | Timbre map location |
| --- | --- | --- |
| Pure sub | sine or triangle, low-pass open or lightly closed, no reverb | low density, harmonic, stable, dry |
| Rounded analog bass | saw/pulse + sine sub, low-pass, fast amp, filter env, drive | dense but controlled, punchy, warm |
| Acid/resonant bass | saw/pulse, low-pass resonance high, positive env_mod, drive | bright cutoff focus, squelch, movement |
| Hollow reed/organ | square/pulse layers, modest filter, little attack | odd-harmonic, nasal, stable |
| Chip lead | square/pulse, fast envelope, little reverb, optional pitch LFO | hard edges, narrow time contour |
| Brass stab | saw stack, fast-to-medium attack, filter env, moderate resonance | bright attack, rounded sustain |
| String/pad analog | detuned saw/triangle layers, slow amp, low-pass motion, reverb | dense, smooth, wide, sustained |
| Evolving wavetable pad | wavetable layers, LFO to wavetable pos, slow amp, reverb | moving spectrum, digital, wide |
| Crystal/glass pad | wavetable/FM + sine, high-pass, long reverb | bright, airy, inharmonic edge |
| FM bell | FM ratio 2-8 or non-integer, depth 0.2-0.7, zero sustain, long release | inharmonic, percussive, reverberant |
| FM electric key | FM with sine support, medium decay/sustain, low-pass | metallic transient, pitched body |
| Growl/FM bass | FM ratio 0.5-2, medium depth, low-pass drive | gritty, moving harmonics, low body |
| Noise percussion | noise, high/band-pass or low-pass, fast amp/filter env | broadband, short, textural |
| Wind/surf/air | noise, high-pass/low-pass sweep, slow LFO, reverb | stochastic, broad, evolving |
| Rhythmic gate | amplitude/filter LFO square or saw, delay sync | cyclic, tempo-driven |
| Drone | sustained saw/wavetable/noise mix, slow LFOs, long reverb | static pitch, high motion, wide |

## Practical Descriptor Map

Use these mappings when asking the agent or designing presets.

| Desired descriptor | Most direct engine route |
| --- | --- |
| Warm | low-pass cutoff down, triangle/saw, low resonance, little high-pass |
| Bright | saw/wavetable high position, open cutoff, low damping, high-pass layers |
| Soft | triangle/sine, slow attack, low drive, low resonance |
| Buzzy | saw, open cutoff, detune, moderate drive |
| Hollow | square/pulse, notch/band-pass, moderate resonance |
| Nasal | pulse, band-pass, positive resonance, mid cutoff |
| Metallic | FM ratio above 2 or non-integer, medium/high FM depth |
| Glassy | FM or wavetable plus high-pass, long reverb, low damping |
| Dark | low cutoff, reverb damping up, slow envelope |
| Airy | noise or high-pass wavetable, reverb mix, low body |
| Gritty | low-pass drive, FM depth, low ratio FM, sample-and-hold motion |
| Evolving | LFO to wavetable position, filter cutoff, FM ratio, or macros |
| Wide | oscillator pan, LFO pan, delay stereo, reverb width |
| Tight | short release, low reverb/delay mix, low voice count |
| Huge | detuned layers, long release, delay feedback, high reverb size/mix |

## Boundaries To State Honestly

Capable, with caveats:

- Supersaw-like sounds are possible with three detuned saw slots and voice
  spread, but there is no dedicated unison stack per oscillator.
- PWM-style static pulse sounds are possible, but the engine does not expose
  pulse width as an LFO target.
- FM sounds are real and useful, but limited to simple two-operator sine
  phase modulation per oscillator slot.
- Wavetable motion is useful, but normal patches use the default four-frame
  table unless another subsystem loads a table into oscillator instances.
- Macro modulation can sound deep because it rewrites effective patch values,
  but this is not the same as a fully audio-rate modulation matrix.

Not currently in range:

- realistic piano/guitar/drum sample playback
- granular clouds
- wavetable import selected by patch metadata
- oscillator sync
- ring modulation
- wavefolding
- formant filters
- vocoding
- MPE/poly-aftertouch routing
- audible filter key tracking
- per-oscillator filters
- dedicated chorus/flanger/phaser

## Grounding Sources

- Apple Logic Pro, "How Subtractive Synthesizers Work": subtractive signal flow,
  common waveform harmonic profiles, filters, ADSR, LFOs, glide, and polyphony.
  https://help.apple.com/logicpro/mac/9.1.6/en/logicpro/instruments/chapter_A_section_3.html
- CMU, "FM Synthesis": FM sidebands, modulation index, and how increasing
  index increases spectral bandwidth.
  https://www.cs.cmu.edu/~music/icm-online/readings/fm-synthesis/index.html
- Ableton Live manual, "Wavetable Synthesis": wavetable position movement as
  shifting timbre over time.
  https://www.ableton.com/en/manual/live-instrument-reference/#wavetable-synthesis
- Csound manual, `freeverb`: Freeverb as stereo comb-plus-allpass reverb with
  room-size and high-frequency damping controls.
  https://csound.com/docs/manual/freeverb.html
- Julius O. Smith, "Freeverb", Physical Audio Signal Processing: Freeverb
  structure, stereo spread, lowpass-feedback comb filters, and room/damping
  interpretation.
  https://www.dsprelated.com/freebooks/pasp/Freeverb.html
