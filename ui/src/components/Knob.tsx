import React, { useCallback, useEffect, useId, useMemo, useRef, useState } from 'react';
import './Knob.css';

// ---------------------------------------------------------------------------
// TIMBRE Knob — SVG-based, accessible, fully themed via design tokens.
// See design/REBRAND.md §4 (Knob Anatomy) for spec.
//
// Value semantics: this component is 0..1 normalized. The caller is
// responsible for mapping to a parameter range and formatting the display
// string (`displayValue`). This keeps the knob pure and reusable.
// ---------------------------------------------------------------------------

export type KnobSize = 'lg' | 'md' | 'sm';
export type ModSource =
  | 'lfo1'
  | 'lfo2'
  | 'env1'
  | 'env2'
  | 'macro1'
  | 'macro2';

export interface KnobProps {
  value: number; // 0..1 normalized
  onChange: (v: number) => void;
  name: string; // bottom label text
  displayValue?: string; // center value (caller formats — e.g. "440 Hz")
  size?: KnobSize;
  bipolar?: boolean;
  defaultValue?: number; // double-click / Enter resets to this
  modSource?: ModSource;
  modAmount?: number; // 0..1 — controls outer halo arc length
  disabled?: boolean;
  onContextMenu?: (e: React.MouseEvent) => void;
  agentDriven?: boolean; // brief flash when the value was just changed by AI
}

// Arc geometry: 270° sweep, starting at 7 o'clock (135° in SVG coords, which
// uses 0° = +X / 3 o'clock and angles rotating clockwise) and ending at 5
// o'clock (405° = 45°+360°).
const START_DEG = 135;
const SWEEP = 270;

// Per-size geometry. The SVG viewBox is fixed (100×100); the rendered <svg>
// width/height is scaled by size. This keeps math simple and crisp at any DPI.
const VIEWBOX = 100;
const SIZE_PX: Record<KnobSize, number> = { lg: 72, md: 52, sm: 36 };

interface Geometry {
  trackR: number; // outer ring radius
  plateR: number; // inner plate radius
  indicatorInner: number; // indicator line start radius (from center)
  indicatorOuter: number; // indicator line end radius
  modRingR: number; // modulation source ring radius (outside track)
  modHaloR: number; // mod amount halo radius (outside mod ring)
  strokeMain: number;
  strokeMod: number;
  strokeHalo: number;
  strokeIndicator: number;
}

// All values in the 0..100 viewBox space — independent of rendered size.
const GEOM: Geometry = {
  trackR: 40,
  plateR: 30,
  indicatorInner: 6,
  indicatorOuter: 28,
  modRingR: 46,
  modHaloR: 50,
  strokeMain: 4,
  strokeMod: 3,
  strokeHalo: 5,
  strokeIndicator: 3,
};

function polarToXY(cx: number, cy: number, r: number, deg: number) {
  const rad = (deg * Math.PI) / 180;
  return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}

function arcPath(cx: number, cy: number, r: number, startDeg: number, endDeg: number): string {
  const s = polarToXY(cx, cy, r, startDeg);
  const e = polarToXY(cx, cy, r, endDeg);
  let sweep = endDeg - startDeg;
  if (sweep < 0) sweep += 360;
  const large = sweep > 180 ? 1 : 0;
  // Degenerate (zero-length) arcs cause Safari to draw the full circle —
  // emit a tiny move-only path instead.
  if (sweep <= 0.0001) return `M ${s.x.toFixed(3)} ${s.y.toFixed(3)}`;
  return `M ${s.x.toFixed(3)} ${s.y.toFixed(3)} A ${r} ${r} 0 ${large} 1 ${e.x.toFixed(3)} ${e.y.toFixed(3)}`;
}

function clamp01(v: number) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}

export function Knob({
  value,
  onChange,
  name,
  displayValue,
  size = 'md',
  bipolar = false,
  defaultValue,
  modSource,
  modAmount = 0,
  disabled = false,
  onContextMenu,
  agentDriven = false,
}: KnobProps) {
  const norm = clamp01(value);
  const pxSize = SIZE_PX[size];
  const cx = VIEWBOX / 2;
  const cy = VIEWBOX / 2;

  // Unique gradient IDs so multiple knobs in one document don't collide.
  // React 18's useId returns ":r0:" style — sanitize for SVG ID/url() ref.
  const rawUid = useId();
  const uid = rawUid.replace(/:/g, '');
  const fillGradId = `knob-fill-grad-${uid}`;
  const plateGradId = `knob-plate-grad-${uid}`;

  // Track (full 270° arc, always drawn)
  const trackPath = useMemo(
    () => arcPath(cx, cy, GEOM.trackR, START_DEG, START_DEG + SWEEP),
    [cx, cy],
  );

  // Fill arc
  // - Unipolar: from start (7 o'clock) to current value
  // - Bipolar:  from 12 o'clock (270° in SVG coords) outward in either dir
  const TWELVE = 270; // SVG-coord angle for 12 o'clock = -90° = 270°
  const valueDeg = START_DEG + norm * SWEEP;

  let fillPath = '';
  let fillStrokeViolet = true; // false = magenta accent-secondary
  if (bipolar) {
    // Map norm 0..1 to deg 135..405; center (0.5) sits at TWELVE = 270.
    if (norm >= 0.5) {
      fillPath = arcPath(cx, cy, GEOM.trackR, TWELVE, valueDeg);
    } else {
      // Negative side — magenta. Draw from current value up to TWELVE.
      fillPath = arcPath(cx, cy, GEOM.trackR, valueDeg, TWELVE);
      fillStrokeViolet = false;
    }
  } else if (norm > 0) {
    fillPath = arcPath(cx, cy, GEOM.trackR, START_DEG, valueDeg);
  }
  const fillStroke = fillStrokeViolet
    ? `url(#${fillGradId})`
    : 'var(--accent-secondary)';

  // Indicator endpoints
  const indStart = polarToXY(cx, cy, GEOM.indicatorInner, valueDeg);
  const indEnd = polarToXY(cx, cy, GEOM.indicatorOuter, valueDeg);

  // Modulation halo arc length proportional to modAmount.
  const haloPath = useMemo(() => {
    if (!modSource || modAmount <= 0) return '';
    const len = clamp01(modAmount) * SWEEP;
    return arcPath(cx, cy, GEOM.modHaloR, START_DEG, START_DEG + len);
  }, [modSource, modAmount, cx, cy]);

  // Concentric mod-source ring (animated via CSS).
  const modRingPath = useMemo(() => {
    if (!modSource) return '';
    return arcPath(cx, cy, GEOM.modRingR, START_DEG, START_DEG + SWEEP);
  }, [modSource, cx, cy]);

  // ---------------------------------------------------------------------
  // Interaction
  // ---------------------------------------------------------------------

  const rootRef = useRef<HTMLDivElement | null>(null);
  const dragRef = useRef<{
    startY: number;
    startNorm: number;
    pointerId: number;
    fine: boolean;
    invert: boolean;
  } | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [isHover, setIsHover] = useState(false);
  const [flashing, setFlashing] = useState(false);
  const flashTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Agent-driven flash
  useEffect(() => {
    if (!agentDriven) return;
    setFlashing(true);
    if (flashTimer.current) clearTimeout(flashTimer.current);
    flashTimer.current = setTimeout(() => setFlashing(false), 400);
    return () => {
      if (flashTimer.current) clearTimeout(flashTimer.current);
    };
  }, [value, agentDriven]);

  const commit = useCallback(
    (next: number) => {
      onChange(clamp01(next));
    },
    [onChange],
  );

  const reset = useCallback(() => {
    commit(defaultValue ?? 0.5);
  }, [commit, defaultValue]);

  const onPointerDown = useCallback(
    (e: React.PointerEvent<SVGSVGElement>) => {
      if (disabled) return;
      // Ignore right-click and middle-click — right-click goes to context
      // menu, middle-click is unused (browser autoscroll).
      if (e.button !== 0) return;
      e.preventDefault();
      (e.currentTarget as Element).setPointerCapture(e.pointerId);
      dragRef.current = {
        startY: e.clientY,
        startNorm: norm,
        pointerId: e.pointerId,
        fine: e.shiftKey,
        invert: e.altKey,
      };
      setIsDragging(true);
    },
    [disabled, norm],
  );

  const onPointerMove = useCallback(
    (e: React.PointerEvent<SVGSVGElement>) => {
      const drag = dragRef.current;
      if (!drag) return;
      // Per-pixel sensitivity. Spec: dy/200 units per pixel. Shift = /10 fine.
      const baseDivisor = drag.fine ? 2000 : 200;
      const rawDelta = (drag.startY - e.clientY) / baseDivisor;
      const delta = drag.invert ? -rawDelta : rawDelta;
      commit(drag.startNorm + delta);
    },
    [commit],
  );

  const onPointerUp = useCallback(() => {
    dragRef.current = null;
    setIsDragging(false);
  }, []);

  const onWheel = useCallback(
    (e: React.WheelEvent<HTMLDivElement>) => {
      if (disabled) return;
      e.preventDefault();
      const step = e.shiftKey ? 0.001 : 0.01;
      // deltaY > 0 = scroll down = decrease (matches drag-down semantics).
      const dir = e.deltaY > 0 ? -1 : 1;
      commit(norm + dir * step);
    },
    [commit, norm, disabled],
  );

  const onDoubleClick = useCallback(() => {
    if (disabled) return;
    reset();
  }, [reset, disabled]);

  const onKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLDivElement>) => {
      if (disabled) return;
      let next: number | null = null;
      switch (e.key) {
        case 'ArrowUp':
        case 'ArrowRight':
          next = norm + 0.05;
          break;
        case 'ArrowDown':
        case 'ArrowLeft':
          next = norm - 0.05;
          break;
        case 'PageUp':
          next = norm + 0.1;
          break;
        case 'PageDown':
          next = norm - 0.1;
          break;
        case 'Home':
          next = 0;
          break;
        case 'End':
          next = 1;
          break;
        case 'Enter':
        case ' ':
          reset();
          e.preventDefault();
          return;
        default:
          return;
      }
      e.preventDefault();
      if (next !== null) commit(next);
    },
    [commit, norm, reset, disabled],
  );

  const handleContextMenu = useCallback(
    (e: React.MouseEvent) => {
      if (!onContextMenu) return;
      e.preventDefault();
      onContextMenu(e);
    },
    [onContextMenu],
  );

  // ---------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------

  const showValueOverlay = (isHover || isDragging) && Boolean(displayValue);
  const ariaText = displayValue ?? norm.toFixed(2);

  const classes = [
    'knob-root',
    `knob-${size}`,
    bipolar ? 'knob-bipolar' : '',
    isDragging ? 'knob-dragging' : '',
    isHover ? 'knob-hover' : '',
    disabled ? 'knob-disabled' : '',
    flashing ? 'knob-agent-flash' : '',
    modSource ? `knob-mod knob-mod-${modSource}` : '',
  ]
    .filter(Boolean)
    .join(' ');


  return (
    <div
      ref={rootRef}
      className={classes}
      role="slider"
      tabIndex={disabled ? -1 : 0}
      aria-label={name}
      aria-valuemin={0}
      aria-valuemax={1}
      aria-valuenow={norm}
      aria-valuetext={ariaText}
      aria-disabled={disabled || undefined}
      onKeyDown={onKeyDown}
      onWheel={onWheel}
      onDoubleClick={onDoubleClick}
      onContextMenu={handleContextMenu}
      onPointerEnter={() => setIsHover(true)}
      onPointerLeave={() => setIsHover(false)}
      style={{ ['--knob-size' as string]: `${pxSize}px` }}
    >
      <div className="knob-dial">
        <svg
          className="knob-svg"
          width={pxSize}
          height={pxSize}
          viewBox={`0 0 ${VIEWBOX} ${VIEWBOX}`}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={onPointerUp}
          aria-hidden="true"
          focusable="false"
          style={{ touchAction: 'none' }}
        >
          <defs>
            <linearGradient
              id={fillGradId}
              x1="0%"
              y1="0%"
              x2="100%"
              y2="100%"
            >
              <stop offset="0%" stopColor="var(--accent-primary)" />
              <stop offset="100%" stopColor="#B388FF" />
            </linearGradient>
            <radialGradient id={plateGradId} cx="50%" cy="42%" r="55%">
              <stop offset="0%" stopColor="#252834" />
              <stop offset="60%" stopColor="#1E2129" />
              <stop offset="100%" stopColor="#14161D" />
            </radialGradient>
          </defs>

          {/* Mod amount halo — outermost, faint */}
          {haloPath && (
            <path
              className="knob-mod-halo"
              d={haloPath}
              fill="none"
              strokeWidth={GEOM.strokeHalo}
              strokeLinecap="round"
            />
          )}

          {/* Mod source animated ring */}
          {modRingPath && (
            <path
              className="knob-mod-ring"
              d={modRingPath}
              fill="none"
              strokeWidth={GEOM.strokeMod}
              strokeLinecap="round"
            />
          )}

          {/* Track */}
          <path
            className="knob-track"
            d={trackPath}
            fill="none"
            strokeWidth={GEOM.strokeMain}
            strokeLinecap="round"
          />

          {/* Bipolar center tick */}
          {bipolar && (
            <line
              className="knob-center-tick"
              x1={cx}
              y1={cy - GEOM.trackR - 3}
              x2={cx}
              y2={cy - GEOM.trackR + 3}
              strokeWidth="1.5"
              strokeLinecap="round"
            />
          )}

          {/* Fill arc */}
          {fillPath && (
            <path
              className="knob-fill"
              d={fillPath}
              fill="none"
              stroke={fillStroke}
              strokeWidth={GEOM.strokeMain}
              strokeLinecap="round"
            />
          )}

          {/* Plate (inner) */}
          <circle
            className="knob-plate"
            cx={cx}
            cy={cy}
            r={GEOM.plateR}
            fill={`url(#${plateGradId})`}
          />
          {/* Plate hairline highlight (top) + shadow (bottom) handled via CSS shadow on this stroke ring */}
          <circle
            className="knob-plate-ring"
            cx={cx}
            cy={cy}
            r={GEOM.plateR}
            fill="none"
            strokeWidth="1"
          />

          {/* Indicator */}
          <line
            className="knob-indicator"
            x1={indStart.x}
            y1={indStart.y}
            x2={indEnd.x}
            y2={indEnd.y}
            strokeWidth={GEOM.strokeIndicator}
            strokeLinecap="round"
          />
        </svg>

        {/* Center value overlay — fades in on hover/drag */}
        <span
          className={`knob-center-value${showValueOverlay ? ' is-visible' : ''}`}
          aria-hidden="true"
        >
          {displayValue ?? ''}
        </span>
      </div>

      <span className="knob-name" aria-hidden="true">
        {name}
      </span>
    </div>
  );
}

