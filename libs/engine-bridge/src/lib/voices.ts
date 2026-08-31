// ── Voice management for the WebAudio patch renderer (issue #280) ────
//
// Design intent: polyphonic Voice/VoiceManager mirroring the C++ engine
// (src/engine/) closely enough that a PatchParams patch sounds
// intentional: per-osc type rendering, ADSR amp/filter envelopes,
// LFO[2] with per-target depth buses, portamento glide and
// voice_count stealing (oldest first). The manager reads the live
// patch through an injected getter so the engine can swap or tweak
// params without re-creating voices.
//
// Approximations (web build, no FFI):
//  - Pulse: one OscillatorNode with a generated PeriodicWave whose
//    harmonic amplitudes follow the duty-cycle Fourier series
//    (a_n = sin(n·π·width) / (n·π)); width changes rebuild the wave.
//  - Wavetable: two fixed frames (mellow 1/n², bright 1/n) crossfaded
//    by wavetable_pos into one PeriodicWave; no arbitrary frame sets.
//  - FM: two operators (sine carrier + sine modulator at fm_ratio);
//    peak deviation ≈ fm_depth · carrierFreq · 2.
//  - LFO SampleAndHold: looping random-step AudioBuffer, rate via
//    playbackRate. phase_offset is ignored (OscillatorNode has no
//    phase parameter). bpm_sync is ignored — the standalone web build
//    has no host transport, so LFOs run free at rate_hz.
//  - LFO targets: FmRatio wobbles the FM modulator detune (≈ ratio
//    wobble); WavetablePos is a no-op (PeriodicWave is not
//    per-sample automatable).
//  - Structural osc changes (enabled toggle, tonal↔noise/FM switches)
//    apply to new notes; live voices retarget volume/pan/detune/
//    filter/wave where sensible. Envelopes re-trigger on noteOn only;
//    live edits retarget the sustain region, not a running attack.

import type { PatchParams } from '@agentic-synth/shared-types';
import { BIQUAD_TYPES, LFO_TARGET, LFO_WAVEFORM, OSC_TYPE } from './paramMap';

const A4_MIDI = 69;
const A4_HZ = 440;
const CUTOFF_MIN = 20;
const CUTOFF_MAX = 20000;
const PULSE_HARMONICS = 32;
const WAVETABLE_HARMONICS = 24;
const DRIVE_CURVE_POINTS = 1024;
const VOICE_COUNT_MIN = 1;
const VOICE_COUNT_MAX = 32;
const NATIVE_WAVES: readonly OscillatorNode['type'][] = ['sine', 'triangle', 'sawtooth', 'square'];

// LFO depth (0..1) → modulation scale per target kind.
const LFO_SCALES = {
  pitchCents: 100,
  cutoffHz: 2000,
  ampGain: 0.4,
  pan: 1,
  fmCents: 200,
} as const;

export function midiToFreq(note: number): number {
  return A4_HZ * Math.pow(2, (note - A4_MIDI) / 12);
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

// Hold a param at its current computed value at time t before new
// automation lands (clickless segment join). Uses
// cancelScheduledValues + setValueAtTime(current) — portable, unlike
// cancelAndHoldAtTime which Safari still lacks.
function holdParam(param: AudioParam, t: number): void {
  param.cancelScheduledValues(t);
  param.setValueAtTime(param.value, t);
}

// Filter cutoff with key tracking: base · (f / C4)^key_track.
function filterBaseCutoff(patch: PatchParams, freq: number): number {
  const track = Math.pow(freq / midiToFreq(60), patch.filter.key_track);
  return clamp(patch.filter.cutoff_hz * track, CUTOFF_MIN, CUTOFF_MAX);
}

// ── Shared per-context assets (cached, quantized keys) ───────────────

interface OscAssets {
  pulseWave(width: number): PeriodicWave;
  wavetableWave(pos: number): PeriodicWave;
  // Float32Array<ArrayBuffer>: WaveShaperNode.curve rejects the
  // ArrayBufferLike supertype under TS ≥5.7's typed-array generics.
  driveCurve(drive: number): Float32Array<ArrayBuffer>;
  noiseBuffer(): AudioBuffer;
  shBuffer(): AudioBuffer;
}

class WebOscAssets implements OscAssets {
  private readonly ctx: BaseAudioContext;
  private readonly pulses = new Map<string, PeriodicWave>();
  private readonly wavetables = new Map<string, PeriodicWave>();
  private readonly curves = new Map<string, Float32Array<ArrayBuffer>>();
  private noise: AudioBuffer | null = null;
  private sh: AudioBuffer | null = null;

  constructor(ctx: BaseAudioContext) {
    this.ctx = ctx;
  }

  pulseWave(width: number): PeriodicWave {
    const key = width.toFixed(2);
    const cached = this.pulses.get(key);
    if (cached) return cached;
    const n = PULSE_HARMONICS;
    const imag = new Float32Array(n + 1);
    // Duty-cycle pulse Fourier series: a_n = sin(n·π·width)/(n·π).
    for (let i = 1; i <= n; i++) {
      imag[i] = Math.sin(i * Math.PI * width) / (i * Math.PI);
    }
    const wave = this.ctx.createPeriodicWave(new Float32Array(n + 1), imag);
    this.pulses.set(key, wave);
    return wave;
  }

  wavetableWave(pos: number): PeriodicWave {
    const key = pos.toFixed(2);
    const cached = this.wavetables.get(key);
    if (cached) return cached;
    const n = WAVETABLE_HARMONICS;
    const imag = new Float32Array(n + 1);
    const bright = clamp(pos, 0, 1);
    for (let i = 1; i <= n; i++) {
      const mellow = 1 / (i * i);
      imag[i] = mellow * (1 - bright) + (1 / i) * bright;
    }
    const wave = this.ctx.createPeriodicWave(new Float32Array(n + 1), imag);
    this.wavetables.set(key, wave);
    return wave;
  }

  driveCurve(drive: number): Float32Array<ArrayBuffer> {
    const key = (clamp(drive, 0, 1) * 20).toFixed(0);
    const cached = this.curves.get(key);
    if (cached) return cached;
    const k = 1 + clamp(drive, 0, 1) * 9;
    const norm = Math.tanh(k);
    // Explicit ArrayBuffer: WaveShaperNode.curve needs
    // Float32Array<ArrayBuffer>, and the length ctor infers ArrayBufferLike.
    const curve = new Float32Array(new ArrayBuffer(DRIVE_CURVE_POINTS * 4));
    for (let i = 0; i < DRIVE_CURVE_POINTS; i++) {
      const x = (i / (DRIVE_CURVE_POINTS - 1)) * 2 - 1;
      curve[i] = Math.tanh(k * x) / norm;
    }
    this.curves.set(key, curve);
    return curve;
  }

  noiseBuffer(): AudioBuffer {
    if (this.noise) return this.noise;
    const sr = this.ctx.sampleRate;
    const buf = this.ctx.createBuffer(1, Math.floor(sr * 2), sr);
    const data = buf.getChannelData(0);
    for (let i = 0; i < data.length; i++) {
      data[i] = Math.random() * 2 - 1;
    }
    this.noise = buf;
    return buf;
  }

  // SampleAndHold lookup table: random steps ~10 ms long, played at
  // rate × playbackRate and looped.
  shBuffer(): AudioBuffer {
    if (this.sh) return this.sh;
    const sr = this.ctx.sampleRate;
    const step = Math.max(1, Math.floor(sr * 0.01));
    const len = step * 128; // ≈1.28 s at 48 kHz
    const buf = this.ctx.createBuffer(1, len, sr);
    const data = buf.getChannelData(0);
    for (let s = 0; s * step < len; s++) {
      const v = Math.random() * 2 - 1;
      for (let i = s * step; i < Math.min(len, (s + 1) * step); i++) {
        data[i] = v;
      }
    }
    this.sh = buf;
    return buf;
  }
}

// ── LFO buses ─────────────────────────────────────────────────────────

// Per-LFO depth gains; voice params connect to the active targets.
interface LfoBus {
  pitch: GainNode;
  cutoff: GainNode;
  amp: GainNode;
  pan: GainNode;
  fm: GainNode;
}

interface LfoRig {
  bus: LfoBus;
  source: AudioScheduledSourceNode | null;
  isSH: boolean;
}

// ── Voice ─────────────────────────────────────────────────────────────

// One rendered oscillator slot inside a voice.
interface OscSlot {
  index: number; // patch.osc index
  kind: number; // OSC_TYPE value
  gain: GainNode;
  pan: StereoPannerNode;
  sources: OscillatorNode[]; // tonal wave / [carrier, modulator] for FM
  modGain: GainNode | null; // FM deviation gain
}

class Voice {
  readonly note: number;
  readonly isEmpty: boolean;
  private readonly ctx: BaseAudioContext;
  private readonly buses: LfoBus[];
  private readonly slots: OscSlot[] = [];
  private noise: AudioBufferSourceNode | null = null;
  private readonly drive: WaveShaperNode;
  private readonly filter: BiquadFilterNode;
  private readonly amp: GainNode;
  private readonly lfoConns: { out: AudioNode; param: AudioParam }[] = [];
  private readonly endSource: AudioScheduledSourceNode | null = null;
  private readonly onEnded: (voice: Voice) => void;
  private freq: number;
  private released = false;
  private cleaned = false;

  constructor(
    ctx: BaseAudioContext,
    note: number,
    velocity: number,
    patch: PatchParams,
    assets: OscAssets,
    buses: LfoBus[],
    dest: AudioNode,
    glideFrom: number | null,
    onEnded: (voice: Voice) => void,
  ) {
    this.ctx = ctx;
    this.note = note;
    this.buses = buses;
    this.onEnded = onEnded;
    const t = ctx.currentTime;
    this.freq = midiToFreq(note);
    const glide = Math.max(0, patch.portamento_s);

    // Per-osc chain: source → gain → pan → bus; bus → drive → filter
    // → amp → dest. Envelopes live on filter.frequency and amp.gain.
    const bus = ctx.createGain();
    this.drive = ctx.createWaveShaper();
    this.drive.oversample = '2x';
    this.drive.curve = assets.driveCurve(patch.filter.drive);
    this.filter = ctx.createBiquadFilter();
    const typeIdx = clamp(Math.round(patch.filter.type), 0, BIQUAD_TYPES.length - 1);
    this.filter.type = BIQUAD_TYPES[typeIdx] ?? 'lowpass';
    this.filter.Q.value = 0.7 + clamp(patch.filter.resonance, 0, 1) * 13;
    this.amp = ctx.createGain();
    this.amp.gain.value = 0;
    bus.connect(this.drive);
    this.drive.connect(this.filter);
    this.filter.connect(this.amp);
    this.amp.connect(dest);

    patch.osc.forEach((o, i) => {
      if (!o.enabled || o.volume <= 0) return;
      const gain = ctx.createGain();
      gain.gain.value = clamp(o.volume, 0, 1);
      const pan = ctx.createStereoPanner();
      pan.pan.value = clamp(o.pan, -1, 1);
      gain.connect(pan);
      pan.connect(bus);
      const slot: OscSlot = { index: i, kind: o.type, gain, pan, sources: [], modGain: null };

      if (o.type === OSC_TYPE.NOISE) {
        const src = ctx.createBufferSource();
        src.buffer = assets.noiseBuffer();
        src.loop = true;
        src.connect(gain);
        src.start(t);
        this.noise = src;
      } else if (o.type === OSC_TYPE.FM) {
        const carrier = ctx.createOscillator();
        carrier.type = 'sine';
        const mod = ctx.createOscillator();
        mod.type = 'sine';
        const modGain = ctx.createGain();
        modGain.gain.value = clamp(o.fm_depth, 0, 1) * this.freq * 2;
        mod.connect(modGain);
        modGain.connect(carrier.frequency);
        const ratio = clamp(o.fm_ratio, 0.5, 16);
        this.setFreq(carrier.frequency, this.freq, t, glide, glideFrom, 1);
        this.setFreq(mod.frequency, this.freq, t, glide, glideFrom, ratio);
        const cents = o.detune_cents + o.semitone_offset * 100;
        carrier.detune.value = cents;
        mod.detune.value = cents;
        carrier.connect(gain);
        carrier.start(t);
        mod.start(t);
        slot.sources = [carrier, mod];
        slot.modGain = modGain;
      } else {
        const osc = ctx.createOscillator();
        applyTonalWave(osc, o.type, o.pulse_width, o.wavetable_pos, assets);
        this.setFreq(osc.frequency, this.freq, t, glide, glideFrom, 1);
        osc.detune.value = o.detune_cents + o.semitone_offset * 100;
        osc.connect(gain);
        osc.start(t);
        slot.sources = [osc];
      }
      this.slots.push(slot);
    });

    this.isEmpty = this.slots.length === 0;
    this.endSource = this.slots[0]?.sources[0] ?? this.noise;
    if (this.endSource) {
      this.endSource.addEventListener('ended', () => this.cleanup());
    }

    if (!this.isEmpty) {
      this.triggerAmp(t, velocity, patch);
      this.triggerFilterEnv(t, patch);
      this.wireLfos(patch);
    } else {
      // Nothing renders — drop the shell nodes immediately.
      this.cleanup();
    }
  }

  // Initial frequency with optional portamento glide from lastFreq.
  // scale = 1 for carriers, fm_ratio for FM modulators.
  private setFreq(
    param: AudioParam,
    freq: number,
    t: number,
    glide: number,
    glideFrom: number | null,
    scale: number,
  ): void {
    if (glideFrom !== null && glide > 0) {
      param.setValueAtTime(glideFrom * scale, t);
      param.linearRampToValueAtTime(freq * scale, t + glide);
    } else {
      param.setValueAtTime(freq * scale, t);
    }
  }

  private triggerAmp(t: number, velocity: number, patch: PatchParams): void {
    const e = patch.amp_env;
    const peak = clamp(velocity, 0, 1);
    const p = this.amp.gain;
    holdParam(p, t);
    if (e.attack_s > 0) {
      p.linearRampToValueAtTime(peak, t + e.attack_s);
    } else {
      p.setValueAtTime(peak, t);
    }
    p.setTargetAtTime(e.sustain * peak, t + Math.max(e.attack_s, 0.001), Math.max(e.decay_s, 0.001) / 3);
  }

  private triggerFilterEnv(t: number, patch: PatchParams): void {
    const e = patch.filter_env;
    const envMod = patch.filter.env_mod;
    const base = filterBaseCutoff(patch, this.freq);
    const peak = clamp(base * (1 + envMod * 3), CUTOFF_MIN, CUTOFF_MAX);
    const sustain = clamp(base * (1 + envMod * e.sustain * 3), CUTOFF_MIN, CUTOFF_MAX);
    const p = this.filter.frequency;
    holdParam(p, t);
    if (e.attack_s > 0) {
      p.linearRampToValueAtTime(peak, t + e.attack_s);
    } else {
      p.setValueAtTime(peak, t);
    }
    p.setTargetAtTime(sustain, t + Math.max(e.attack_s, 0.001), Math.max(e.decay_s, 0.001) / 3);
  }

  // True once release (or steal) has been scheduled. Sources cannot be
  // restarted after stop(), so the manager must build a fresh voice to
  // re-strike a released note.
  get isReleased(): boolean {
    return this.released;
  }

  // Re-strike a held key: glide from lastFreq, re-run envelopes.
  retrigger(velocity: number, patch: PatchParams, glideFrom: number | null): void {
    if (this.isEmpty) return;
    const t = this.ctx.currentTime;
    this.released = false;
    const glide = Math.max(0, patch.portamento_s);
    for (const slot of this.slots) {
      const o = patch.osc[slot.index];
      const ratio = slot.kind === OSC_TYPE.FM && o ? clamp(o.fm_ratio, 0.5, 16) : 1;
      slot.sources.forEach((s, si) => {
        const scale = slot.kind === OSC_TYPE.FM && si === 1 ? ratio : 1;
        holdParam(s.frequency, t);
        if (glideFrom !== null && glide > 0) {
          s.frequency.setValueAtTime(glideFrom * scale, t);
          s.frequency.linearRampToValueAtTime(this.freq * scale, t + glide);
        } else {
          s.frequency.setValueAtTime(this.freq * scale, t);
        }
      });
    }
    this.triggerAmp(t, velocity, patch);
    this.triggerFilterEnv(t, patch);
  }

  // Live-retarget everything the running graph can accept: volumes,
  // pans, detune, FM ratio/depth, filter Q + sustain cutoff, drive
  // curve, and tonal wave (incl. pulse/wavetable morph). Structural
  // changes wait for the next note.
  retargetLive(patch: PatchParams, assets: OscAssets): void {
    if (this.isEmpty) return;
    const t = this.ctx.currentTime;
    for (const slot of this.slots) {
      const o = patch.osc[slot.index];
      if (!o) continue;
      slot.gain.gain.setTargetAtTime(clamp(o.volume, 0, 1), t, 0.02);
      slot.pan.pan.setTargetAtTime(clamp(o.pan, -1, 1), t, 0.02);
      const cents = o.detune_cents + o.semitone_offset * 100;
      for (const s of slot.sources) {
        s.detune.setTargetAtTime(cents, t, 0.02);
      }
      if (slot.kind === OSC_TYPE.FM && slot.sources.length === 2 && slot.modGain) {
        const mod = slot.sources[1];
        const ratio = clamp(o.fm_ratio, 0.5, 16);
        mod.frequency.setTargetAtTime(this.freq * ratio, t, 0.03);
        slot.modGain.gain.setTargetAtTime(clamp(o.fm_depth, 0, 1) * this.freq * 2, t, 0.03);
      } else if (slot.sources.length === 1) {
        applyTonalWave(slot.sources[0], o.type, o.pulse_width, o.wavetable_pos, assets);
      }
    }
    this.drive.curve = assets.driveCurve(patch.filter.drive);
    this.filter.Q.setTargetAtTime(0.7 + clamp(patch.filter.resonance, 0, 1) * 13, t, 0.02);
    // Sustain-region cutoff retarget (running attacks are not re-shaped).
    const base = filterBaseCutoff(patch, this.freq);
    const sustain = clamp(
      base * (1 + patch.filter.env_mod * patch.filter_env.sustain * 3),
      CUTOFF_MIN,
      CUTOFF_MAX,
    );
    this.filter.frequency.setTargetAtTime(sustain, t, 0.03);
  }

  // (Re)connect LFO depth buses to this voice's params per patch
  // targets. Called on construction and whenever LFO targets change.
  wireLfos(patch: PatchParams): void {
    this.unwireLfos();
    patch.lfo.forEach((lfo, i) => {
      const bus = this.buses[i];
      if (!bus) return;
      switch (lfo.target) {
        case LFO_TARGET.PITCH:
          for (const slot of this.slots) {
            for (const s of slot.sources) this.connectLfo(bus.pitch, s.detune);
          }
          break;
        case LFO_TARGET.FILTER_CUTOFF:
          this.connectLfo(bus.cutoff, this.filter.frequency);
          break;
        case LFO_TARGET.AMPLITUDE:
          this.connectLfo(bus.amp, this.amp.gain);
          break;
        case LFO_TARGET.PAN:
          for (const slot of this.slots) this.connectLfo(bus.pan, slot.pan.pan);
          break;
        case LFO_TARGET.FM_RATIO:
          for (const slot of this.slots) {
            const mod = slot.kind === OSC_TYPE.FM ? slot.sources[1] : undefined;
            if (mod) this.connectLfo(bus.fm, mod.detune);
          }
          break;
        default:
          break; // NONE / WAVETABLE_POS: no web equivalent
      }
    });
  }

  private connectLfo(out: AudioNode, param: AudioParam): void {
    out.connect(param);
    this.lfoConns.push({ out, param });
  }

  private unwireLfos(): void {
    for (const c of this.lfoConns) {
      try {
        c.out.disconnect(c.param);
      } catch {
        // already disconnected — nothing to do
      }
    }
    this.lfoConns.length = 0;
  }

  // ADSR release, then stop sources; 'ended' fires cleanup.
  release(t: number, patch: PatchParams): void {
    if (this.released || this.isEmpty) return;
    this.released = true;
    const rel = Math.max(0.01, patch.amp_env.release_s);
    holdParam(this.amp.gain, t);
    this.amp.gain.setTargetAtTime(0, t, rel / 3);
    const stopAt = t + rel * 4 + 0.1; // well past the tail — silent
    for (const slot of this.slots) {
      for (const s of slot.sources) s.stop(stopAt);
    }
    this.noise?.stop(stopAt);
  }

  // Voice stealing: fast fade so the removed voice never clicks.
  forceKill(): void {
    if (this.released || this.isEmpty) return;
    this.released = true;
    const t = this.ctx.currentTime;
    holdParam(this.amp.gain, t);
    this.amp.gain.setTargetAtTime(0, t, 0.015);
    const stopAt = t + 0.08;
    for (const slot of this.slots) {
      for (const s of slot.sources) s.stop(stopAt);
    }
    this.noise?.stop(stopAt);
  }

  private cleanup(): void {
    if (this.cleaned) return;
    this.cleaned = true;
    this.unwireLfos();
    for (const slot of this.slots) {
      slot.gain.disconnect();
      slot.pan.disconnect();
    }
    this.noise?.disconnect();
    this.drive.disconnect();
    this.filter.disconnect();
    this.amp.disconnect();
    this.onEnded(this);
  }
}

// Route a tonal osc type onto an OscillatorNode (native type or a
// cached PeriodicWave for pulse / wavetable morph).
function applyTonalWave(
  osc: OscillatorNode,
  type: number,
  pulseWidth: number,
  wtPos: number,
  assets: OscAssets,
): void {
  if (type === OSC_TYPE.PULSE) {
    osc.setPeriodicWave(assets.pulseWave(clamp(pulseWidth, 0.01, 0.99)));
  } else if (type === OSC_TYPE.WAVETABLE) {
    osc.setPeriodicWave(assets.wavetableWave(clamp(wtPos, 0, 1)));
  } else {
    osc.type = NATIVE_WAVES[clamp(Math.round(type), 0, NATIVE_WAVES.length - 1)] ?? 'sine';
  }
}

// ── VoiceManager ──────────────────────────────────────────────────────

export class VoiceManager {
  readonly output: GainNode;
  private readonly ctx: BaseAudioContext;
  private readonly getPatch: () => PatchParams;
  private readonly assets: WebOscAssets;
  private readonly voices = new Map<number, Voice>(); // insertion order = age
  private rigs: LfoRig[] = [];
  private lastFreq: number | null = null;
  private disposed = false;

  constructor(ctx: BaseAudioContext, getPatch: () => PatchParams) {
    this.ctx = ctx;
    this.getPatch = getPatch;
    this.assets = new WebOscAssets(ctx);
    this.output = ctx.createGain();
    this.rebuildLfos(this.getPatch());
  }

  noteOn(note: number, velocity: number): void {
    if (this.disposed || !Number.isFinite(note) || !Number.isFinite(velocity)) return;
    const patch = this.getPatch();
    const cap = clamp(Math.round(patch.voice_count), VOICE_COUNT_MIN, VOICE_COUNT_MAX);
    // Steal oldest voices (Map preserves insertion order) beyond cap.
    while (this.voices.size >= cap) {
      const oldest = this.voices.keys().next();
      if (oldest.done) break;
      const victim = this.voices.get(oldest.value);
      this.voices.delete(oldest.value);
      victim?.forceKill();
    }
    const existing = this.voices.get(note);
    let voice: Voice;
    if (existing && !existing.isReleased) {
      this.voices.delete(note);
      existing.retrigger(velocity, patch, this.lastFreq);
      voice = existing;
    } else {
      // Releasing voices cannot restart (stop() is one-way) — replace.
      existing?.forceKill();
      this.voices.delete(note);
      voice = new Voice(this.ctx, note, velocity, patch, this.assets, this.rigBuses(), this.output, this.lastFreq, (v) => {
        const current = this.voices.get(v.note);
        if (current === v) this.voices.delete(v.note);
      });
    }
    if (!voice.isEmpty) this.voices.set(note, voice);
    this.lastFreq = midiToFreq(note);
  }

  noteOff(note: number): void {
    if (this.disposed) return;
    const voice = this.voices.get(note);
    if (voice) voice.release(this.ctx.currentTime, this.getPatch());
  }

  // Re-derive LFO rigs and retarget all live voices from the patch.
  setPatch(): void {
    if (this.disposed) return;
    const patch = this.getPatch();
    this.rebuildLfos(patch);
    for (const voice of this.voices.values()) {
      voice.retargetLive(patch, this.assets);
      voice.wireLfos(patch);
    }
  }

  private rigBuses(): LfoBus[] {
    return this.rigs.map((r) => r.bus);
  }

  // Create/update LFO sources + depth gains from patch.lfo[i].
  private rebuildLfos(patch: PatchParams): void {
    const t = this.ctx.currentTime;
    while (this.rigs.length < patch.lfo.length) {
      const mk = (): GainNode => this.ctx.createGain();
      this.rigs.push({
        bus: { pitch: mk(), cutoff: mk(), amp: mk(), pan: mk(), fm: mk() },
        source: null,
        isSH: false,
      });
    }
    patch.lfo.forEach((lfo, i) => {
      const rig = this.rigs[i];
      if (!rig) return;
      const wantSH = lfo.waveform === LFO_WAVEFORM.SAMPLE_AND_HOLD;
      if (!rig.source || rig.isSH !== wantSH) {
        if (rig.source) {
          try {
            rig.source.stop();
          } catch {
            // never started — nothing to stop
          }
          rig.source.disconnect();
        }
        if (wantSH) {
          const src = this.ctx.createBufferSource();
          src.buffer = this.assets.shBuffer();
          src.loop = true;
          src.playbackRate.value = clamp(lfo.rate_hz, 0.01, 20);
          src.start(t);
          rig.source = src;
        } else {
          const osc = this.ctx.createOscillator();
          osc.type = NATIVE_WAVES[clamp(Math.round(lfo.waveform), 0, NATIVE_WAVES.length - 1)] ?? 'sine';
          osc.frequency.value = clamp(lfo.rate_hz, 0.01, 20);
          osc.start(t);
          rig.source = osc;
        }
        rig.isSH = wantSH;
      } else if (wantSH) {
        const src = rig.source as AudioBufferSourceNode;
        src.playbackRate.setTargetAtTime(clamp(lfo.rate_hz, 0.01, 20), t, 0.05);
      } else {
        const osc = rig.source as OscillatorNode;
        osc.frequency.setTargetAtTime(clamp(lfo.rate_hz, 0.01, 20), t, 0.05);
      }
      // Depth per target; inactive targets get 0 so rewiring is safe.
      const d = clamp(lfo.depth, 0, 1);
      const on = lfo.target;
      rig.bus.pitch.gain.setTargetAtTime(on === LFO_TARGET.PITCH ? d * LFO_SCALES.pitchCents : 0, t, 0.05);
      rig.bus.cutoff.gain.setTargetAtTime(on === LFO_TARGET.FILTER_CUTOFF ? d * LFO_SCALES.cutoffHz : 0, t, 0.05);
      rig.bus.amp.gain.setTargetAtTime(on === LFO_TARGET.AMPLITUDE ? d * LFO_SCALES.ampGain : 0, t, 0.05);
      rig.bus.pan.gain.setTargetAtTime(on === LFO_TARGET.PAN ? d * LFO_SCALES.pan : 0, t, 0.05);
      rig.bus.fm.gain.setTargetAtTime(on === LFO_TARGET.FM_RATIO ? d * LFO_SCALES.fmCents : 0, t, 0.05);
    });
  }

  dispose(): void {
    this.disposed = true;
    for (const voice of this.voices.values()) voice.forceKill();
    this.voices.clear();
    for (const rig of this.rigs) {
      if (rig.source) {
        try {
          rig.source.stop();
        } catch {
          // never started — nothing to stop
        }
        rig.source.disconnect();
      }
      rig.bus.pitch.disconnect();
      rig.bus.cutoff.disconnect();
      rig.bus.amp.disconnect();
      rig.bus.pan.disconnect();
      rig.bus.fm.disconnect();
    }
    this.rigs = [];
    this.output.disconnect();
  }
}
