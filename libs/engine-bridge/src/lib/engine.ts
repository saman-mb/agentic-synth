// ── WebAudio synth engine facade (issue #280) ────────────────────────
//
// Design intent: the only module the UI bridge talks to. Owns the
// AudioContext graph (VoiceManager → effect rack → master → analyser →
// worklet tap → destination), the live PatchParams copy, and the macro
// routing surface. setParam/setPatch mutate the patch immediately and
// retarget the graph when one exists; before ensureStarted() they only
// stage the patch, which is applied when the context starts.
//
// Approximations:
//  - The worklet is compiled from an inline source string via a Blob
//    URL — mirrors the /public/pcm-tap.js pattern without requiring an
//    extra static asset. It is a pass-through tap; scope samples come
//    from the AnalyserNode. If addModule fails (old browser / CSP) the
//    analyser connects straight to the destination.
//  - Macro routes (and extras) apply as static normalized offsets:
//    base + amount * (max - min), clamped via PARAM_RANGES — see
//    paramMap.macroTargetValue. They are not animated LFOs.
//  - playMidiNote schedules note-off with setTimeout, not the audio
//    clock; sub-frame jitter is acceptable for the demo.
//  - makeFallbackPatch restates KnobGrid.makeDefaultPatch for the
//    pre-patch case (KnobGrid.tsx pulls in React and must stay a
//    type-only import here); the UI default remains the source of
//    truth for what the user sees.
//
// Autoplay: an AudioContext only leaves 'suspended' inside a user
// gesture — the bridge must call ensureStarted() from a click/keydown
// handler before the first note.

import type { AgentModulationPlan, PatchParams } from '@agentic-synth/shared-types';
import { createEffectRack, type EffectRack } from './effects';
import { getPatchParam, isEnvParam, macroTargetValue, setPatchParam } from './paramMap';
import { VoiceManager } from './voices';

export interface SynthEngine {
  ensureStarted(): Promise<void>;
  setPatch(patch: PatchParams): void;
  setParam(name: string, value: number): void;
  applyMacros(mod: AgentModulationPlan): void;
  noteOn(note: number, velocity: number): void;
  noteOff(note: number): void;
  playMidiNote(note: number, velocity: number, durationMs: number): void;
  getScopeSamples(n: number): number[];
  // Routes all output to the given device (AudioContext.setSinkId).
  // Throws when the browser lacks setSinkId or the switch fails — the
  // caller surfaces the message. Empty string restores the default.
  setOutputDevice(deviceId: string): Promise<void>;
  dispose(): void;
}

// Pass-through AudioWorkletProcessor installed from a Blob URL so the
// static deploy needs no extra asset (see header comment).
const TAP_PROCESSOR_SOURCE = [
  'class EngineTapProcessor extends AudioWorkletProcessor {',
  '  process(inputs, outputs) {',
  '    const input = inputs[0];',
  '    const output = outputs[0];',
  '    if (input && output) {',
  '      for (let c = 0; c < output.length; c++) {',
  '        const ic = input[Math.min(c, input.length - 1)];',
  '        if (ic && output[c]) output[c].set(ic);',
  '      }',
  '    }',
  '    return true;',
  '  }',
  '}',
  "registerProcessor('engine-tap', EngineTapProcessor);",
].join('\n');

const SCOPE_FFT_SIZE = 2048;
const MASTER_GAIN_MAX = 1;

function clamp01(v: number): number {
  return Math.min(MASTER_GAIN_MAX, Math.max(0, v));
}

// Minimal silent-ish patch used before the first setPatch() arrives.
// Mirrors KnobGrid.makeDefaultPatch (single enabled saw) — see header
// comment for why it is restated here.
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

// PatchParams is plain JSON data — a JSON round-trip is a sufficient
// deep copy and keeps the engine's copy independent of the caller's.
function clonePatch(patch: PatchParams): PatchParams {
  return JSON.parse(JSON.stringify(patch)) as PatchParams;
}

class WebSynthEngine implements SynthEngine {
  private patch: PatchParams = makeFallbackPatch();
  private ctx: AudioContext | null = null;
  private manager: VoiceManager | null = null;
  private rack: EffectRack | null = null;
  private masterGain: GainNode | null = null;
  private analyser: AnalyserNode | null = null;
  private tap: AudioWorkletNode | null = null;
  private startPromise: Promise<void> | null = null;
  private readonly pendingNoteOffs = new Set<number>();
  private disposed = false;

  async ensureStarted(): Promise<void> {
    if (this.disposed) return;
    if (this.ctx) {
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      return;
    }
    if (!this.startPromise) {
      this.startPromise = this.start().catch((err) => {
        // Allow a later user gesture to retry after a failed start
        // (e.g. AudioContext construction threw).
        this.startPromise = null;
        throw err;
      });
    }
    await this.startPromise;
  }

  private async start(): Promise<void> {
    const ctx = new AudioContext({ latencyHint: 'interactive' });
    const manager = new VoiceManager(ctx, () => this.patch);
    const rack = createEffectRack(ctx);
    const masterGain = ctx.createGain();
    masterGain.gain.value = clamp01(this.patch.master_gain);
    const analyser = ctx.createAnalyser();
    analyser.fftSize = SCOPE_FFT_SIZE;

    manager.output.connect(rack.input);
    rack.output.connect(masterGain);
    masterGain.connect(analyser);

    try {
      const url = URL.createObjectURL(new Blob([TAP_PROCESSOR_SOURCE], { type: 'text/javascript' }));
      try {
        await ctx.audioWorklet.addModule(url);
      } finally {
        URL.revokeObjectURL(url);
      }
      const tap = new AudioWorkletNode(ctx, 'engine-tap');
      analyser.connect(tap);
      tap.connect(ctx.destination);
      this.tap = tap;
    } catch {
      // Graceful degradation: no worklet support — the analyser feeds
      // the destination directly and everything else still works.
      analyser.connect(ctx.destination);
    }

    this.ctx = ctx;
    this.manager = manager;
    this.rack = rack;
    this.masterGain = masterGain;
    this.analyser = analyser;
    this.applyPatchToGraph();
  }

  private applyPatchToGraph(): void {
    if (!this.ctx || !this.manager || !this.rack || !this.masterGain) return;
    const t = this.ctx.currentTime;
    this.rack.setReverb(this.patch.reverb);
    this.rack.setDelay(this.patch.delay);
    this.masterGain.gain.setTargetAtTime(clamp01(this.patch.master_gain), t, 0.02);
    this.manager.setPatch();
  }

  setPatch(patch: PatchParams): void {
    this.patch = clonePatch(patch);
    if (this.ctx) this.applyPatchToGraph();
  }

  setParam(name: string, value: number): void {
    if (!setPatchParam(this.patch, name, value)) return;
    if (!this.ctx) return; // staged — applied when the context starts
    // Envelopes apply to new notes only (running ADSR is not re-shaped).
    if (isEnvParam(name)) return;
    if (name.startsWith('reverb.')) {
      this.rack?.setReverb(this.patch.reverb);
      return;
    }
    if (name.startsWith('delay.')) {
      this.rack?.setDelay(this.patch.delay);
      return;
    }
    if (name === 'master_gain') {
      this.masterGain?.gain.setTargetAtTime(clamp01(this.patch.master_gain), this.ctx.currentTime, 0.02);
      return;
    }
    if (name === 'voice_count' || name === 'portamento_s') return; // read per-note
    // osc.* / lfo.* / filter.* — a full retarget is cheap (iterates
    // live voices) and keeps every knob path on one code path.
    this.manager?.setPatch();
  }

  applyMacros(mod: AgentModulationPlan): void {
    if (!mod) return;
    const routes: { target: string; amount: number }[] = [];
    for (const macro of mod.macros ?? []) {
      for (const route of macro.routes ?? []) routes.push(route);
    }
    // Extras are applied with the same static-offset projection; the
    // source discriminator does not matter for a non-animated route.
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
    if (!this.ctx || this.disposed) return;
    this.manager?.noteOn(note, velocity);
  }

  noteOff(note: number): void {
    if (!this.ctx || this.disposed) return;
    this.manager?.noteOff(note);
  }

  playMidiNote(note: number, velocity: number, durationMs: number): void {
    if (!this.ctx || this.disposed) return;
    if (typeof durationMs !== 'number' || !Number.isFinite(durationMs) || durationMs <= 0) return;
    this.noteOn(note, velocity);
    const id = window.setTimeout(() => {
      this.pendingNoteOffs.delete(id);
      this.noteOff(note);
    }, durationMs);
    this.pendingNoteOffs.add(id);
  }

  getScopeSamples(n: number): number[] {
    const count = Math.max(0, Math.min(Math.floor(n), SCOPE_FFT_SIZE));
    if (!this.analyser || count === 0) return new Array<number>(count).fill(0);
    const buf = new Float32Array(count);
    this.analyser.getFloatTimeDomainData(buf);
    return Array.from(buf);
  }

  async setOutputDevice(deviceId: string): Promise<void> {
    if (this.disposed) throw new Error('Audio engine disposed');
    await this.ensureStarted();
    const ctx = this.ctx;
    if (!ctx) throw new Error('Audio engine not started');
    // setSinkId is Chrome/Edge-only and not yet in TS lib.dom — narrow
    // capability check instead of a blind cast.
    const sink = ctx as AudioContext & { setSinkId?: (sinkId: string) => Promise<void> };
    if (typeof sink.setSinkId !== 'function') {
      throw new Error('Output device selection is not supported in this browser.');
    }
    await sink.setSinkId(deviceId);
  }

  dispose(): void {
    this.disposed = true;
    for (const id of this.pendingNoteOffs) window.clearTimeout(id);
    this.pendingNoteOffs.clear();
    this.manager?.dispose();
    this.manager = null;
    this.rack?.dispose();
    this.rack = null;
    this.tap?.disconnect();
    this.tap = null;
    this.analyser?.disconnect();
    this.analyser = null;
    this.masterGain?.disconnect();
    this.masterGain = null;
    const ctx = this.ctx;
    this.ctx = null;
    this.startPromise = null;
    if (ctx && ctx.state !== 'closed') {
      void ctx.close().catch(() => undefined);
    }
  }
}

export function createSynthEngine(): SynthEngine {
  return new WebSynthEngine();
}
