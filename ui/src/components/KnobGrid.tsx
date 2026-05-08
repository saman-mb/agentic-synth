import React, { useCallback } from 'react';
import { Knob } from './Knob';

export interface OscParams {
  volume: number;
  detune_cents: number;
  semitone_offset: number;
  fm_ratio: number;
  fm_depth: number;
  wavetable_pos: number;
  pulse_width: number;
  pan: number;
}

export interface FilterParams {
  cutoff_hz: number;
  resonance: number;
  env_mod: number;
  drive: number;
}

export interface EnvParams {
  attack_s: number;
  decay_s: number;
  sustain: number;
  release_s: number;
}

export interface LfoParams {
  rate_hz: number;
  depth: number;
  phase_offset: number;
}

export interface ReverbParams {
  size: number;
  damping: number;
  width: number;
  mix: number;
}

export interface DelayParams {
  time_s: number;
  feedback: number;
  mix: number;
}

export interface PatchParams {
  osc: OscParams[];
  filter: FilterParams;
  filter_env: EnvParams;
  amp_env: EnvParams;
  lfo: LfoParams[];
  reverb: ReverbParams;
  delay: DelayParams;
  master_gain: number;
  portamento_s: number;
}

export function makeDefaultPatch(): PatchParams {
  const osc: OscParams = {
    volume: 1, detune_cents: 0, semitone_offset: 0,
    fm_ratio: 1, fm_depth: 0, wavetable_pos: 0, pulse_width: 0.5, pan: 0,
  };
  return {
    osc: [{ ...osc }, { ...osc, volume: 0 }, { ...osc, volume: 0 }],
    filter: { cutoff_hz: 18000, resonance: 0, env_mod: 0, drive: 0 },
    filter_env: { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 },
    amp_env: { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 },
    lfo: [
      { rate_hz: 1, depth: 0, phase_offset: 0 },
      { rate_hz: 1, depth: 0, phase_offset: 0 },
    ],
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0 },
    master_gain: 1,
    portamento_s: 0,
  };
}

interface KnobGridProps {
  patch: PatchParams;
  agentKeys: Set<string>;
  onKnobChange: (param: string, value: number) => void;
}

export function KnobGrid({ patch, agentKeys, onKnobChange }: KnobGridProps) {
  const k = useCallback(
    (param: string, value: number, min: number, max: number, label: string, unit?: string, decimals?: number) => (
      <Knob
        key={param}
        value={value}
        min={min}
        max={max}
        label={label}
        unit={unit}
        decimals={decimals}
        onChange={(v) => onKnobChange(param, v)}
        agentDriven={agentKeys.has(param)}
      />
    ),
    [agentKeys, onKnobChange],
  );

  return (
    <div className="knob-grid">
      {patch.osc.map((osc, i) => (
        <section key={`osc${i}`} className="knob-section">
          <h3>Osc {i + 1}</h3>
          <div className="knob-row">
            {k(`osc.${i}.volume`, osc.volume, 0, 1, 'Vol')}
            {k(`osc.${i}.detune_cents`, osc.detune_cents, -100, 100, 'Detune', 'c', 0)}
            {k(`osc.${i}.semitone_offset`, osc.semitone_offset, -48, 48, 'Semi', 'st', 0)}
            {k(`osc.${i}.pan`, osc.pan, -1, 1, 'Pan')}
            {k(`osc.${i}.pulse_width`, osc.pulse_width, 0.01, 0.99, 'PW')}
            {k(`osc.${i}.fm_ratio`, osc.fm_ratio, 0.5, 16, 'FM Rt', '', 1)}
            {k(`osc.${i}.fm_depth`, osc.fm_depth, 0, 1, 'FM Dep')}
          </div>
        </section>
      ))}

      <section className="knob-section">
        <h3>Filter</h3>
        <div className="knob-row">
          {k('filter.cutoff_hz', patch.filter.cutoff_hz, 20, 20000, 'Cutoff', 'hz', 0)}
          {k('filter.resonance', patch.filter.resonance, 0, 1, 'Reso')}
          {k('filter.env_mod', patch.filter.env_mod, -1, 1, 'EnvMod')}
          {k('filter.drive', patch.filter.drive, 0, 1, 'Drive')}
        </div>
      </section>

      <section className="knob-section">
        <h3>Amp Env</h3>
        <div className="knob-row">
          {k('amp_env.attack_s', patch.amp_env.attack_s, 0, 10, 'Atk', 's', 3)}
          {k('amp_env.decay_s', patch.amp_env.decay_s, 0, 10, 'Dec', 's', 3)}
          {k('amp_env.sustain', patch.amp_env.sustain, 0, 1, 'Sus')}
          {k('amp_env.release_s', patch.amp_env.release_s, 0, 20, 'Rel', 's', 3)}
        </div>
      </section>

      <section className="knob-section">
        <h3>Filter Env</h3>
        <div className="knob-row">
          {k('filter_env.attack_s', patch.filter_env.attack_s, 0, 10, 'Atk', 's', 3)}
          {k('filter_env.decay_s', patch.filter_env.decay_s, 0, 10, 'Dec', 's', 3)}
          {k('filter_env.sustain', patch.filter_env.sustain, 0, 1, 'Sus')}
          {k('filter_env.release_s', patch.filter_env.release_s, 0, 20, 'Rel', 's', 3)}
        </div>
      </section>

      {patch.lfo.map((lfo, i) => (
        <section key={`lfo${i}`} className="knob-section">
          <h3>LFO {i + 1}</h3>
          <div className="knob-row">
            {k(`lfo.${i}.rate_hz`, lfo.rate_hz, 0.01, 20, 'Rate', 'hz', 2)}
            {k(`lfo.${i}.depth`, lfo.depth, 0, 1, 'Depth')}
            {k(`lfo.${i}.phase_offset`, lfo.phase_offset, 0, 1, 'Phase')}
          </div>
        </section>
      ))}

      <section className="knob-section">
        <h3>Reverb</h3>
        <div className="knob-row">
          {k('reverb.size', patch.reverb.size, 0, 1, 'Size')}
          {k('reverb.damping', patch.reverb.damping, 0, 1, 'Damp')}
          {k('reverb.width', patch.reverb.width, 0, 1, 'Width')}
          {k('reverb.mix', patch.reverb.mix, 0, 1, 'Mix')}
        </div>
      </section>

      <section className="knob-section">
        <h3>Delay</h3>
        <div className="knob-row">
          {k('delay.time_s', patch.delay.time_s, 0, 2, 'Time', 's', 3)}
          {k('delay.feedback', patch.delay.feedback, 0, 0.99, 'Feedback')}
          {k('delay.mix', patch.delay.mix, 0, 1, 'Mix')}
        </div>
      </section>

      <section className="knob-section">
        <h3>Global</h3>
        <div className="knob-row">
          {k('master_gain', patch.master_gain, 0, 1, 'Gain')}
          {k('portamento_s', patch.portamento_s, 0, 2, 'Glide', 's', 3)}
        </div>
      </section>
    </div>
  );
}
