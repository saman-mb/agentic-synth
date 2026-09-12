// ── WebAudio param-path mapping (issue #280) ─────────────────────────
//
// Design intent: the single place that understands dotted param paths
// ("filter.cutoff_hz", "osc.0.volume", "master_gain") for the browser
// synth engine. Pure data transforms only — no AudioContext access —
// so paths can be resolved in unit tests without an audio device.
//
// Ranges come from PARAM_RANGES in data/modulation.ts (single source
// of truth; do not duplicate them here). The enum mirrors below
// restate the C++ PatchStruct.h enums numerically — the web engine has
// no FFI, so the numbers are copied by hand and kept in sync by review
// (see src/engine/PatchStruct.h).
//
// Approximations:
//  - Macro projection follows the modulation.ts convention: a
//    normalized macro amount scales a delta of (max - min) natural
//    units, clamped to the range. Unknown paths fall back to the
//    identity range [0, 1] per the PARAM_RANGES header comment.
//  - voice_count has no PARAM_RANGES entry (not knob-mapped), so it is
//    clamped locally to 1..32.

import type { PatchParams } from '@agentic-synth/shared-types';
import { PARAM_RANGES } from '@agentic-synth/data';

// Mirrors PatchStruct.h OscType.
export const OSC_TYPE = {
  SINE: 0,
  TRIANGLE: 1,
  SAWTOOTH: 2,
  SQUARE: 3,
  PULSE: 4,
  WAVETABLE: 5,
  FM: 6,
  NOISE: 7,
} as const;

// Mirrors PatchStruct.h FilterType → BiquadFilterNode.type.
export const BIQUAD_TYPES: readonly BiquadFilterNode['type'][] = [
  'lowpass',
  'highpass',
  'bandpass',
  'notch',
  'peaking',
];

// Mirrors PatchStruct.h LfoWaveform.
export const LFO_WAVEFORM = {
  SINE: 0,
  TRIANGLE: 1,
  SAWTOOTH: 2,
  SQUARE: 3,
  SAMPLE_AND_HOLD: 4,
} as const;

// Mirrors PatchStruct.h LfoTarget. WAVETABLE_POS is a no-op in the web
// renderer (PeriodicWave cannot be automated per-sample).
export const LFO_TARGET = {
  NONE: 0,
  PITCH: 1,
  FILTER_CUTOFF: 2,
  AMPLITUDE: 3,
  PAN: 4,
  WAVETABLE_POS: 5,
  FM_RATIO: 6,
} as const;

const VOICE_COUNT_MIN = 1;
const VOICE_COUNT_MAX = 32;

// Assign value to an own numeric field; returns false for unknown or
// inherited keys (hasOwnProperty rejects 'constructor'-style paths).
// Object.hasOwn needs lib ES2022; this project targets ES2020.
function setNumber(target: object, key: string, value: number): boolean {
  if (!Object.prototype.hasOwnProperty.call(target, key)) return false;
  const slot = target as Record<string, number>;
  if (typeof slot[key] !== 'number') return false;
  slot[key] = value;
  return true;
}

function getNumber(target: object, key: string): number | null {
  if (!Object.prototype.hasOwnProperty.call(target, key)) return null;
  const slot = target as Record<string, number>;
  return typeof slot[key] === 'number' ? slot[key] : null;
}

// Resolve a dotted path against the patch, in place. Returns false for
// unrecognized paths so callers can ignore them silently.
export function setPatchParam(patch: PatchParams, name: string, value: number): boolean {
  if (typeof value !== 'number' || !Number.isFinite(value)) return false;
  const parts = name.split('.');
  const head = parts[0] ?? '';
  const leaf = parts[parts.length - 1] ?? '';

  if (head === 'osc' && parts.length === 3) {
    const osc = patch.osc[Number(parts[1])];
    return osc ? setNumber(osc, leaf, value) : false;
  }
  if (head === 'lfo' && parts.length === 3) {
    const lfo = patch.lfo[Number(parts[1])];
    return lfo ? setNumber(lfo, leaf, value) : false;
  }
  if (parts.length !== 1 && parts.length !== 2) return false;
  if (head === 'filter') return setNumber(patch.filter, leaf, value);
  if (head === 'amp_env') return setNumber(patch.amp_env, leaf, value);
  if (head === 'filter_env') return setNumber(patch.filter_env, leaf, value);
  if (head === 'reverb') return setNumber(patch.reverb, leaf, value);
  if (head === 'delay') return setNumber(patch.delay, leaf, value);
  if (head === 'master_gain' && parts.length === 1) {
    patch.master_gain = value;
    return true;
  }
  if (head === 'portamento_s' && parts.length === 1) {
    patch.portamento_s = value;
    return true;
  }
  if (head === 'voice_count' && parts.length === 1) {
    patch.voice_count = Math.min(VOICE_COUNT_MAX, Math.max(VOICE_COUNT_MIN, Math.round(value)));
    return true;
  }
  return false;
}

// Read a dotted path; null when unrecognized.
export function getPatchParam(patch: PatchParams, name: string): number | null {
  const parts = name.split('.');
  const head = parts[0] ?? '';
  const leaf = parts[parts.length - 1] ?? '';
  if (head === 'osc' && parts.length === 3) {
    const osc = patch.osc[Number(parts[1])];
    return osc ? getNumber(osc, leaf) : null;
  }
  if (head === 'lfo' && parts.length === 3) {
    const lfo = patch.lfo[Number(parts[1])];
    return lfo ? getNumber(lfo, leaf) : null;
  }
  if (head === 'filter' && parts.length === 2) return getNumber(patch.filter, leaf);
  if (head === 'amp_env' && parts.length === 2) return getNumber(patch.amp_env, leaf);
  if (head === 'filter_env' && parts.length === 2) return getNumber(patch.filter_env, leaf);
  if (head === 'reverb' && parts.length === 2) return getNumber(patch.reverb, leaf);
  if (head === 'delay' && parts.length === 2) return getNumber(patch.delay, leaf);
  if (name === 'master_gain') return patch.master_gain;
  if (name === 'portamento_s') return patch.portamento_s;
  if (name === 'voice_count') return patch.voice_count;
  return null;
}

// Project a normalized macro amount onto a param's natural range:
// base + amount * (max - min), clamped. Matches the macro-projection
// notes on PARAM_RANGES in data/modulation.ts.
export function macroTargetValue(name: string, base: number, amount: number): number {
  const range = PARAM_RANGES[name];
  if (!range) return base + amount;
  const delta = amount * (range.max - range.min);
  return Math.min(range.max, Math.max(range.min, base + delta));
}

// True when the dotted path belongs to an envelope section; envelope
// changes apply to new notes only in the web renderer (a running ADSR
// is not re-shaped mid-flight).
export function isEnvParam(name: string): boolean {
  return name.startsWith('amp_env.') || name.startsWith('filter_env.');
}
