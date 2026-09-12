import React, { useState, useEffect, useCallback, useRef } from 'react';

interface KnobProps {
  label: string;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  onChange?: (value: number) => void;
  disabled?: boolean;
}

const Knob: React.FC<KnobProps> = ({ label, value, min = 0, max = 1, step = 0.01, onChange, disabled }) => {
  const [dragging, setDragging] = useState(false);
  const startY = useRef(0);
  const startVal = useRef(0);
  const pct = ((value - min) / (max - min)) * 100;

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    if (disabled) return;
    setDragging(true);
    startY.current = e.clientY;
    startVal.current = value;
  }, [value, disabled]);

  useEffect(() => {
    if (!dragging) return;
    const handleMove = (e: MouseEvent) => {
      const delta = (startY.current - e.clientY) / 200;
      const range = max - min;
      const newVal = Math.min(max, Math.max(min, startVal.current + delta * range));
      const stepped = Math.round(newVal / step) * step;
      onChange?.(stepped);
    };
    const handleUp = () => setDragging(false);
    window.addEventListener('mousemove', handleMove);
    window.addEventListener('mouseup', handleUp);
    return () => {
      window.removeEventListener('mousemove', handleMove);
      window.removeEventListener('mouseup', handleUp);
    };
  }, [dragging, min, max, step, onChange]);

  // SVG arc: 135° sweep from -135° to +135°
  const r = 36;
  const cx = 40;
  const cy = 44;
  const startAngle = -135 * (Math.PI / 180);
  const endAngle = (pct / 100) * 270 * (Math.PI / 180) + startAngle;
  const x1 = cx + r * Math.cos(startAngle);
  const y1 = cy + r * Math.sin(startAngle);
  const x2 = cx + r * Math.cos(endAngle);
  const y2 = cy + r * Math.sin(endAngle);
  const largeArc = pct > 50 ? 1 : 0;

  return (
    <div style={{ textAlign: 'center', width: 80, opacity: disabled ? 0.4 : 1 }}>
      <svg width={80} height={80} onMouseDown={handleMouseDown} style={{ cursor: disabled ? 'default' : 'pointer' }}>
        {/* Track */}
        <path d={`M ${x1} ${y1} A ${r} ${r} 0 1 0 ${40 - r * 0.707} ${44 + r * 0.707}`}
          fill="none" stroke="#333" strokeWidth={6} strokeLinecap="round" />
        {/* Value arc */}
        {pct > 0 && (
          <path d={`M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2}`}
            fill="none" stroke="#4fc3f7" strokeWidth={6} strokeLinecap="round" />
        )}
        {/* Center dot */}
        <circle cx={cx} cy={cy} r={4} fill="#fff" />
        {/* Value indicator */}
        <text x={cx} y={cy + 22} textAnchor="middle" fill="#999" fontSize={10}>
          {typeof value === 'number' ? value.toFixed(2) : value}
        </text>
      </svg>
      <div style={{ color: '#ccc', fontSize: 11, marginTop: -4 }}>{label}</div>
    </div>
  );
};

interface KnobGridProps {
  parameters: Record<string, number>;
  onChange?: (key: string, value: number) => void;
  disabled?: boolean;
}

export const KnobGrid: React.FC<KnobGridProps> = ({ parameters, onChange, disabled }) => {
  const paramConfig: Record<string, { min: number; max: number; step: number }> = {
    'cutoff': { min: 20, max: 18000, step: 10 },
    'resonance': { min: 0, max: 1, step: 0.01 },
    'attack': { min: 1, max: 5000, step: 1 },
    'decay': { min: 1, max: 5000, step: 1 },
    'sustain': { min: 0, max: 1, step: 0.01 },
    'release': { min: 1, max: 10000, step: 1 },
    'lfo_rate': { min: 0.1, max: 30, step: 0.1 },
    'lfo_depth': { min: 0, max: 1, step: 0.01 },
  };

  const labels: Record<string, string> = {
    cutoff: 'Cutoff',
    resonance: 'Resonance',
    attack: 'Attack',
    decay: 'Decay',
    sustain: 'Sustain',
    release: 'Release',
    lfo_rate: 'LFO Rate',
    lfo_depth: 'LFO Depth',
  };

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: 'repeat(auto-fill, 80px)',
      gap: 8,
      padding: 12,
      justifyContent: 'center',
    }}>
      {Object.entries(paramConfig).map(([key, cfg]) => (
        <Knob
          key={key}
          label={labels[key] || key}
          value={(parameters[key] as number) ?? cfg.min}
          min={cfg.min}
          max={cfg.max}
          step={cfg.step}
          disabled={disabled}
          onChange={(v) => onChange?.(key, v)}
        />
      ))}
    </div>
  );
};

export default KnobGrid;
