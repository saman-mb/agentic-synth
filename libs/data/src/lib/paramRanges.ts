// Per-param numeric ranges. Mirrors the (min,max) tuples wired into
// the knob grid (see KnobGrid.tsx). Used by the Phase 13 macro
// projection path to (a) scale a normalized 0..1 macro into a delta
// in the destination's natural units and (b) clamp the resulting
// effective value to a sane range before pushing to the engine.
//
// Keep this in sync with the (min,max) literals in ModulesGrid/KnobGrid.
// If you add a destination in DESTINATION_CATALOG below, add its range
// here too — otherwise the macro projection silently falls back to
// the identity range [0,1] and clamping won't be meaningful.
export interface ParamRange { min: number; max: number; }

export const PARAM_RANGES: Record<string, ParamRange> = (() => {
  const r: Record<string, ParamRange> = {};
  for (const i of [0, 1, 2]) {
    r[`osc.${i}.volume`]          = { min: 0, max: 1 };
    r[`osc.${i}.detune_cents`]    = { min: -100, max: 100 };
    r[`osc.${i}.semitone_offset`] = { min: -48, max: 48 };
    r[`osc.${i}.wavetable_pos`]   = { min: 0, max: 1 };
    r[`osc.${i}.pan`]             = { min: -1, max: 1 };
    r[`osc.${i}.pulse_width`]     = { min: 0.01, max: 0.99 };
    r[`osc.${i}.fm_ratio`]        = { min: 0.5, max: 16 };
    r[`osc.${i}.fm_depth`]        = { min: 0, max: 1 };
  }
  r['filter.cutoff_hz'] = { min: 20, max: 20000 };
  r['filter.resonance'] = { min: 0, max: 1 };
  r['filter.env_mod']   = { min: -1, max: 1 };
  r['filter.key_track'] = { min: 0, max: 1 };
  r['filter.drive']     = { min: 0, max: 1 };
  for (const env of ['amp_env', 'filter_env']) {
    r[`${env}.attack_s`]  = { min: 0, max: 10 };
    r[`${env}.decay_s`]   = { min: 0, max: 10 };
    r[`${env}.sustain`]   = { min: 0, max: 1 };
    r[`${env}.release_s`] = { min: 0, max: 20 };
  }
  for (const i of [0, 1]) {
    r[`lfo.${i}.rate_hz`]      = { min: 0.01, max: 20 };
    r[`lfo.${i}.depth`]        = { min: 0, max: 1 };
    r[`lfo.${i}.phase_offset`] = { min: 0, max: 1 };
  }
  r['reverb.size']    = { min: 0, max: 1 };
  r['reverb.damping'] = { min: 0, max: 1 };
  r['reverb.width']   = { min: 0, max: 1 };
  r['reverb.mix']     = { min: 0, max: 1 };
  r['delay.time_s']   = { min: 0, max: 2 };
  r['delay.feedback'] = { min: 0, max: 0.99 };
  r['delay.stereo']   = { min: 0, max: 1 };
  r['delay.mix']      = { min: 0, max: 1 };
  r['master_gain']    = { min: 0, max: 1 };
  r['portamento_s']   = { min: 0, max: 2 };
  return r;
})();
