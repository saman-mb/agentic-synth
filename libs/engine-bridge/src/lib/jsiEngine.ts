// JSI (React Native) SynthEngine — control-rate only (#308).
// Native AudioStream owns ags_engine_render on the RT thread; this class
// marshals PatchParams → PatchStruct bytes on the JS thread via packPatchParams
// and maps AGS_ERR_* return codes to AgsynthError. Native never throws.

import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import type { SynthEngine } from './engine';
import { packPatchParams } from './patchAbi';
import { getPatchParam, macroTargetValue, setPatchParam } from './paramMap';

const SCOPE_FFT_SIZE = 2048;

/** C API `ags_engine_*` return codes, plus QUEUE for the JSI event path. */
export const AGS_OK = 0;
export const AGS_ERR_PARAM = 1;
export const AGS_ERR_SIZE = 2;
export const AGS_ERR_STATE = 3;
export const AGS_ERR_NULL = 4;
export const AGS_ERR_QUEUE = 5;

export type AgsynthErrorCode = 'PARAM' | 'SIZE' | 'NULL' | 'STATE' | 'QUEUE';

const STATUS_TO_CODE: Record<number, AgsynthErrorCode> = {
  [AGS_ERR_PARAM]: 'PARAM',
  [AGS_ERR_SIZE]: 'SIZE',
  [AGS_ERR_STATE]: 'STATE',
  [AGS_ERR_NULL]: 'NULL',
  [AGS_ERR_QUEUE]: 'QUEUE',
};

export class AgsynthError extends Error {
  readonly code: AgsynthErrorCode;

  constructor(code: AgsynthErrorCode, message: string) {
    super(message);
    this.name = 'AgsynthError';
    this.code = code;
  }
}

export interface JsiNativeBinding {
  setPatch(bytes: ArrayBuffer): number | void;
  setParam(path: string, value: number): number | void;
  noteOn(note: number, velocity: number): number | void;
  noteOff(note: number): number | void;
  dispose(): number | void;
  recreate(sampleRate: number): number | void;
  start?(): number | void | Promise<number | void>;
  renderOffline?(
    patchBytes: ArrayBuffer,
    events: unknown,
    sampleRate: number,
    frames: number,
  ): Float32Array | number;
}

function makeFallbackPatch(): PatchParams {
  const osc = {
    type: 2,
    volume: 1,
    detune_cents: 0,
    semitone_offset: 0,
    wavetable_pos: 0,
    fm_ratio: 1,
    fm_depth: 0,
    pulse_width: 0.5,
    pan: 0,
    enabled: 1,
  };
  return {
    osc: [{ ...osc }, { ...osc, volume: 0 }, { ...osc, volume: 0 }],
    filter: { type: 0, cutoff_hz: 18000, resonance: 0, env_mod: 0, key_track: 0, drive: 0 },
    filter_env: { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 },
    amp_env: { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 },
    lfo: [
      { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
      { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
    ],
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0, stereo: 0.5, bpm_sync: 0 },
    master_gain: 1,
    portamento_s: 0,
    voice_count: 8,
  };
}

function clonePatch(patch: PatchParams): PatchParams {
  return JSON.parse(JSON.stringify(patch)) as PatchParams;
}

function mapNativeStatus(status: number | void, action: string): void {
  if (status === undefined || status === AGS_OK) return;
  const code = STATUS_TO_CODE[status] ?? 'STATE';
  throw new AgsynthError(code, `Native ${action} failed (${code})`);
}

export class JsiSynthEngine implements SynthEngine {
  private patch: PatchParams = makeFallbackPatch();
  private readonly binding: JsiNativeBinding;
  private readonly pendingNoteOffs = new Set<number>();
  private disposed = false;

  constructor(binding: JsiNativeBinding) {
    if (binding == null) {
      throw new AgsynthError('NULL', 'Native JSI binding is required');
    }
    if (typeof binding !== 'object') {
      throw new AgsynthError('PARAM', 'Native JSI binding must be an object');
    }
    this.binding = binding;
  }

  async ensureStarted(): Promise<void> {
    if (this.disposed) return;
    if (typeof this.binding.start === 'function') {
      mapNativeStatus(await this.binding.start(), 'start');
    }
  }

  setPatch(patch: PatchParams): void {
    if (this.disposed) throw new AgsynthError('STATE', 'Audio engine disposed');
    if (patch == null) {
      throw new AgsynthError('NULL', 'Patch is null');
    }
    if (typeof patch !== 'object' || Array.isArray(patch)) {
      throw new AgsynthError('PARAM', 'Patch must be an object');
    }
    this.patch = clonePatch(patch);
    const bytes = packPatchParams(this.patch);
    mapNativeStatus(this.binding.setPatch(bytes), 'setPatch');
  }

  setParam(name: string, value: number): void {
    if (this.disposed) throw new AgsynthError('STATE', 'Audio engine disposed');
    if (typeof name !== 'string' || name.length === 0) {
      throw new AgsynthError('PARAM', 'Param path must be a non-empty string');
    }
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      throw new AgsynthError('PARAM', `Invalid param value for ${name}`);
    }
    if (!setPatchParam(this.patch, name, value)) {
      throw new AgsynthError('PARAM', `Unknown param path: ${name}`);
    }
    mapNativeStatus(this.binding.setParam(name, value), 'setParam');
  }

  applyMacros(mod: AgentModulationPlan): void {
    if (!mod) return;
    const routes: { target: string; amount: number }[] = [];
    for (const macro of mod.macros ?? []) {
      for (const route of macro.routes ?? []) routes.push(route);
    }
    for (const extra of mod.extras ?? []) {
      routes.push({ target: extra.target, amount: extra.amount });
    }
    for (const route of routes) {
      if (!route || typeof route.target !== 'string') continue;
      const base = getPatchParam(this.patch, route.target);
      if (base === null) continue;
      this.setParam(route.target, macroTargetValue(route.target, base, route.amount));
    }
  }

  noteOn(note: number, velocity: number): void {
    if (this.disposed) return;
    if (!Number.isFinite(note) || !Number.isFinite(velocity)) {
      throw new AgsynthError('PARAM', 'noteOn requires finite note and velocity');
    }
    mapNativeStatus(this.binding.noteOn(note, velocity), 'noteOn');
  }

  noteOff(note: number): void {
    if (this.disposed) return;
    if (!Number.isFinite(note)) {
      throw new AgsynthError('PARAM', 'noteOff requires a finite note');
    }
    mapNativeStatus(this.binding.noteOff(note), 'noteOff');
  }

  playMidiNote(note: number, velocity: number, durationMs: number): void {
    if (this.disposed) return;
    if (typeof durationMs !== 'number' || !Number.isFinite(durationMs) || durationMs <= 0) return;
    this.noteOn(note, velocity);
    const id = globalThis.setTimeout(() => {
      this.pendingNoteOffs.delete(id);
      this.noteOff(note);
    }, durationMs);
    this.pendingNoteOffs.add(id);
  }

  getScopeSamples(n: number): number[] {
    const count = Math.max(0, Math.min(Math.floor(n), SCOPE_FFT_SIZE));
    return new Array<number>(count).fill(0);
  }

  async setOutputDevice(_deviceId: string): Promise<void> {
    throw new AgsynthError(
      'STATE',
      'Output device selection is not supported on the native JSI engine (no WebAudio sinkId).',
    );
  }

  recreate(sampleRate: number): void {
    if (this.disposed) throw new AgsynthError('STATE', 'Audio engine disposed');
    if (typeof sampleRate !== 'number' || !Number.isFinite(sampleRate) || sampleRate <= 0) {
      throw new AgsynthError('PARAM', 'sampleRate must be a finite positive number');
    }
    mapNativeStatus(this.binding.recreate(sampleRate), 'recreate');
  }

  renderOffline(
    events: unknown,
    sampleRate: number,
    frames: number,
  ): Float32Array {
    if (this.disposed) throw new AgsynthError('STATE', 'Audio engine disposed');
    if (typeof this.binding.renderOffline !== 'function') {
      throw new AgsynthError('STATE', 'renderOffline is not available on this binding');
    }
    const bytes = packPatchParams(this.patch);
    const result = this.binding.renderOffline(bytes, events, sampleRate, frames);
    if (typeof result === 'number') {
      mapNativeStatus(result, 'renderOffline');
      throw new AgsynthError('STATE', 'Native renderOffline returned a status with no buffer');
    }
    return result;
  }

  dispose(): void {
    this.disposed = true;
    for (const id of this.pendingNoteOffs) globalThis.clearTimeout(id);
    this.pendingNoteOffs.clear();
    try {
      this.binding.dispose();
    } catch {
      // Teardown must not throw; native is best-effort here.
    }
  }
}
