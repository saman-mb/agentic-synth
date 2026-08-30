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
//    drags do not thrash allocations.
//  - Delay: two DelayNodes. stereo = 0 → two parallel independent
//    lines; stereo = 1 → classic ping-pong (L→R→L cross-feedback); in
//    between the feedback is split proportionally. Outputs merged hard
//    L/R. Feedback is scaled by 0.95 as a stability guard.
//  - bpm_sync: the standalone web build has no host transport, so a
//    synced delay snaps to a 120 BPM quarter note (0.5 s).

import type { DelayParams, ReverbParams } from '../components/KnobGrid';

export interface EffectRack {
  readonly input: GainNode;
  readonly output: GainNode;
  setReverb(p: ReverbParams): void;
  setDelay(p: DelayParams): void;
  dispose(): void;
}

const IMPULSE_REBUILD_EPS = 0.01;
const FEEDBACK_GUARD = 0.95;

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
      if (key !== impulseKey) {
        impulseKey = key;
        convolver.buffer = buildImpulse(ctx, p.size, p.damping, p.width);
      }
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
