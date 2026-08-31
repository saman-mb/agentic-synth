// ── Patch codec for the web demo ────────────────────────────────────
//
// Single shared codec between the LLM's string-enum PatchStruct JSON
// (system-prompt.md §1) and the UI's numeric PatchParams. Bundled into
// BOTH the browser (vite) and the Netlify function (esbuild), so all
// runtime imports here must stay React-free: PatchParams is a
// type-only import from @agentic-synth/shared-types, and the numeric
// ranges come from libs/data paramRanges.ts (PARAM_RANGES) via a
// relative import — Netlify esbuild cannot resolve tsconfig paths.
//
// GrammarSampler parity (#280): reject, never coerce. convertLlmPatch
// maps enums → ints and booleans → 0/1 but invents nothing — a missing
// or mistyped field surfaces as NaN and is rejected by validatePatch,
// mirroring the C++ parser's fail-closed behaviour (except key ORDER,
// which is unobservable once JSON.parse has produced an object).
// version / patch_id / rationale are LLM-transport fields and are
// intentionally dropped during conversion.

import type { PatchParams } from '@agentic-synth/shared-types';
// eslint-disable-next-line @nx/enforce-module-boundaries -- Netlify esbuild cannot resolve tsconfig paths
import { PARAM_RANGES, type ParamRange } from '../../../../libs/data/src/lib/paramRanges.ts';

// Numeric enum values mirror src/engine/PatchStruct.h exactly.
export const OSC_TYPES = {
  Sine: 0, Triangle: 1, Sawtooth: 2, Square: 3,
  Pulse: 4, Wavetable: 5, FM: 6, Noise: 7,
} as const;

export const FILTER_TYPES = {
  LowPass: 0, HighPass: 1, BandPass: 2, Notch: 3, Peak: 4,
} as const;

export const LFO_WAVEFORMS = {
  Sine: 0, Triangle: 1, Sawtooth: 2, Square: 3, SampleAndHold: 4,
} as const;

export const LFO_TARGETS = {
  None: 0, Pitch: 1, FilterCutoff: 2, Amplitude: 3,
  Pan: 4, WavetablePos: 5, FmRatio: 6,
} as const;

export type OscTypeName = keyof typeof OSC_TYPES;
export type FilterTypeName = keyof typeof FILTER_TYPES;
export type LfoWaveformName = keyof typeof LFO_WAVEFORMS;
export type LfoTargetName = keyof typeof LFO_TARGETS;

// String-enum shape the LLM emits (system-prompt.md §1 field order).
export interface LlmOsc {
  type: OscTypeName;
  semitone_offset: number;
  detune_cents: number;
  wavetable_pos: number;
  fm_ratio: number;
  fm_depth: number;
  volume: number;
  pan: number;
  pulse_width: number;
  enabled: boolean;
}

export interface LlmFilter {
  type: FilterTypeName;
  cutoff_hz: number;
  resonance: number;
  env_mod: number;
  key_track: number;
  drive: number;
}

export interface LlmEnv {
  attack_s: number;
  decay_s: number;
  sustain: number;
  release_s: number;
}

export interface LlmLfo {
  waveform: LfoWaveformName;
  target: LfoTargetName;
  rate_hz: number;
  depth: number;
  phase_offset: number;
  bpm_sync: boolean;
}

export interface LlmReverb {
  size: number;
  damping: number;
  width: number;
  mix: number;
}

export interface LlmDelay {
  time_s: number;
  feedback: number;
  mix: number;
  stereo: number;
  bpm_sync: boolean;
}

export interface LlmPatch {
  version: number;
  patch_id: number;
  osc: LlmOsc[];
  filter: LlmFilter;
  filter_env: LlmEnv;
  amp_env: LlmEnv;
  lfo: LlmLfo[];
  reverb: LlmReverb;
  delay: LlmDelay;
  master_gain: number;
  portamento_s: number;
  voice_count: number;
  rationale?: string;
}

// String enum name → int. Unknown names (runtime garbage the type system
// can't see) become NaN so validatePatch rejects the patch downstream.
function enumToInt(map: Record<string, number>, name: string): number {
  const n = map[name];
  return typeof n === 'number' ? n : Number.NaN;
}

// Strict boolean → 0/1. Anything but a real boolean becomes NaN.
function boolToInt(b: boolean): number {
  return b === true ? 1 : b === false ? 0 : Number.NaN;
}

export function convertLlmPatch(llm: LlmPatch): PatchParams {
  return {
    osc: llm.osc.map((o) => ({
      type: enumToInt(OSC_TYPES, o.type),
      volume: o.volume,
      detune_cents: o.detune_cents,
      semitone_offset: o.semitone_offset,
      wavetable_pos: o.wavetable_pos,
      fm_ratio: o.fm_ratio,
      fm_depth: o.fm_depth,
      pulse_width: o.pulse_width,
      pan: o.pan,
      enabled: boolToInt(o.enabled),
    })),
    filter: {
      type: enumToInt(FILTER_TYPES, llm.filter.type),
      cutoff_hz: llm.filter.cutoff_hz,
      resonance: llm.filter.resonance,
      env_mod: llm.filter.env_mod,
      key_track: llm.filter.key_track,
      drive: llm.filter.drive,
    },
    filter_env: { ...llm.filter_env },
    amp_env: { ...llm.amp_env },
    lfo: llm.lfo.map((l) => ({
      waveform: enumToInt(LFO_WAVEFORMS, l.waveform),
      target: enumToInt(LFO_TARGETS, l.target),
      rate_hz: l.rate_hz,
      depth: l.depth,
      phase_offset: l.phase_offset,
      bpm_sync: boolToInt(l.bpm_sync),
    })),
    reverb: { ...llm.reverb },
    delay: {
      time_s: llm.delay.time_s,
      feedback: llm.delay.feedback,
      mix: llm.delay.mix,
      stereo: llm.delay.stereo,
      bpm_sync: boolToInt(llm.delay.bpm_sync),
    },
    master_gain: llm.master_gain,
    portamento_s: llm.portamento_s,
    voice_count: llm.voice_count,
  };
}

// ---------------------------------------------------------------------------
// validatePatch — GrammarSampler parity: reject, never coerce.
// ---------------------------------------------------------------------------

function rangeFor(key: string): ParamRange | undefined {
  return PARAM_RANGES[key];
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

// Finite number (rejects NaN/Infinity/missing/wrong type — never coerced).
function isNumber(v: unknown): v is number {
  return typeof v === 'number' && Number.isFinite(v);
}

// `prop` is the property on `obj`; `path` is both the PARAM_RANGES lookup
// key ("osc.0.volume", "filter.cutoff_hz", "master_gain", …) and the
// error-message path.
function checkFloat(
  obj: Record<string, unknown>,
  prop: string,
  path: string,
  fail: (msg: string) => void,
): void {
  const v = obj[prop];
  if (!isNumber(v)) {
    fail(`${path}: expected finite number`);
    return;
  }
  const r = rangeFor(path);
  if (r === undefined) {
    fail(`${path}: no range registered`);
    return;
  }
  if (v < r.min || v > r.max) {
    fail(`${path}: ${v} out of range [${r.min}, ${r.max}]`);
  }
}

// Integer enum bound check (osc.type 0-7, filter.type 0-4, …).
function checkEnumInt(
  obj: Record<string, unknown>,
  prop: string,
  path: string,
  min: number,
  max: number,
  fail: (msg: string) => void,
): void {
  const v = obj[prop];
  if (typeof v !== 'number' || !Number.isInteger(v) || v < min || v > max) {
    fail(`${path}: expected integer ${min}-${max}`);
  }
}

function checkBoolFlag(
  obj: Record<string, unknown>,
  prop: string,
  path: string,
  fail: (msg: string) => void,
): void {
  const v = obj[prop];
  if (v !== 0 && v !== 1) {
    fail(`${path}: expected 0 or 1`);
  }
}

function checkEnv(
  env: Record<string, unknown>,
  prefix: string,
  fail: (msg: string) => void,
): void {
  checkFloat(env, 'attack_s', `${prefix}.attack_s`, fail);
  checkFloat(env, 'decay_s', `${prefix}.decay_s`, fail);
  checkFloat(env, 'sustain', `${prefix}.sustain`, fail);
  checkFloat(env, 'release_s', `${prefix}.release_s`, fail);
}

export function validatePatch(p: unknown): { ok: true } | { ok: false; error: string } {
  const errors: string[] = [];
  const fail = (msg: string): void => {
    if (errors.length === 0) errors.push(msg);
  };

  if (!isRecord(p)) {
    return { ok: false, error: 'patch: expected object' };
  }

  // Engine has fixed-size voice arrays (GrammarSampler kMaxOscillators /
  // kMaxLfos): exactly 3 oscs and 2 LFOs, always present.
  if (!Array.isArray(p.osc) || p.osc.length !== 3) {
    return { ok: false, error: 'osc: expected array of exactly 3 oscillators' };
  }
  if (!Array.isArray(p.lfo) || p.lfo.length !== 2) {
    return { ok: false, error: 'lfo: expected array of exactly 2 LFOs' };
  }

  p.osc.forEach((o, i) => {
    if (!isRecord(o)) {
      fail(`osc.${i}: expected object`);
      return;
    }
    checkEnumInt(o, 'type', `osc.${i}.type`, 0, 7, fail);
    checkBoolFlag(o, 'enabled', `osc.${i}.enabled`, fail);
    for (const key of ['volume', 'detune_cents', 'semitone_offset', 'wavetable_pos', 'pan', 'pulse_width', 'fm_ratio', 'fm_depth'] as const) {
      checkFloat(o, key, `osc.${i}.${key}`, fail);
    }
  });

  if (isRecord(p.filter)) {
    checkEnumInt(p.filter, 'type', 'filter.type', 0, 4, fail);
    for (const key of ['cutoff_hz', 'resonance', 'env_mod', 'key_track', 'drive'] as const) {
      checkFloat(p.filter, key, `filter.${key}`, fail);
    }
  } else {
    fail('filter: expected object');
  }

  if (isRecord(p.filter_env)) checkEnv(p.filter_env, 'filter_env', fail);
  else fail('filter_env: expected object');

  if (isRecord(p.amp_env)) checkEnv(p.amp_env, 'amp_env', fail);
  else fail('amp_env: expected object');

  p.lfo.forEach((l, i) => {
    if (!isRecord(l)) {
      fail(`lfo.${i}: expected object`);
      return;
    }
    checkEnumInt(l, 'waveform', `lfo.${i}.waveform`, 0, 4, fail);
    checkEnumInt(l, 'target', `lfo.${i}.target`, 0, 6, fail);
    checkBoolFlag(l, 'bpm_sync', `lfo.${i}.bpm_sync`, fail);
    for (const key of ['rate_hz', 'depth', 'phase_offset'] as const) {
      checkFloat(l, key, `lfo.${i}.${key}`, fail);
    }
  });

  if (isRecord(p.reverb)) {
    for (const key of ['size', 'damping', 'width', 'mix'] as const) {
      checkFloat(p.reverb, key, `reverb.${key}`, fail);
    }
  } else {
    fail('reverb: expected object');
  }

  if (isRecord(p.delay)) {
    checkBoolFlag(p.delay, 'bpm_sync', 'delay.bpm_sync', fail);
    for (const key of ['time_s', 'feedback', 'mix', 'stereo'] as const) {
      checkFloat(p.delay, key, `delay.${key}`, fail);
    }
  } else {
    fail('delay: expected object');
  }

  checkFloat(p, 'master_gain', 'master_gain', fail);
  checkFloat(p, 'portamento_s', 'portamento_s', fail);
  checkEnumInt(p, 'voice_count', 'voice_count', 1, 16, fail);

  if (errors.length > 0) return { ok: false, error: errors[0] };
  return { ok: true };
}
