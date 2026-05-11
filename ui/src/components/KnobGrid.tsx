import React, { useCallback, useEffect, useState } from 'react';
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

// ---------------------------------------------------------------------------
// Collapse persistence
// ---------------------------------------------------------------------------

const STORAGE_KEY = 'agentic-synth.knob-collapse.v1';

// Sections expanded by default. Everything not listed here starts collapsed.
const DEFAULT_EXPANDED: ReadonlySet<string> = new Set([
  'Osc 1',
  'Filter',
  'Amp Env',
]);

function loadCollapseState(): Record<string, boolean> {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as Record<string, boolean>;
    return parsed && typeof parsed === 'object' ? parsed : {};
  } catch {
    return {};
  }
}

function saveCollapseState(state: Record<string, boolean>) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  } catch {
    // ignore quota / disabled storage
  }
}

function isOpen(state: Record<string, boolean>, section: string): boolean {
  if (Object.prototype.hasOwnProperty.call(state, section)) return state[section];
  return DEFAULT_EXPANDED.has(section);
}

// ---------------------------------------------------------------------------
// Section summaries
// ---------------------------------------------------------------------------

function pct(v: number): string {
  return `${Math.round(v * 100)}%`;
}

function oscSummary(o: OscParams): string {
  if (o.volume <= 0.001) return 'muted';
  const bits: string[] = [`vol ${pct(o.volume)}`];
  if (Math.abs(o.detune_cents) >= 1) bits.push(`${o.detune_cents > 0 ? '+' : ''}${o.detune_cents.toFixed(0)}c`);
  if (o.semitone_offset !== 0) bits.push(`${o.semitone_offset > 0 ? '+' : ''}${o.semitone_offset}st`);
  return bits.join(' · ');
}

function filterSummary(f: FilterParams): string {
  const hz = f.cutoff_hz >= 1000 ? `${(f.cutoff_hz / 1000).toFixed(1)}k` : `${Math.round(f.cutoff_hz)}`;
  return `cutoff ${hz}Hz · res ${pct(f.resonance)}`;
}

function envSummary(e: EnvParams): string {
  return `A ${e.attack_s.toFixed(2)}s · S ${pct(e.sustain)} · R ${e.release_s.toFixed(2)}s`;
}

function lfoSummary(l: LfoParams): string {
  if (l.depth <= 0.001) return 'idle';
  return `${l.rate_hz.toFixed(2)}Hz · depth ${pct(l.depth)}`;
}

function reverbSummary(r: ReverbParams): string {
  return `mix ${pct(r.mix)} · size ${pct(r.size)}`;
}

function delaySummary(d: DelayParams): string {
  return `mix ${pct(d.mix)} · time ${d.time_s.toFixed(2)}s`;
}

function globalSummary(p: PatchParams): string {
  return `gain ${pct(p.master_gain)} · glide ${p.portamento_s.toFixed(2)}s`;
}

// ---------------------------------------------------------------------------
// Collapsible section wrapper
// ---------------------------------------------------------------------------

interface SectionProps {
  title: string;
  summary: string;
  open: boolean;
  onToggle: (title: string) => void;
  children: React.ReactNode;
}

function CollapsibleSection({ title, summary, open, onToggle, children }: SectionProps) {
  const panelId = `knob-section-${title.replace(/\s+/g, '-').toLowerCase()}`;
  return (
    <section className={`knob-section${open ? ' knob-section-open' : ' knob-section-collapsed'}`}>
      <button
        type="button"
        className="knob-section-header"
        aria-expanded={open}
        aria-controls={panelId}
        onClick={() => onToggle(title)}
      >
        <span className={`knob-section-chevron${open ? ' open' : ''}`} aria-hidden="true">▸</span>
        <span className="knob-section-title">{title}</span>
        {!open && <span className="knob-section-summary">{summary}</span>}
      </button>
      {open && (
        <div id={panelId} className="knob-row" role="group" aria-label={title}>
          {children}
        </div>
      )}
    </section>
  );
}

// ---------------------------------------------------------------------------
// KnobGrid
// ---------------------------------------------------------------------------

interface KnobGridProps {
  patch: PatchParams;
  agentKeys: Set<string>;
  onKnobChange: (param: string, value: number) => void;
}

export function KnobGrid({ patch, agentKeys, onKnobChange }: KnobGridProps) {
  const [collapseState, setCollapseState] = useState<Record<string, boolean>>(() => loadCollapseState());

  useEffect(() => {
    saveCollapseState(collapseState);
  }, [collapseState]);

  const toggle = useCallback((title: string) => {
    setCollapseState((prev) => ({ ...prev, [title]: !isOpen(prev, title) }));
  }, []);

  // Build the full list of section titles so expand/collapse-all is exhaustive.
  const allTitles: string[] = [
    ...patch.osc.map((_, i) => `Osc ${i + 1}`),
    'Filter',
    'Amp Env',
    'Filter Env',
    ...patch.lfo.map((_, i) => `LFO ${i + 1}`),
    'Reverb',
    'Delay',
    'Global',
  ];

  const expandAll = useCallback(() => {
    setCollapseState(() => {
      const next: Record<string, boolean> = {};
      for (const t of allTitles) next[t] = true;
      return next;
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [allTitles.join('|')]);

  const collapseAll = useCallback(() => {
    setCollapseState(() => {
      const next: Record<string, boolean> = {};
      for (const t of allTitles) next[t] = false;
      return next;
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [allTitles.join('|')]);

  const k = useCallback(
    (param: string, value: number, min: number, max: number, label: string, unit?: string, decimals?: number) => {
      const edited = agentKeys.has(param);
      return (
        <div key={param} className={`knob-slot${edited ? ' knob-slot-agent' : ''}`}>
          {edited && (
            <span className="knob-agent-badge" aria-label="Edited by agent" title="Edited by agent">AI</span>
          )}
          <Knob
            value={value}
            min={min}
            max={max}
            label={label}
            unit={unit}
            decimals={decimals}
            onChange={(v) => onKnobChange(param, v)}
            agentDriven={edited}
          />
        </div>
      );
    },
    [agentKeys, onKnobChange],
  );

  return (
    <div className="knob-grid">
      <div className="knob-grid-toolbar" role="toolbar" aria-label="Section visibility">
        <button type="button" className="knob-grid-tool" onClick={expandAll}>Expand all</button>
        <button type="button" className="knob-grid-tool" onClick={collapseAll}>Collapse all</button>
      </div>

      <div className="visually-hidden" aria-live="polite" aria-atomic="true">
        {agentKeys.size > 0
          ? `Agent updated ${agentKeys.size} patch parameter${agentKeys.size === 1 ? '' : 's'}`
          : ''}
      </div>

      {patch.osc.map((osc, i) => {
        const title = `Osc ${i + 1}`;
        return (
          <CollapsibleSection
            key={`osc${i}`}
            title={title}
            summary={oscSummary(osc)}
            open={isOpen(collapseState, title)}
            onToggle={toggle}
          >
            {k(`osc.${i}.volume`, osc.volume, 0, 1, 'Vol')}
            {k(`osc.${i}.detune_cents`, osc.detune_cents, -100, 100, 'Detune', 'c', 0)}
            {k(`osc.${i}.semitone_offset`, osc.semitone_offset, -48, 48, 'Semi', 'st', 0)}
            {k(`osc.${i}.pan`, osc.pan, -1, 1, 'Pan')}
            {k(`osc.${i}.pulse_width`, osc.pulse_width, 0.01, 0.99, 'PW')}
            {k(`osc.${i}.fm_ratio`, osc.fm_ratio, 0.5, 16, 'FM Rt', '', 1)}
            {k(`osc.${i}.fm_depth`, osc.fm_depth, 0, 1, 'FM Dep')}
          </CollapsibleSection>
        );
      })}

      <CollapsibleSection
        title="Filter"
        summary={filterSummary(patch.filter)}
        open={isOpen(collapseState, 'Filter')}
        onToggle={toggle}
      >
        {k('filter.cutoff_hz', patch.filter.cutoff_hz, 20, 20000, 'Cutoff', 'hz', 0)}
        {k('filter.resonance', patch.filter.resonance, 0, 1, 'Reso')}
        {k('filter.env_mod', patch.filter.env_mod, -1, 1, 'EnvMod')}
        {k('filter.drive', patch.filter.drive, 0, 1, 'Drive')}
      </CollapsibleSection>

      <CollapsibleSection
        title="Amp Env"
        summary={envSummary(patch.amp_env)}
        open={isOpen(collapseState, 'Amp Env')}
        onToggle={toggle}
      >
        {k('amp_env.attack_s', patch.amp_env.attack_s, 0, 10, 'Atk', 's', 3)}
        {k('amp_env.decay_s', patch.amp_env.decay_s, 0, 10, 'Dec', 's', 3)}
        {k('amp_env.sustain', patch.amp_env.sustain, 0, 1, 'Sus')}
        {k('amp_env.release_s', patch.amp_env.release_s, 0, 20, 'Rel', 's', 3)}
      </CollapsibleSection>

      <CollapsibleSection
        title="Filter Env"
        summary={envSummary(patch.filter_env)}
        open={isOpen(collapseState, 'Filter Env')}
        onToggle={toggle}
      >
        {k('filter_env.attack_s', patch.filter_env.attack_s, 0, 10, 'Atk', 's', 3)}
        {k('filter_env.decay_s', patch.filter_env.decay_s, 0, 10, 'Dec', 's', 3)}
        {k('filter_env.sustain', patch.filter_env.sustain, 0, 1, 'Sus')}
        {k('filter_env.release_s', patch.filter_env.release_s, 0, 20, 'Rel', 's', 3)}
      </CollapsibleSection>

      {patch.lfo.map((lfo, i) => {
        const title = `LFO ${i + 1}`;
        return (
          <CollapsibleSection
            key={`lfo${i}`}
            title={title}
            summary={lfoSummary(lfo)}
            open={isOpen(collapseState, title)}
            onToggle={toggle}
          >
            {k(`lfo.${i}.rate_hz`, lfo.rate_hz, 0.01, 20, 'Rate', 'hz', 2)}
            {k(`lfo.${i}.depth`, lfo.depth, 0, 1, 'Depth')}
            {k(`lfo.${i}.phase_offset`, lfo.phase_offset, 0, 1, 'Phase')}
          </CollapsibleSection>
        );
      })}

      <CollapsibleSection
        title="Reverb"
        summary={reverbSummary(patch.reverb)}
        open={isOpen(collapseState, 'Reverb')}
        onToggle={toggle}
      >
        {k('reverb.size', patch.reverb.size, 0, 1, 'Size')}
        {k('reverb.damping', patch.reverb.damping, 0, 1, 'Damp')}
        {k('reverb.width', patch.reverb.width, 0, 1, 'Width')}
        {k('reverb.mix', patch.reverb.mix, 0, 1, 'Mix')}
      </CollapsibleSection>

      <CollapsibleSection
        title="Delay"
        summary={delaySummary(patch.delay)}
        open={isOpen(collapseState, 'Delay')}
        onToggle={toggle}
      >
        {k('delay.time_s', patch.delay.time_s, 0, 2, 'Time', 's', 3)}
        {k('delay.feedback', patch.delay.feedback, 0, 0.99, 'Feedback')}
        {k('delay.mix', patch.delay.mix, 0, 1, 'Mix')}
      </CollapsibleSection>

      <CollapsibleSection
        title="Global"
        summary={globalSummary(patch)}
        open={isOpen(collapseState, 'Global')}
        onToggle={toggle}
      >
        {k('master_gain', patch.master_gain, 0, 1, 'Gain')}
        {k('portamento_s', patch.portamento_s, 0, 2, 'Glide', 's', 3)}
      </CollapsibleSection>
    </div>
  );
}
