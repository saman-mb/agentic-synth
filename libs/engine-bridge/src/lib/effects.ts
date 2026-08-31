// ── Effect rack for the WebAudio patch renderer (issue #280) ─────────
//
// Design intent: parallel-send rack (dry + reverb + delay) matching the
// PatchParams reverb/delay blocks with rendering that is approximate
// but intentional. The dry path is always unity — reverb.mix and
// delay.mix scale their wet sends only, like the C++ engine's send
// buses, so mix = 0 is bit-clean bypass.
//
// Approximations:
//  - Reverb: ConvolverNode with a synthesized impulse — exponentially
//    decaying stereo noise. damping darkens the tail through a one-pole
//    lowpass whose coefficient closes over time; width cross-mixes the
//    impulse channels (0 = mono, 1 = fully decorrelated). The impulse
//    is rebuilt only when size/damping/width move by > 0.01 so knob
//    drags do not thrash allocations. Rebuilds are debounced off the
//    click/knob task and cached by quantized key — the loaded impulse
//    keeps sounding until the fresh buffer swaps in, so a preset switch
//    never blocks the main thread (only the tail character may lag;
//    mix/gain changes stay immediate).
//  - Delay: two DelayNodes. stereo = 0 → two parallel independent
//    lines; stereo = 1 → classic ping-pong (L→R→L cross-feedback); in
//    between the feedback is split proportionally. Outputs merged hard
//    L/R. Feedback is scaled by 0.95 as a stability guard.
//  - bpm_sync: the standalone web build has no host transport, so a
//    synced delay snaps to a 120 BPM quarter note (0.5 s).

import type { DelayParams, ReverbParams } from '@agentic-synth/shared-types';

export interface EffectRack {
  readonly input: GainNode;
  readonly output: GainNode;
  setReverb(p: ReverbParams): void;
  setDelay(p: DelayParams): void;
  dispose(): void;
}

const IMPULSE_REBUILD_EPS = 0.01;
const FEEDBACK_GUARD = 0.95;
// Impulse builds are debounced past the input event so a preset switch
// (which fans out several reverb params in one tick) coalesces into a
// single build off the click path. The cache is bounded: a 3.5 s stereo
// impulse is >1 MB, and a knob drag would otherwise pin dozens.
const IMPULSE_BUILD_DEBOUNCE_MS = 50;
const IMPULSE_CACHE_MAX = 6;

function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

// Synthesized decaying-noise impulse response.
function buildImpulse(ctx: BaseAudioContext, size: number, damping: number, width: number): AudioBuffer {
  const sr = ctx.sampleRate;
  const dur = 0.25 + size * 3.25; // 0.25 .. 3.5 s
  const len = Math.max(1, Math.floor(sr * dur));
  const buf = ctx.createBuffer(2, len, sr);
  const baseCoef = 0.02 + (1 - damping) * 0.55; // tail brightness
  for (let ch = 0; ch < 2; ch++) {
    const data = buf.getChannelData(ch);
    let lp = 0;
    for (let i = 0; i < len; i++) {
      const t = i / len;
      // One-pole lowpass that darkens as the tail decays.
      lp += baseCoef * (1 - 0.7 * t) * (Math.random() * 2 - 1 - lp);
      const env = Math.pow(1 - t, 1.8 + damping * 1.5);
      data[i] = lp * env;
    }
  }
  // Width: cross-mix so 0 → identical channels, 1 → decorrelated.
  if (width < 0.99) {
    const l = buf.getChannelData(0);
    const r = buf.getChannelData(1);
    const a = 0.5 + width / 2;
    const b = 0.5 - width / 2;
    for (let i = 0; i < len; i++) {
      const lm = l[i] * a + r[i] * b;
      const rm = r[i] * a + l[i] * b;
      l[i] = lm;
      r[i] = rm;
    }
  }
  return buf;
}

export function createEffectRack(ctx: BaseAudioContext): EffectRack {
  const input = ctx.createGain();
  const output = ctx.createGain();
  input.connect(output); // dry, unity

  // ── Reverb send ──────────────────────────────────────────────────
  const reverbSend = ctx.createGain();
  reverbSend.gain.value = 0;
  const convolver = ctx.createConvolver();
  reverbSend.connect(convolver);
  convolver.connect(output);
  let impulseKey = '';
  // Quantized-key -> built buffer. Insertion-ordered; oldest entry is
  // evicted once IMPULSE_CACHE_MAX is exceeded.
  const impulseCache = new Map<string, AudioBuffer>();
  let pendingBuild: { key: string; size: number; damping: number; width: number } | null = null;
  let buildTimer: ReturnType<typeof setTimeout> | null = null;

  // Runs IMPULSE_BUILD_DEBOUNCE_MS after the last setReverb() that asked
  // for a new key. Builds the impulse, caches it, and swaps it into the
  // convolver only if it is still the wanted key. Assigning
  // ConvolverNode.buffer is an atomic graph change — the playing tail is
  // replaced without a click.
  const flushImpulseBuild = (): void => {
    buildTimer = null;
    if (!pendingBuild) return;
    const req = pendingBuild;
    pendingBuild = null;
    const buf = buildImpulse(ctx, req.size, req.damping, req.width);
    impulseCache.set(req.key, buf);
    while (impulseCache.size > IMPULSE_CACHE_MAX) {
      const oldest = impulseCache.keys().next();
      if (oldest.done) break;
      impulseCache.delete(oldest.value);
    }
    if (impulseKey === req.key) convolver.buffer = buf;
  };

  // ── Delay network ────────────────────────────────────────────────
  // dl/dr = left/right delay lines; xLR/xRL = cross-feedback
  // (ping-pong); sL/sR = self-feedback (parallel); drDry feeds the
  // right line directly so stereo = 0 gives two parallel lines.
  const delaySend = ctx.createGain();
  delaySend.gain.value = 0;
  const dl = ctx.createDelay(2.0);
  const dr = ctx.createDelay(2.0);
  const drDry = ctx.createGain();
  const xLR = ctx.createGain();
  const xRL = ctx.createGain();
  const sL = ctx.createGain();
  const sR = ctx.createGain();
  const merge = ctx.createChannelMerger(2);
  const delayWet = ctx.createGain();
  delaySend.connect(dl);
  delaySend.connect(drDry);
  drDry.connect(dr);
  dl.connect(xLR);
  xLR.connect(dr);
  dr.connect(xRL);
  xRL.connect(dl);
  dl.connect(sL);
  sL.connect(dl);
  dr.connect(sR);
  sR.connect(dr);
  dl.connect(merge, 0, 0);
  dr.connect(merge, 0, 1);
  merge.connect(delayWet);
  delayWet.connect(output);

  return {
    input,
    output,
    setReverb(p: ReverbParams): void {
      const t = ctx.currentTime;
      reverbSend.gain.setTargetAtTime(clamp(p.mix, 0, 1), t, 0.02);
      // Quantize to the rebuild threshold so sub-0.01 knob jitters
      // do not regenerate the impulse.
      const q = (v: number): number => Math.round(v / IMPULSE_REBUILD_EPS);
      const key = `${q(p.size)}|${q(p.damping)}|${q(p.width)}`;
      if (key === impulseKey) return;
      impulseKey = key;
      const cached = impulseCache.get(key);
      if (cached) {
        convolver.buffer = cached;
        return;
      }
      // Defer the build off the click/knob task; the current impulse
      // keeps sounding until the rebuilt buffer is swapped in. The tail
      // character may lag by one debounce window — the documented cost
      // of never blocking the main thread while audio plays.
      pendingBuild = { key, size: p.size, damping: p.damping, width: p.width };
      if (buildTimer !== null) clearTimeout(buildTimer);
      buildTimer = setTimeout(flushImpulseBuild, IMPULSE_BUILD_DEBOUNCE_MS);
    },
    setDelay(p: DelayParams): void {
      const t = ctx.currentTime;
      const time = p.bpm_sync ? 0.5 : clamp(p.time_s, 0.001, 2);
      dl.delayTime.setTargetAtTime(time, t, 0.05);
      dr.delayTime.setTargetAtTime(time, t, 0.05);
      const stereo = clamp(p.stereo, 0, 1);
      const fb = clamp(p.feedback, 0, 0.99) * FEEDBACK_GUARD;
      xLR.gain.setTargetAtTime(fb * stereo, t, 0.02);
      xRL.gain.setTargetAtTime(fb * stereo, t, 0.02);
      sL.gain.setTargetAtTime(fb * (1 - stereo), t, 0.02);
      sR.gain.setTargetAtTime(fb * (1 - stereo), t, 0.02);
      drDry.gain.setTargetAtTime(1 - stereo, t, 0.02);
      delaySend.gain.setTargetAtTime(clamp(p.mix, 0, 1), t, 0.02);
    },
    dispose(): void {
      if (buildTimer !== null) clearTimeout(buildTimer);
      buildTimer = null;
      pendingBuild = null;
      impulseCache.clear();
      input.disconnect();
      reverbSend.disconnect();
      convolver.disconnect();
      delaySend.disconnect();
      dl.disconnect();
      dr.disconnect();
      drDry.disconnect();
      xLR.disconnect();
      xRL.disconnect();
      sL.disconnect();
      sR.disconnect();
      merge.disconnect();
      delayWet.disconnect();
      output.disconnect();
    },
  };
}
