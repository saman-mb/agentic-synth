import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import { WebSynthEngine, type SynthEngine } from './engine';
import { WasmSynthEngine, WASM_PROCESSOR_NAME, wasmWorkletUrl } from './wasmEngine';
import { isWebAudioSupported } from './audioEnvironment';

// How long the probe may hold up the very first note. The probe starts at
// construction (app boot), so it has normally settled long before a user
// gesture arrives; this only bounds the pathological case, because making
// the user wait is worse than starting on the always-available engine.
const PROBE_BUDGET_MS = 400;
const PROBE_TIMEOUT_MS = 2500;

export type EngineKind = 'wasm' | 'webaudio';

function delay<T>(ms: number, value: T): Promise<T> {
  return new Promise((resolve) => {
    globalThis.setTimeout(() => resolve(value), ms);
  });
}

// Asks the real worklet whether it can actually boot, on a throwaway
// offline context: no audio device, no user gesture, and no half-started
// realtime context left behind if the answer is no. A missing asset, an
// unsupported API inside AudioWorkletGlobalScope, or a failed WASM
// instantiation all surface here as `false` rather than as silence.
export async function probeWasmWorklet(
  workletUrl: string,
  processorName: string,
): Promise<boolean> {
  const Offline = (globalThis as { OfflineAudioContext?: typeof OfflineAudioContext })
    .OfflineAudioContext;
  if (!Offline) return false;

  let ctx: OfflineAudioContext;
  try {
    ctx = new Offline(2, 256, 48000);
  } catch {
    return false;
  }
  if (!ctx.audioWorklet) return false;

  try {
    await ctx.audioWorklet.addModule(workletUrl);
  } catch {
    return false;
  }

  let node: AudioWorkletNode;
  try {
    node = new AudioWorkletNode(ctx, processorName, {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
    });
  } catch {
    return false;
  }
  node.connect(ctx.destination);

  const settled = new Promise<boolean>((resolve) => {
    node.port.onmessage = (ev: MessageEvent) => {
      const data = ev.data as { type?: string } | null;
      if (!data || typeof data !== 'object') return;
      if (data.type === 'ready') resolve(true);
      else if (data.type === 'error') resolve(false);
    };
  });

  // The processor is not constructed until the context renders.
  void ctx.startRendering().catch(() => undefined);

  const ok = await Promise.race([settled, delay(PROBE_TIMEOUT_MS, false)]);
  try {
    node.port.postMessage({ type: 'dispose' });
    node.disconnect();
  } catch {
    // Teardown is best-effort; the context is garbage either way.
  }
  return ok;
}

// Picks the best engine the current browser can actually run, and keeps
// the pure-WebAudio engine as a guaranteed floor. Every state mutation
// made before the choice is settled is applied to both candidates, so
// whichever one starts already has the current patch.
export class ResilientSynthEngine implements SynthEngine {
  private readonly fallback = new WebSynthEngine();
  private readonly preferred = new WasmSynthEngine();
  private active: SynthEngine | null = null;
  private activeKind: EngineKind | null = null;
  private starting: Promise<void> | null = null;
  private readonly probe: Promise<boolean>;
  private disposed = false;

  constructor() {
    this.probe = probeWasmWorklet(wasmWorkletUrl(), WASM_PROCESSOR_NAME).catch(() => false);
  }

  get kind(): EngineKind | null {
    return this.activeKind;
  }

  async ensureStarted(): Promise<void> {
    if (this.disposed) return;
    if (this.active) {
      await this.active.ensureStarted();
      return;
    }
    if (!isWebAudioSupported()) throw new Error('Web Audio is not supported in this browser.');
    this.starting ??= this.startOnce().finally(() => {
      this.starting = null;
    });
    await this.starting;
  }

  private async startOnce(): Promise<void> {
    const useWasm = await Promise.race([this.probe, delay(PROBE_BUDGET_MS, false)]);
    if (useWasm) {
      try {
        await this.preferred.ensureStarted();
        this.active = this.preferred;
        this.activeKind = 'wasm';
        return;
      } catch {
        // Probe passed but the realtime start did not: fall through rather
        // than leave the user with no audio at all.
      }
    }
    await this.fallback.ensureStarted();
    this.active = this.fallback;
    this.activeKind = 'webaudio';
  }

  private targets(): SynthEngine[] {
    return this.active ? [this.active] : [this.preferred, this.fallback];
  }

  setPatch(patch: PatchParams): void {
    for (const engine of this.targets()) engine.setPatch(patch);
  }

  setParam(name: string, value: number): void {
    for (const engine of this.targets()) engine.setParam(name, value);
  }

  applyMacros(mod: AgentModulationPlan): void {
    for (const engine of this.targets()) engine.applyMacros(mod);
  }

  noteOn(note: number, velocity: number): void {
    this.active?.noteOn(note, velocity);
  }

  noteOff(note: number): void {
    this.active?.noteOff(note);
  }

  playMidiNote(note: number, velocity: number, durationMs: number): void {
    this.active?.playMidiNote(note, velocity, durationMs);
  }

  getScopeSamples(n: number): number[] {
    if (this.active) return this.active.getScopeSamples(n);
    return new Array<number>(Math.max(0, Math.floor(n))).fill(0);
  }

  async setOutputDevice(deviceId: string): Promise<void> {
    if (this.disposed) throw new Error('Audio engine disposed');
    await this.ensureStarted();
    if (!this.active) throw new Error('Audio engine not started');
    await this.active.setOutputDevice(deviceId);
  }

  dispose(): void {
    this.disposed = true;
    this.preferred.dispose();
    this.fallback.dispose();
    this.active = null;
  }
}

export function createSynthEngine(): SynthEngine {
  return new ResilientSynthEngine();
}
