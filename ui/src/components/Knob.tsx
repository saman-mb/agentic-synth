import React, { useCallback, useEffect, useRef, useState } from 'react';

interface KnobProps {
  value: number;
  min: number;
  max: number;
  label: string;
  unit?: string;
  decimals?: number;
  onChange: (value: number) => void;
  agentDriven?: boolean;
  step?: number;
}

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
  return `M ${s.x.toFixed(2)} ${s.y.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${e.x.toFixed(2)} ${e.y.toFixed(2)}`;
}

const START_DEG = 135;
const SWEEP = 270;

function formatVal(v: number, unit: string | undefined, decimals: number) {
  const s = v.toFixed(decimals);
  return unit ? `${s}${unit}` : s;
}

// Spoken form for aria-valuetext — screen readers say "820 hertz" not "820Hz"
function spokenUnit(unit: string | undefined): string {
  if (!unit) return '';
  const u = unit.trim().toLowerCase();
  switch (u) {
    case 'hz': return ' hertz';
    case 'khz': return ' kilohertz';
    case 's': return ' seconds';
    case 'ms': return ' milliseconds';
    case 'db': return ' decibels';
    case '%': return ' percent';
    case 'st': return ' semitones';
    default: return ` ${unit}`;
  }
}

function clamp(v: number, min: number, max: number) {
  return Math.max(min, Math.min(max, v));
}

export function Knob({
  value,
  min,
  max,
  label,
  unit,
  decimals = 2,
  onChange,
  agentDriven,
  step,
}: KnobProps) {
  const norm = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const valueDeg = START_DEG + norm * SWEEP;
  const endDeg = START_DEG + SWEEP;

  const cx = 32, cy = 32, trackR = 22, dotR = 18;
  const trackPath = arcPath(cx, cy, trackR, START_DEG, endDeg);
  const fillPath = norm > 0
    ? arcPath(cx, cy, trackR, START_DEG, valueDeg)
    : '';
  const dot = polarToXY(cx, cy, dotR, valueDeg);

  const dragRef = useRef<{ startY: number; startNorm: number } | null>(null);
  const [flashing, setFlashing] = useState(false);
  const [isDragging, setIsDragging] = useState(false);
  const flashTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const fineStep = step ?? (max - min) / 100;
  const coarseStep = fineStep * 10;

  useEffect(() => {
    if (!agentDriven) return;
    setFlashing(true);
    if (flashTimer.current) clearTimeout(flashTimer.current);
    flashTimer.current = setTimeout(() => setFlashing(false), 400);
  }, [value, agentDriven]);

  const onPointerDown = useCallback((e: React.PointerEvent<SVGSVGElement>) => {
    e.preventDefault();
    // Capture on currentTarget (the SVG root) — capturing on e.target binds
    // to whatever child node the pointer landed on (fill path, dot circle).
    // If React re-renders and replaces that child mid-drag (e.g. agent edit
    // batch arrives), the captured node detaches and pointermove stops.
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    dragRef.current = { startY: e.clientY, startNorm: norm };
    setIsDragging(true);
  }, [norm]);

  const onPointerMove = useCallback((e: React.PointerEvent<SVGSVGElement>) => {
    if (!dragRef.current) return;
    const delta = (dragRef.current.startY - e.clientY) / 120;
    const newNorm = Math.max(0, Math.min(1, dragRef.current.startNorm + delta));
    onChange(min + newNorm * (max - min));
  }, [min, max, onChange]);

  const onPointerUp = useCallback(() => {
    dragRef.current = null;
    setIsDragging(false);
  }, []);

  const onKeyDown = useCallback((e: React.KeyboardEvent<HTMLDivElement>) => {
    let next: number | null = null;
    const inc = e.shiftKey ? coarseStep : fineStep;
    switch (e.key) {
      case 'ArrowUp':
      case 'ArrowRight':
        next = clamp(value + inc, min, max);
        break;
      case 'ArrowDown':
      case 'ArrowLeft':
        next = clamp(value - inc, min, max);
        break;
      case 'PageUp':
        next = clamp(value + coarseStep, min, max);
        break;
      case 'PageDown':
        next = clamp(value - coarseStep, min, max);
        break;
      case 'Home':
        next = min;
        break;
      case 'End':
        next = max;
        break;
      default:
        return;
    }
    if (next !== null) {
      e.preventDefault();
      onChange(next);
    }
  }, [value, min, max, fineStep, coarseStep, onChange]);

  const valueText = `${formatVal(value, unit, decimals)}${unit ? '' : ''}`;
  const ariaValueText = `${value.toFixed(decimals)}${spokenUnit(unit)}`;

  return (
    <div
      className={`knob-wrap${flashing ? ' knob-agent' : ''}${isDragging ? ' knob-editing' : ''}`}
      role="slider"
      tabIndex={0}
      aria-label={label}
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={value}
      aria-valuetext={ariaValueText}
      onKeyDown={onKeyDown}
    >
      <svg
        width="64"
        height="64"
        style={{ cursor: 'ns-resize', touchAction: 'none' }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        aria-hidden="true"
        focusable="false"
      >
        <path d={trackPath} fill="none" stroke="#2a2540" strokeWidth="5" strokeLinecap="round" />
        {fillPath && (
          <path d={fillPath} fill="none" stroke="#7b5cff" strokeWidth="5" strokeLinecap="round" />
        )}
        <circle cx={dot.x} cy={dot.y} r="4" fill="#f4f0ff" />
      </svg>
      <span className="knob-label">{label}</span>
      <span className="knob-value">{valueText}</span>
    </div>
  );
}
