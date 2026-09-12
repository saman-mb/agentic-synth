import type { DelayParams, EnvParams, FilterParams, LfoParams, OscParams, PatchParams, ReverbParams } from '@agentic-synth/shared-types';

const OSC_COUNT = 3;
const LFO_COUNT = 2;
const OSC_SIZE = 40;
const FILTER_SIZE = 24;
const ENV_SIZE = 16;
const LFO_SIZE = 20;
const REVERB_SIZE = 16;
const DELAY_SIZE = 20;
const CHORUS_SIZE = 20;
const TUBESAT_SIZE = 16;
const RATIONALE_SIZE = 256;
const AUGMENTER_SIZE = 256;

const OFF_VERSION = 0;
const OFF_PATCH_ID = 4;
const OFF_OSC = 8;
const OFF_FILTER = OFF_OSC + OSC_SIZE * OSC_COUNT;
const OFF_FILTER_ENV = OFF_FILTER + FILTER_SIZE;
const OFF_AMP_ENV = OFF_FILTER_ENV + ENV_SIZE;
const OFF_LFO = OFF_AMP_ENV + ENV_SIZE;
const OFF_REVERB = OFF_LFO + LFO_SIZE * LFO_COUNT;
const OFF_DELAY = OFF_REVERB + REVERB_SIZE;
const OFF_CHORUS = OFF_DELAY + DELAY_SIZE;
const OFF_TUBESAT = OFF_CHORUS + CHORUS_SIZE;
const OFF_REVERB_SEND_HPF = OFF_TUBESAT + TUBESAT_SIZE;
const OFF_MASTER_GAIN = OFF_REVERB_SEND_HPF + 4 + 4;
const OFF_PORTAMENTO = OFF_MASTER_GAIN + 4;
const OFF_VOICE_COUNT = OFF_PORTAMENTO + 4;
const OFF_RATIONALE = OFF_VOICE_COUNT + 1 + 3;
const OFF_AUGMENTER = OFF_RATIONALE + RATIONALE_SIZE;

export const PATCH_STRUCT_SIZE = OFF_AUGMENTER + AUGMENTER_SIZE;

const PATCH_STRUCT_CANARY = 828;
if (PATCH_STRUCT_SIZE !== PATCH_STRUCT_CANARY) {
  throw new Error(`PatchStruct packer size ${PATCH_STRUCT_SIZE} !== ${PATCH_STRUCT_CANARY}`);
}

const LE = true as const;
const PATCH_VERSION = 1;
const CHORUS_RATE_HZ = 0.4;
const CHORUS_DEPTH = 0.35;
const CHORUS_MIX = 0;
const TUBESAT_DRIVE = 0;
const TUBESAT_MIX = 1;
const REVERB_SEND_HPF_HZ = 0;

function u8(v: number): number {
  return v & 0xff;
}

function packOsc(view: DataView, offset: number, osc: OscParams | undefined): void {
  if (!osc) return;
  view.setUint8(offset + 0, u8(osc.type));
  view.setFloat32(offset + 4, osc.semitone_offset, LE);
  view.setFloat32(offset + 8, osc.detune_cents, LE);
  view.setFloat32(offset + 12, osc.wavetable_pos, LE);
  view.setFloat32(offset + 16, osc.fm_ratio, LE);
  view.setFloat32(offset + 20, osc.fm_depth, LE);
  view.setFloat32(offset + 24, osc.volume, LE);
  view.setFloat32(offset + 28, osc.pan, LE);
  view.setFloat32(offset + 32, osc.pulse_width, LE);
  view.setUint8(offset + 36, u8(osc.enabled));
}

function packFilter(view: DataView, offset: number, filter: FilterParams): void {
  view.setUint8(offset + 0, u8(filter.type));
  view.setFloat32(offset + 4, filter.cutoff_hz, LE);
  view.setFloat32(offset + 8, filter.resonance, LE);
  view.setFloat32(offset + 12, filter.env_mod, LE);
  view.setFloat32(offset + 16, filter.key_track, LE);
  view.setFloat32(offset + 20, filter.drive, LE);
}

function packEnv(view: DataView, offset: number, env: EnvParams): void {
  view.setFloat32(offset + 0, env.attack_s, LE);
  view.setFloat32(offset + 4, env.decay_s, LE);
  view.setFloat32(offset + 8, env.sustain, LE);
  view.setFloat32(offset + 12, env.release_s, LE);
}

function packLfo(view: DataView, offset: number, lfo: LfoParams | undefined): void {
  if (!lfo) return;
  view.setUint8(offset + 0, u8(lfo.waveform));
  view.setUint8(offset + 1, u8(lfo.target));
  view.setFloat32(offset + 4, lfo.rate_hz, LE);
  view.setFloat32(offset + 8, lfo.depth, LE);
  view.setFloat32(offset + 12, lfo.phase_offset, LE);
  view.setUint8(offset + 16, u8(lfo.bpm_sync));
}

function packReverb(view: DataView, offset: number, reverb: ReverbParams): void {
  view.setFloat32(offset + 0, reverb.size, LE);
  view.setFloat32(offset + 4, reverb.damping, LE);
  view.setFloat32(offset + 8, reverb.width, LE);
  view.setFloat32(offset + 12, reverb.mix, LE);
}

function packDelay(view: DataView, offset: number, delay: DelayParams): void {
  view.setFloat32(offset + 0, delay.time_s, LE);
  view.setFloat32(offset + 4, delay.feedback, LE);
  view.setFloat32(offset + 8, delay.mix, LE);
  view.setFloat32(offset + 12, delay.stereo, LE);
  view.setUint8(offset + 16, u8(delay.bpm_sync));
}

export function packPatchParams(patch: PatchParams): ArrayBuffer {
  const buf = new ArrayBuffer(PATCH_STRUCT_SIZE);
  const view = new DataView(buf);

  view.setUint32(OFF_VERSION, PATCH_VERSION, LE);
  view.setUint32(OFF_PATCH_ID, 0, LE);

  for (let i = 0; i < OSC_COUNT; i++) {
    packOsc(view, OFF_OSC + i * OSC_SIZE, patch.osc[i]);
  }

  packFilter(view, OFF_FILTER, patch.filter);
  packEnv(view, OFF_FILTER_ENV, patch.filter_env);
  packEnv(view, OFF_AMP_ENV, patch.amp_env);

  for (let i = 0; i < LFO_COUNT; i++) {
    packLfo(view, OFF_LFO + i * LFO_SIZE, patch.lfo[i]);
  }

  packReverb(view, OFF_REVERB, patch.reverb);
  packDelay(view, OFF_DELAY, patch.delay);

  view.setFloat32(OFF_CHORUS + 0, CHORUS_RATE_HZ, LE);
  view.setFloat32(OFF_CHORUS + 4, CHORUS_DEPTH, LE);
  view.setFloat32(OFF_CHORUS + 8, CHORUS_MIX, LE);

  view.setFloat32(OFF_TUBESAT + 0, TUBESAT_DRIVE, LE);
  view.setFloat32(OFF_TUBESAT + 4, TUBESAT_MIX, LE);

  view.setFloat32(OFF_REVERB_SEND_HPF, REVERB_SEND_HPF_HZ, LE);
  view.setFloat32(OFF_MASTER_GAIN, patch.master_gain, LE);
  view.setFloat32(OFF_PORTAMENTO, patch.portamento_s, LE);
  view.setUint8(OFF_VOICE_COUNT, u8(patch.voice_count));

  return buf;
}
