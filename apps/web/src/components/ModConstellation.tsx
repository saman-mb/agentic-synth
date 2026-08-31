import React, { useMemo, useState } from 'react';
import {
  ModConnection,
  MOD_SOURCE_LABELS,
  MOD_SOURCE_VARS,
  ModSourceId,
  destinationLabel,
} from '../data/modulation';
import './ModConstellation.css';

// ── ModConstellation (Phase 8 — REBRAND.md §12 wow #2) ──────────────
//
// SVG view of the modulation routes as a star-field. Sources are
// bright nodes on the left, destinations dim nodes on the right.
// Connections are cubic-Bezier curves stroked in the source's mod
// color, with an animated stroke-dashoffset to suggest signal flow.
// Hovering a curve reveals the amount mid-curve; clicking handles
// back to the list view (handled by parent).

interface ModConstellationProps {
  connections: ModConnection[];
  highlightedId: string | null;
  onSelectConnection: (id: string) => void;
  onUpdateConnection: (id: string, patch: Partial<ModConnection>) => void;
}

// All ten sources always render on the left so the user has somewhere
// to drop a future connection visually — empty sources just look idle.
const SOURCE_ORDER: ModSourceId[] = [
  'lfo1', 'lfo2', 'env1', 'env2',
  'macro1', 'macro2', 'macro3', 'macro4',
  'velocity', 'keytrack',
];

const VIEW_W = 460;
const VIEW_H = 280;
const PAD_X = 24;
const SRC_X = PAD_X + 8;
const DST_X = VIEW_W - PAD_X - 8;

export function ModConstellation({
  connections,
  highlightedId,
  onSelectConnection,
  onUpdateConnection,
}: ModConstellationProps) {
  // Hover-tracked connection id for the mid-curve amount badge.
  const [hoverId, setHoverId] = useState<string | null>(null);

  // Layout: distribute source nodes evenly down the left side, and
  // unique destinations down the right side. Re-derive each render
  // (cheap — at most ~60 items).
  const sourcePositions = useMemo(() => {
    const step = (VIEW_H - 2 * PAD_X) / Math.max(1, SOURCE_ORDER.length - 1);
    const map: Record<ModSourceId, { x: number; y: number }> = {} as Record<
      ModSourceId,
      { x: number; y: number }
    >;
    SOURCE_ORDER.forEach((id, i) => {
      map[id] = { x: SRC_X, y: PAD_X + i * step };
    });
    return map;
  }, []);

  const destPositions = useMemo(() => {
    // Stable order: unique destinations in the order they first appear
    // in `connections`. Keeps the layout from twitching when amounts
    // change but the topology hasn't.
    const seen: string[] = [];
    for (const c of connections) {
      if (!seen.includes(c.destination)) seen.push(c.destination);
    }
    const map: Record<string, { x: number; y: number }> = {};
    if (seen.length === 0) return map;
    const step = (VIEW_H - 2 * PAD_X) / Math.max(1, seen.length - 1);
    seen.forEach((dst, i) => {
      map[dst] = { x: DST_X, y: PAD_X + (seen.length === 1 ? (VIEW_H / 2 - PAD_X) : i * step) };
    });
    return map;
  }, [connections]);

  if (connections.length === 0) {
    return (
      <div className="mod-constellation-empty">
        <p>No connections yet. Drag a source dot from the bar onto any knob to begin.</p>
        <ConstellationSky sourcePositions={sourcePositions} />
      </div>
    );
  }

  return (
    <div className="mod-constellation-wrap">
      <svg
        className="mod-constellation"
        viewBox={`0 0 ${VIEW_W} ${VIEW_H}`}
        preserveAspectRatio="xMidYMid meet"
        role="img"
        aria-label="Modulation constellation"
      >
        {/* Background — faint star field for cinematic depth. Drawn from
            seeded random in render so it's stable across renders. */}
        <StarField />

        {/* Connection threads — drawn under the nodes so the bright
            source/dest dots remain readable on top. */}
        {connections.map((c) => {
          const src = sourcePositions[c.source];
          const dst = destPositions[c.destination];
          if (!src || !dst) return null;
          return (
            <ConnectionThread
              key={c.id}
              conn={c}
              src={src}
              dst={dst}
              highlighted={c.id === highlightedId || c.id === hoverId}
              onHover={setHoverId}
              onClick={() => onSelectConnection(c.id)}
            />
          );
        })}

        {/* Source nodes — render only those used so the visible side
            stays clean. Unused ones live in the empty/sky background. */}
        {Object.entries(sourcePositions).map(([id, p]) => {
          const used = connections.some((c) => c.source === id);
          return <SourceNode key={id} id={id as ModSourceId} x={p.x} y={p.y} active={used} />;
        })}

        {/* Destination nodes */}
        {Object.entries(destPositions).map(([dst, p]) => (
          <DestNode key={dst} dst={dst} x={p.x} y={p.y} />
        ))}

        {/* Mid-curve amount readouts on hover/highlight. */}
        {connections.map((c) => {
          if (c.id !== hoverId && c.id !== highlightedId) return null;
          const src = sourcePositions[c.source];
          const dst = destPositions[c.destination];
          if (!src || !dst) return null;
          const mx = (src.x + dst.x) / 2;
          const my = (src.y + dst.y) / 2;
          return (
            <g key={`amt-${c.id}`} className="mod-constellation-amount">
              <rect x={mx - 22} y={my - 10} width={44} height={20} rx={10} />
              <text x={mx} y={my + 4} textAnchor="middle">
                {c.amount.toFixed(2)}
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
}

// ─────────────────────────────────────────────────────────────────────
// Sub-components
// ─────────────────────────────────────────────────────────────────────

interface ConnectionThreadProps {
  conn: ModConnection;
  src: { x: number; y: number };
  dst: { x: number; y: number };
  highlighted: boolean;
  onHover: (id: string | null) => void;
  onClick: () => void;
}

function ConnectionThread({
  conn,
  src,
  dst,
  highlighted,
  onHover,
  onClick,
}: ConnectionThreadProps) {
  // Cubic Bezier with horizontal control points so curves bow outward
  // — keeps multiple lines visually distinct.
  const dx = (dst.x - src.x) * 0.55;
  const d = `M ${src.x} ${src.y} C ${src.x + dx} ${src.y}, ${dst.x - dx} ${dst.y}, ${dst.x} ${dst.y}`;
  const cssVar = MOD_SOURCE_VARS[conn.source];
  const strokeWidth = highlighted ? 2.4 : 1.6;
  const opacity = conn.enabled ? (highlighted ? 0.95 : 0.7) : 0.25;

  return (
    <g
      className={`mod-constellation-thread${highlighted ? ' is-highlighted' : ''}${
        conn.enabled ? '' : ' is-disabled'
      }`}
      onMouseEnter={() => onHover(conn.id)}
      onMouseLeave={() => onHover(null)}
      onClick={onClick}
      role="button"
      aria-label={`${MOD_SOURCE_LABELS[conn.source]} to ${destinationLabel(conn.destination)} at ${conn.amount.toFixed(2)}`}
    >
      {/* Wider transparent hit area — easier to mouse onto a 1.6px curve. */}
      <path d={d} stroke="transparent" strokeWidth={12} fill="none" />
      <path
        className="mod-constellation-thread-line"
        d={d}
        stroke={`var(${cssVar})`}
        strokeWidth={strokeWidth}
        strokeLinecap="round"
        fill="none"
        opacity={opacity}
      />
    </g>
  );
}

interface SourceNodeProps {
  id: ModSourceId;
  x: number;
  y: number;
  active: boolean;
}

function SourceNode({ id, x, y, active }: SourceNodeProps) {
  const cssVar = MOD_SOURCE_VARS[id];
  return (
    <g className={`mod-constellation-src${active ? ' is-active' : ''}`}>
      <circle
        cx={x}
        cy={y}
        r={active ? 5 : 3.5}
        fill={`var(${cssVar})`}
      />
      {/* Soft outer pulse only on active sources. */}
      {active && (
        <circle
          cx={x}
          cy={y}
          r={9}
          fill="none"
          stroke={`var(${cssVar})`}
          strokeWidth={1}
          opacity={0.4}
          className="mod-constellation-src-pulse"
        />
      )}
      <text x={x - 10} y={y + 3} textAnchor="end" className="mod-constellation-srclabel">
        {MOD_SOURCE_LABELS[id]}
      </text>
    </g>
  );
}

interface DestNodeProps {
  dst: string;
  x: number;
  y: number;
}

function DestNode({ dst, x, y }: DestNodeProps) {
  return (
    <g className="mod-constellation-dst">
      <circle cx={x} cy={y} r={3.5} />
      <text x={x + 10} y={y + 3} textAnchor="start" className="mod-constellation-dstlabel">
        {destinationLabel(dst)}
      </text>
    </g>
  );
}

function ConstellationSky({
  sourcePositions,
}: {
  sourcePositions: Record<ModSourceId, { x: number; y: number }>;
}) {
  return (
    <svg
      className="mod-constellation mod-constellation-sky"
      viewBox={`0 0 ${VIEW_W} ${VIEW_H}`}
      preserveAspectRatio="xMidYMid meet"
      aria-hidden="true"
    >
      <StarField />
      {Object.entries(sourcePositions).map(([id, p]) => (
        <SourceNode key={id} id={id as ModSourceId} x={p.x} y={p.y} active={false} />
      ))}
    </svg>
  );
}

// Static (seeded) starfield — 28 tiny dots. Position is deterministic
// per index so renders don't flicker.
function StarField() {
  // Simple deterministic pseudo-random based on index — gives a stable
  // sprinkle across the canvas without bringing in a seedable RNG.
  const stars = useMemo(() => {
    const out: { x: number; y: number; r: number; o: number }[] = [];
    for (let i = 0; i < 28; i++) {
      const a = Math.sin(i * 12.9898) * 43758.5453;
      const b = Math.sin(i * 78.233) * 43758.5453;
      const fx = a - Math.floor(a);
      const fy = b - Math.floor(b);
      out.push({
        x: fx * VIEW_W,
        y: fy * VIEW_H,
        r: 0.6 + ((i * 7) % 3) * 0.3,
        o: 0.15 + ((i * 11) % 5) * 0.06,
      });
    }
    return out;
  }, []);
  return (
    <g className="mod-constellation-stars" aria-hidden="true">
      {stars.map((s, i) => (
        <circle key={i} cx={s.x} cy={s.y} r={s.r} fill="white" opacity={s.o} />
      ))}
    </g>
  );
}
