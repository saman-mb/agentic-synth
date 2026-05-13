import React, { useCallback } from 'react';
import { Knob } from './Knob';
import type { PatchParams } from './KnobGrid';
import './ModulesGrid.css';

// ── ModulesGrid (Phase 4) ────────────────────────────────────────────
//
// Replaces the legacy accordion-style KnobGrid with an always-visible
// 4-row CSS grid laid out per REBRAND.md §5:
//   row 1: OSC 1 | OSC 2 | OSC 3 | (Visualizer lives in RightColumn)
//   row 2: Filter (spans 2)  | Amp Env (spans 2)
//   row 3: Env 2 | LFO 1 | LFO 2
//   row 4: Reverb (spans 2)  | Delay (spans 2)
//
// All 70 params remain accessible — only the visual grouping changed.
// The expand/collapse machinery in KnobGrid is gone: sound designers
// don't scroll. State stays owned by App.tsx via patch + onKnobChange.

interface ModulesGridProps {
  patch: PatchParams;
  agentKeys: Set<string>;
  onKnobChange: (param: string, value: number) => void;
}

interface KnobSpec {
  param: string;
  value: number;
  min: number;
  max: number;
  label: string;
  unit?: string;
  decimals?: number;
}

export function ModulesGrid({ patch, agentKeys, onKnobChange }: ModulesGridProps) {
  const renderKnob = useCallback(
    (spec: KnobSpec) => {
      const { param, value, min, max, label, unit, decimals } = spec;
      const edited = agentKeys.has(param);
      const range = max - min;
      const norm = range === 0 ? 0 : Math.max(0, Math.min(1, (value - min) / range));
      const d = decimals ?? 2;
      const display = unit ? `${value.toFixed(d)}${unit}` : value.toFixed(d);
      const bipolar = min < 0 && max > 0;
      const defaultNorm = bipolar ? 0.5 : 0;
      return (
        <div key={param} className={`knob-slot${edited ? ' knob-slot-agent' : ''}`}>
          {edited && (
            <span className="knob-agent-badge" aria-label="Edited by agent" title="Edited by agent">AI</span>
          )}
          <Knob
            value={norm}
            name={label}
            displayValue={display}
            bipolar={bipolar}
            defaultValue={defaultNorm}
            onChange={(v) => onKnobChange(param, min + v * range)}
            agentDriven={edited}
          />
        </div>
      );
    },
    [agentKeys, onKnobChange],
  );

  // Per-oscillator knob set (compact: vol/detune/semi/pan/PW + FM ratio/depth).
  const oscKnobs = (i: number, o: PatchParams['osc'][number]): KnobSpec[] => [
    { param: `osc.${i}.volume`,          value: o.volume,          min: 0,    max: 1,   label: 'Vol' },
    { param: `osc.${i}.detune_cents`,    value: o.detune_cents,    min: -100, max: 100, label: 'Detune', unit: 'c',  decimals: 0 },
    { param: `osc.${i}.semitone_offset`, value: o.semitone_offset, min: -48,  max: 48,  label: 'Semi',   unit: 'st', decimals: 0 },
    { param: `osc.${i}.pan`,             value: o.pan,             min: -1,   max: 1,   label: 'Pan' },
    { param: `osc.${i}.pulse_width`,     value: o.pulse_width,     min: 0.01, max: 0.99, label: 'PW' },
    { param: `osc.${i}.fm_ratio`,        value: o.fm_ratio,        min: 0.5,  max: 16,  label: 'FM Rt',  decimals: 1 },
    { param: `osc.${i}.fm_depth`,        value: o.fm_depth,        min: 0,    max: 1,   label: 'FM Dep' },
  ];

  const filterKnobs: KnobSpec[] = [
    { param: 'filter.cutoff_hz', value: patch.filter.cutoff_hz, min: 20, max: 20000, label: 'Cutoff', unit: 'hz', decimals: 0 },
    { param: 'filter.resonance', value: patch.filter.resonance, min: 0,  max: 1,     label: 'Reso' },
    { param: 'filter.env_mod',   value: patch.filter.env_mod,   min: -1, max: 1,     label: 'EnvMod' },
    { param: 'filter.drive',     value: patch.filter.drive,     min: 0,  max: 1,     label: 'Drive' },
  ];

  const ampEnvKnobs: KnobSpec[] = [
    { param: 'amp_env.attack_s',  value: patch.amp_env.attack_s,  min: 0, max: 10, label: 'Atk', unit: 's', decimals: 3 },
    { param: 'amp_env.decay_s',   value: patch.amp_env.decay_s,   min: 0, max: 10, label: 'Dec', unit: 's', decimals: 3 },
    { param: 'amp_env.sustain',   value: patch.amp_env.sustain,   min: 0, max: 1,  label: 'Sus' },
    { param: 'amp_env.release_s', value: patch.amp_env.release_s, min: 0, max: 20, label: 'Rel', unit: 's', decimals: 3 },
  ];

  const filterEnvKnobs: KnobSpec[] = [
    { param: 'filter_env.attack_s',  value: patch.filter_env.attack_s,  min: 0, max: 10, label: 'Atk', unit: 's', decimals: 3 },
    { param: 'filter_env.decay_s',   value: patch.filter_env.decay_s,   min: 0, max: 10, label: 'Dec', unit: 's', decimals: 3 },
    { param: 'filter_env.sustain',   value: patch.filter_env.sustain,   min: 0, max: 1,  label: 'Sus' },
    { param: 'filter_env.release_s', value: patch.filter_env.release_s, min: 0, max: 20, label: 'Rel', unit: 's', decimals: 3 },
  ];

  const lfoKnobs = (i: number, l: PatchParams['lfo'][number]): KnobSpec[] => [
    { param: `lfo.${i}.rate_hz`,       value: l.rate_hz,       min: 0.01, max: 20, label: 'Rate', unit: 'hz', decimals: 2 },
    { param: `lfo.${i}.depth`,         value: l.depth,         min: 0,    max: 1,  label: 'Depth' },
    { param: `lfo.${i}.phase_offset`,  value: l.phase_offset,  min: 0,    max: 1,  label: 'Phase' },
  ];

  const reverbKnobs: KnobSpec[] = [
    { param: 'reverb.size',    value: patch.reverb.size,    min: 0, max: 1, label: 'Size' },
    { param: 'reverb.damping', value: patch.reverb.damping, min: 0, max: 1, label: 'Damp' },
    { param: 'reverb.width',   value: patch.reverb.width,   min: 0, max: 1, label: 'Width' },
    { param: 'reverb.mix',     value: patch.reverb.mix,     min: 0, max: 1, label: 'Mix' },
  ];

  const delayKnobs: KnobSpec[] = [
    { param: 'delay.time_s',   value: patch.delay.time_s,   min: 0, max: 2,    label: 'Time', unit: 's', decimals: 3 },
    { param: 'delay.feedback', value: patch.delay.feedback, min: 0, max: 0.99, label: 'Feedback' },
    { param: 'delay.mix',      value: patch.delay.mix,      min: 0, max: 1,    label: 'Mix' },
  ];

  const globalKnobs: KnobSpec[] = [
    { param: 'master_gain',  value: patch.master_gain,  min: 0, max: 1, label: 'Gain' },
    { param: 'portamento_s', value: patch.portamento_s, min: 0, max: 2, label: 'Glide', unit: 's', decimals: 3 },
  ];

  return (
    <div className="modules-grid" role="region" aria-label="Synthesis modules">
      <div className="visually-hidden" aria-live="polite" aria-atomic="true">
        {agentKeys.size > 0
          ? `Agent updated ${agentKeys.size} patch parameter${agentKeys.size === 1 ? '' : 's'}`
          : ''}
      </div>

      {/* Row 1: three oscillators */}
      {patch.osc.map((osc, i) => (
        <section key={`osc${i}`} className={`module module-osc module-osc-${i + 1}`} aria-label={`Oscillator ${i + 1}`}>
          <header className="module-header">
            <span className="module-title">OSC {i + 1}</span>
          </header>
          <div className="module-knobs">{oscKnobs(i, osc).map(renderKnob)}</div>
        </section>
      ))}

      {/* Row 2: Filter (spans 2) | Amp Env (spans 2) */}
      <section className="module module-filter" aria-label="Filter">
        <header className="module-header">
          <span className="module-title">Filter</span>
        </header>
        <div className="module-knobs">{filterKnobs.map(renderKnob)}</div>
      </section>

      <section className="module module-amp-env" aria-label="Amp envelope">
        <header className="module-header">
          <span className="module-title">Amp Env</span>
        </header>
        <div className="module-knobs">{ampEnvKnobs.map(renderKnob)}</div>
      </section>

      {/* Row 3: Filter Env (Env 2) | LFO 1 | LFO 2 */}
      <section className="module module-env2" aria-label="Filter envelope">
        <header className="module-header">
          <span className="module-title">Env 2 · Filter</span>
        </header>
        <div className="module-knobs">{filterEnvKnobs.map(renderKnob)}</div>
      </section>

      {patch.lfo.map((lfo, i) => (
        <section key={`lfo${i}`} className={`module module-lfo module-lfo-${i + 1}`} aria-label={`LFO ${i + 1}`}>
          <header className="module-header">
            <span className="module-title">LFO {i + 1}</span>
          </header>
          <div className="module-knobs">{lfoKnobs(i, lfo).map(renderKnob)}</div>
        </section>
      ))}

      {/* Row 4: Reverb (spans 2) | Delay (spans 2) */}
      <section className="module module-reverb" aria-label="Reverb">
        <header className="module-header">
          <span className="module-title">FX · Reverb</span>
        </header>
        <div className="module-knobs">{reverbKnobs.map(renderKnob)}</div>
      </section>

      <section className="module module-delay" aria-label="Delay">
        <header className="module-header">
          <span className="module-title">FX · Delay</span>
        </header>
        <div className="module-knobs">{delayKnobs.map(renderKnob)}</div>
      </section>

      {/* Row 5: Global (Gain + Glide) — tucked under FX row, spans full width. */}
      <section className="module module-global" aria-label="Global">
        <header className="module-header">
          <span className="module-title">Global</span>
        </header>
        <div className="module-knobs">{globalKnobs.map(renderKnob)}</div>
      </section>
    </div>
  );
}
