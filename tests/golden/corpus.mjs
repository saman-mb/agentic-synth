// ── Golden corpus (#307) ─────────────────────────────────────────────
//
// Representative PatchParams + MIDI events for WebAudio reference
// renders. Numbers match PatchStruct.h enums (OscType, FilterType,
// LfoTarget). packPatchParams in engine-bridge is the only ABI writer.

export const SAMPLE_RATE = 44100;
export const FRAMES = 44100;
export const CHANNELS = 2;
export const PCM_BYTES = FRAMES * CHANNELS * 4;
export const PATCH_STRUCT_SIZE = 828;
export const EVENT_SIZE = 12;

export const AGS_EVENT_NOTE_ON = 1;
export const AGS_EVENT_NOTE_OFF = 2;

// WebAudio vs C++ is a topology gap, not FMA. 1.6 covers named
// OscillatorNode waves (sine/saw/square) plus filter/envelope/FX.
// 2.5 is the a-priori class for PeriodicWave pulse/wavetable, FM
// operators, and OscillatorNode triangle vs PolyBLEP — the harmonic
// recipe differs, not just aliasing. Do not retune after seeing diffs.
export const ERR_RMS_RATIO_DEFAULT = 1.6;
export const ERR_RMS_RATIO_PERIODICWAVE = 2.5;
export const ERR_RMS_RATIO_BY_ID = {
  pulse: ERR_RMS_RATIO_PERIODICWAVE,
  tri: ERR_RMS_RATIO_PERIODICWAVE,
  wavetable: ERR_RMS_RATIO_PERIODICWAVE,
  fm: ERR_RMS_RATIO_PERIODICWAVE,
};

export const TOLERANCES = {
  wasm_native: "memcmp",
  // Fixture `fm` only: glibc vs emscripten libm on per-sample std::sin.
  wasm_native_fm_peak_abs: 1e-3,
  webaudio: {
    same_length: true,
    no_nan_inf: true,
    rms_ratio: [0.25, 4],
    bucket_cosine: { buckets: 10, min: 0.85 },
    err_rms_ratio: ERR_RMS_RATIO_DEFAULT,
    err_rms_ratio_by_id: ERR_RMS_RATIO_BY_ID,
    peak_abs_err: 2,
  },
};

function osc(overrides = {}) {
  return {
    type: 0,
    volume: 1,
    detune_cents: 0,
    semitone_offset: 0,
    wavetable_pos: 0,
    fm_ratio: 1,
    fm_depth: 0,
    pulse_width: 0.5,
    pan: 0,
    enabled: 1,
    ...overrides,
  };
}

function silentOsc() {
  return osc({ type: 2, volume: 0, enabled: 0 });
}

function openFilter() {
  return { type: 0, cutoff_hz: 18000, resonance: 0, env_mod: 0, key_track: 0, drive: 0 };
}

function idleLfos() {
  return [
    { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
    { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
  ];
}

function dryFx() {
  return {
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0, stereo: 0.5, bpm_sync: 0 },
  };
}

function ampHeld() {
  return { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 };
}

function filterEnvIdle() {
  return { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 };
}

function basePatch(osc0, extra = {}) {
  const fx = dryFx();
  return {
    osc: [osc0, silentOsc(), silentOsc()],
    filter: openFilter(),
    filter_env: filterEnvIdle(),
    amp_env: ampHeld(),
    lfo: idleLfos(),
    reverb: fx.reverb,
    delay: fx.delay,
    master_gain: 1,
    portamento_s: 0,
    voice_count: 8,
    ...extra,
  };
}

function noteOn(note, velocity, sample_offset) {
  return { kind: AGS_EVENT_NOTE_ON, note, velocity, cc: 0, sample_offset };
}

function noteOff(note, sample_offset) {
  return { kind: AGS_EVENT_NOTE_OFF, note, velocity: 0, cc: 0, sample_offset };
}

export const DEFAULT_EVENTS = [noteOn(60, 100, 0), noteOff(60, 22050)];

// Sine-only extra: retrigger C4 at sample 10000 while the key is still
// held, then the shared note-off at 22050. Tests envelope re-strike.
export const SINE_EVENTS = [noteOn(60, 100, 0), noteOn(60, 100, 10000), noteOff(60, 22050)];

export const CORPUS = [
  { id: "sine", title: "Sine oscillator", patch: basePatch(osc({ type: 0 })), events: SINE_EVENTS },
  { id: "tri", title: "Triangle oscillator", patch: basePatch(osc({ type: 1 })), events: DEFAULT_EVENTS },
  { id: "saw", title: "Sawtooth oscillator", patch: basePatch(osc({ type: 2 })), events: DEFAULT_EVENTS },
  { id: "square", title: "Square oscillator", patch: basePatch(osc({ type: 3 })), events: DEFAULT_EVENTS },
  { id: "pulse", title: "Pulse oscillator", patch: basePatch(osc({ type: 4, pulse_width: 0.25 })), events: DEFAULT_EVENTS },
  {
    id: "wavetable",
    title: "Wavetable oscillator",
    patch: basePatch(osc({ type: 5, wavetable_pos: 0.65 })),
    events: DEFAULT_EVENTS,
  },
  {
    id: "fm",
    title: "Two-operator FM",
    patch: basePatch(osc({ type: 6, fm_ratio: 2, fm_depth: 0.45 })),
    events: DEFAULT_EVENTS,
  },
  { id: "noise", title: "Noise oscillator", patch: basePatch(osc({ type: 7 })), events: DEFAULT_EVENTS },
  {
    id: "lp-filter-env",
    title: "Low-pass + filter envelope",
    patch: basePatch(osc({ type: 2 }), {
      filter: { type: 0, cutoff_hz: 420, resonance: 0.55, env_mod: 0.85, key_track: 0.2, drive: 0.15 },
      filter_env: { attack_s: 0.01, decay_s: 0.28, sustain: 0.12, release_s: 0.12 },
    }),
    events: DEFAULT_EVENTS,
  },
  {
    id: "lfo-pitch-cutoff",
    title: "LFO → pitch and cutoff",
    patch: basePatch(osc({ type: 2 }), {
      filter: { type: 0, cutoff_hz: 1600, resonance: 0.35, env_mod: 0, key_track: 0, drive: 0 },
      lfo: [
        { waveform: 0, target: 1, rate_hz: 5, depth: 0.4, phase_offset: 0, bpm_sync: 0 },
        { waveform: 1, target: 2, rate_hz: 2.2, depth: 0.55, phase_offset: 0, bpm_sync: 0 },
      ],
    }),
    events: DEFAULT_EVENTS,
  },
  {
    id: "delay-reverb-wet",
    title: "Delay + reverb wet",
    patch: basePatch(osc({ type: 2 }), {
      reverb: { size: 0.7, damping: 0.4, width: 1, mix: 0.55 },
      delay: { time_s: 0.22, feedback: 0.45, mix: 0.4, stereo: 0.7, bpm_sync: 0 },
    }),
    events: DEFAULT_EVENTS,
  },
  {
    id: "acid-303-like",
    title: "Acid 303-like",
    patch: {
      osc: [
        osc({ type: 2, volume: 0.95 }),
        osc({ type: 2, volume: 0.35, semitone_offset: -12 }),
        osc({ type: 0, volume: 0.3, semitone_offset: -12 }),
      ],
      filter: { type: 0, cutoff_hz: 400, resonance: 0.85, env_mod: 0.85, key_track: 0.4, drive: 0.5 },
      filter_env: { attack_s: 0.001, decay_s: 0.15, sustain: 0, release_s: 0.08 },
      amp_env: { attack_s: 0.001, decay_s: 0.1, sustain: 0.6, release_s: 0.1 },
      lfo: idleLfos(),
      reverb: { size: 0.2, damping: 0.5, width: 0.5, mix: 0.08 },
      delay: { time_s: 0.25, feedback: 0.2, mix: 0.1, stereo: 0.3, bpm_sync: 0 },
      master_gain: 0.9,
      portamento_s: 0.08,
      voice_count: 1,
    },
    events: DEFAULT_EVENTS,
  },
];
