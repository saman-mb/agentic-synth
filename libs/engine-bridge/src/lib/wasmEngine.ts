import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import type { SynthEngine } from './engine';
import { packPatchParams } from './patchAbi';
import { getPatchParam, macroTargetValue, setPatchParam } from './paramMap';
import { createAudioContext, keepAudioContextRunning, resumeAudioContext } from './audioEnvironment';

const WORKLET_FILE = 'agsynth-worklet.js';
const GLUE_FILE = 'agsynth.js';
const WASM_FILE = 'agsynth.wasm';
export const WASM_PROCESSOR_NAME = 'agsynth-worklet';
const SCOPE_FFT_SIZE = 2048;

// Resolved against the document base, not the origin root, so the demo
// also works when deployed under a sub-path. AudioWorkletGlobalScope has
// no `location` and (outside WebKit) no `URL`, so the worklet cannot work
// these out for itself — they are handed to it via processorOptions.
export function assetUrl(file: string): string {
  const base =
    typeof document !== 'undefined' && document.baseURI
      ? document.baseURI
      : `${globalThis.location?.origin ?? ''}/`;
  try {
    return new URL(file, base).href;
  } catch {
    return `/${file}`;
  }
}

export function wasmWorkletUrl(): string {
  return assetUrl(WORKLET_FILE);
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

function asError(err: unknown): Error {
  return err instanceof Error ? err : new Error(String(err));
}

function waitForWorkletReady(node: AudioWorkletNode): Promise<void> {
  return new Promise((resolve, reject) => {
    const onMessage = (ev: MessageEvent) => {
      const data = ev.data as { type?: string; message?: string } | null;
      if (!data || typeof data !== 'object') return;
      if (data.type === 'ready') {
        node.port.removeEventListener('message', onMessage);
        resolve();
        return;
      }
      if (data.type === 'error') {
        node.port.removeEventListener('message', onMessage);
        reject(new Error(typeof data.message === 'string' ? data.message : 'WASM engine failed to start'));
      }
    };
    node.port.addEventListener('message', onMessage);
    node.port.start();
  });
}

export class WasmSynthEngine implements SynthEngine {
  private patch: PatchParams = makeFallbackPatch();
  private ctx: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private analyser: AnalyserNode | null = null;
  private startPromise: Promise<void> | null = null;
  private stopKeepAlive: (() => void) | null = null;
  private readonly pendingNoteOffs = new Set<ReturnType<typeof setTimeout>>();
  private disposed = false;

  async ensureStarted(): Promise<void> {
    if (this.disposed) return;
    if (this.ctx) {
      await resumeAudioContext(this.ctx);
      return;
    }
    if (!this.startPromise) {
      this.startPromise = this.start().catch((err) => {
        this.startPromise = null;
        throw err;
      });
    }
    await this.startPromise;
  }

  private async start(): Promise<void> {
    const ctx = createAudioContext({ latencyHint: 'interactive' });
    try {
      await ctx.audioWorklet.addModule(wasmWorkletUrl());
    } catch (err) {
      await ctx.close().catch(() => undefined);
      throw asError(err);
    }

    let node: AudioWorkletNode;
    try {
      node = new AudioWorkletNode(ctx, WASM_PROCESSOR_NAME, {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { glueUrl: assetUrl(GLUE_FILE), wasmUrl: assetUrl(WASM_FILE) },
      });
    } catch (err) {
      await ctx.close().catch(() => undefined);
      throw asError(err);
    }

    try {
      await waitForWorkletReady(node);
    } catch (err) {
      node.disconnect();
      await ctx.close().catch(() => undefined);
      throw asError(err);
    }

    const analyser = ctx.createAnalyser();
    analyser.fftSize = SCOPE_FFT_SIZE;
    node.connect(analyser);
    analyser.connect(ctx.destination);

    this.ctx = ctx;
    this.node = node;
    this.analyser = analyser;
    this.stopKeepAlive = keepAudioContextRunning(ctx);
    this.postPatch(this.patch);
  }

  private postPatch(patch: PatchParams): void {
    if (!this.node) return;
    const bytes = packPatchParams(patch);
    this.node.port.postMessage({ type: 'setPatch', bytes }, [bytes]);
  }

  setPatch(patch: PatchParams): void {
    this.patch = clonePatch(patch);
    if (this.node) this.postPatch(this.patch);
  }

  setParam(name: string, value: number): void {
    if (!setPatchParam(this.patch, name, value)) return;
    if (!this.node) return;
    this.node.port.postMessage({ type: 'setParam', path: name, value });
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
    if (!this.node || this.disposed) return;
    this.node.port.postMessage({ type: 'noteOn', note, velocity });
  }

  noteOff(note: number): void {
    if (!this.node || this.disposed) return;
    this.node.port.postMessage({ type: 'noteOff', note });
  }

  playMidiNote(note: number, velocity: number, durationMs: number): void {
    if (!this.node || this.disposed) return;
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
    if (!this.analyser || count === 0) return [];
    // The WASM worklet's single analyser tap is mono; duplicate it across L/R
    // so the bridge shape stays interleaved stereo (#435). XY on this engine
    // therefore draws the honest mono 45-degree line, not a fabricated one.
    const buf = new Float32Array(count);
    this.analyser.getFloatTimeDomainData(buf);
    const out = new Array<number>(count * 2);
    for (let i = 0; i < count; i++) {
      out[i * 2] = buf[i];
      out[i * 2 + 1] = buf[i];
    }
    return out;
  }

  getScopeSampleRate(): number {
    return this.ctx?.sampleRate ?? 48000;
  }

  async setOutputDevice(deviceId: string): Promise<void> {
    if (this.disposed) throw new Error('Audio engine disposed');
    await this.ensureStarted();
    const ctx = this.ctx;
    if (!ctx) throw new Error('Audio engine not started');
    const sink = ctx as AudioContext & { setSinkId?: (sinkId: string) => Promise<void> };
    if (typeof sink.setSinkId !== 'function') {
      throw new Error('Output device selection is not supported in this browser.');
    }
    await sink.setSinkId(deviceId);
  }

  dispose(): void {
    this.disposed = true;
    this.stopKeepAlive?.();
    this.stopKeepAlive = null;
    for (const id of this.pendingNoteOffs) globalThis.clearTimeout(id);
    this.pendingNoteOffs.clear();
    this.node?.port.postMessage({ type: 'dispose' });
    this.node?.disconnect();
    this.node = null;
    this.analyser?.disconnect();
    this.analyser = null;
    const ctx = this.ctx;
    this.ctx = null;
    this.startPromise = null;
    if (ctx && ctx.state !== 'closed') {
      void ctx.close().catch(() => undefined);
    }
  }
}
