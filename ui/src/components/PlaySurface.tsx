import { useCallback, useMemo } from 'react';
import { Knob } from './Knob';
import { ABToggle, type ABSide } from './ABToggle';
import { MacroState } from './MacroBar';
import type { PatchParams } from './KnobGrid';
import { type ModSourceId } from '../data/modulation';
import './PlaySurface.css';

// ── PlaySurface (Phase A / #270) ─────────────────────────────────────
//
// The default left-column mount. Replaces the fine-grain ModulesGrid
// for first-time users (and stays the surface until the user opens
// the hood — see HoodSlideOver).
//
// Layout (no header label — silence is on-brand):
//   • Hero row: 4 macros @ 96px (BRIGHTNESS / WOBBLE / EDGE / AIR)
//   • Expressive trio @ ~64px: CUTOFF, SPACE, VOLUME
//   • A/B toggle pill (center-bottom)
//   • "Open the hood" reveal affordance (bottom)
//
// Every control wires straight into the existing patch state machinery
// (handleMacroChange / handleKnobChange in App.tsx) so the hood-open
// surface stays in sync without bespoke synchronisation logic.

interface PlaySurfaceProps {
  macros: MacroState[];
  onMacroChange: (index: number, value: number) => void;
  patch: PatchParams;
  onKnobChange: (param: string, value: number) => void;
  onOpenHood: () => void;
  // A/B compare
  activeSlot: ABSide;
  onSelectSlot: (slot: ABSide) => void;
  onToggleSlot: () => void;
  abReady?: boolean;
}

// ── Param helpers ─────────────────────────────────────────────────────
// Filter cutoff is the only log-scale value on the surface. Knobs are
// normalised 0..1; we map to [20Hz, 20kHz] symmetrically with the
// PatchPreview helper so the display reads the same everywhere.
const CUTOFF_MIN = 20;
const CUTOFF_MAX = 20000;
const CUTOFF_LOG_MIN = Math.log(CUTOFF_MIN);
const CUTOFF_LOG_MAX = Math.log(CUTOFF_MAX);

function normFromCutoff(hz: number): number {
  const clamped = Math.max(CUTOFF_MIN, Math.min(CUTOFF_MAX, hz));
  return (Math.log(clamped) - CUTOFF_LOG_MIN) / (CUTOFF_LOG_MAX - CUTOFF_LOG_MIN);
}

function cutoffFromNorm(n: number): number {
  const t = Math.max(0, Math.min(1, n));
  return Math.exp(CUTOFF_LOG_MIN + t * (CUTOFF_LOG_MAX - CUTOFF_LOG_MIN));
}

function formatCutoff(hz: number): string {
  if (hz >= 1000) return `${(hz / 1000).toFixed(hz >= 10000 ? 1 : 2)} kHz`;
  return `${Math.round(hz)} Hz`;
}

// Gear glyph — 6 teeth, 1.5px stroke, no fill. Inline so the affordance
// stays self-contained.
function GearGlyph() {
  return (
    <svg
      className="play-surface-gear"
      width="12"
      height="12"
      viewBox="0 0 16 16"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <circle cx="8" cy="8" r="2.4" stroke="currentColor" strokeWidth="1.5" />
      {/* Six teeth: drawn as short rectangles at 60° intervals from center. */}
      {[0, 60, 120, 180, 240, 300].map((deg) => {
        const rad = (deg * Math.PI) / 180;
        const inner = 4.4;
        const outer = 6.6;
        const x1 = 8 + Math.cos(rad) * inner;
        const y1 = 8 + Math.sin(rad) * inner;
        const x2 = 8 + Math.cos(rad) * outer;
        const y2 = 8 + Math.sin(rad) * outer;
        return (
          <line
            key={deg}
            x1={x1.toFixed(2)}
            y1={y1.toFixed(2)}
            x2={x2.toFixed(2)}
            y2={y2.toFixed(2)}
            stroke="currentColor"
            strokeWidth="1.5"
            strokeLinecap="round"
          />
        );
      })}
    </svg>
  );
}

export function PlaySurface({
  macros,
  onMacroChange,
  patch,
  onKnobChange,
  onOpenHood,
  activeSlot,
  onSelectSlot,
  onToggleSlot,
  abReady,
}: PlaySurfaceProps) {
  // ── Cutoff ────────────────────────────────────────────────────────
  const cutoffNorm = useMemo(
    () => normFromCutoff(patch.filter.cutoff_hz),
    [patch.filter.cutoff_hz],
  );
  const handleCutoff = useCallback(
    (v: number) => onKnobChange('filter.cutoff_hz', cutoffFromNorm(v)),
    [onKnobChange],
  );

  // ── Space (reverb mix) ────────────────────────────────────────────
  const space = patch.reverb.mix;
  const handleSpace = useCallback(
    (v: number) => onKnobChange('reverb.mix', v),
    [onKnobChange],
  );

  // ── Volume (master gain) ──────────────────────────────────────────
  const volume = patch.master_gain;
  const handleVolume = useCallback(
    (v: number) => onKnobChange('master_gain', v),
    [onKnobChange],
  );

  return (
    <div className="play-surface" role="region" aria-label="Play surface">
      {/* Hero row — 4 macros */}
      <div className="play-surface-macros" role="group" aria-label="Macros">
        {macros.map((m, i) => {
          const sourceId: ModSourceId = (`macro${i + 1}`) as ModSourceId;
          return (
            <div className="play-surface-macro-slot" key={i}>
              <div className="play-surface-knob play-surface-knob--hero">
                <Knob
                  value={m.value}
                  onChange={(v) => onMacroChange(i, v)}
                  name={m.label}
                  size="lg"
                  defaultValue={0}
                  modSource={sourceId}
                  modAmount={m.value}
                  displayValue={`${Math.round(m.value * 100)}%`}
                />
              </div>
              <span className="play-surface-macro-label">{m.label}</span>
              <span className="play-surface-macro-value">
                {Math.round(m.value * 100)}%
              </span>
            </div>
          );
        })}
      </div>

      <div className="play-surface-divider" aria-hidden="true" />

      {/* Expressive trio — cutoff / space / volume */}
      <div className="play-surface-trio" role="group" aria-label="Expressive controls">
        <div className="play-surface-trio-slot">
          <div className="play-surface-knob play-surface-knob--trio">
            <Knob
              value={cutoffNorm}
              onChange={handleCutoff}
              name="CUTOFF"
              size="lg"
              defaultValue={normFromCutoff(18000)}
              displayValue={formatCutoff(patch.filter.cutoff_hz)}
            />
          </div>
          <span className="play-surface-trio-label">CUTOFF</span>
        </div>
        <div className="play-surface-trio-slot">
          <div className="play-surface-knob play-surface-knob--trio">
            <Knob
              value={space}
              onChange={handleSpace}
              name="SPACE"
              size="lg"
              defaultValue={0}
              displayValue={`${Math.round(space * 100)}%`}
            />
          </div>
          <span className="play-surface-trio-label">SPACE</span>
        </div>
        <div className="play-surface-trio-slot">
          <div className="play-surface-knob play-surface-knob--trio">
            <Knob
              value={volume}
              onChange={handleVolume}
              name="VOLUME"
              size="lg"
              defaultValue={1}
              displayValue={`${Math.round(volume * 100)}%`}
            />
          </div>
          <span className="play-surface-trio-label">VOLUME</span>
        </div>
      </div>

      <div className="play-surface-divider" aria-hidden="true" />

      {/* A/B compare pill — center-bottom slot */}
      <div className="play-surface-ab">
        <ABToggle
          active={activeSlot}
          onSetActive={onSelectSlot}
          onToggle={onToggleSlot}
          ready={abReady}
        />
      </div>

      {/* Bottom: open the hood reveal affordance */}
      <div className="play-surface-foot">
        <button
          type="button"
          className="play-surface-hood-btn"
          onClick={onOpenHood}
          aria-label="Open the hood — full knob layout"
        >
          <GearGlyph />
          <span>Open the hood</span>
        </button>
      </div>
    </div>
  );
}
