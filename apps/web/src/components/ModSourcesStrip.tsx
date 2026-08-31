import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  MOD_SOURCES,
  MOD_SOURCE_LABELS,
  MOD_SOURCE_VARS,
  ModSourceId,
  setDragSource,
} from '../data/modulation';
import './ModSourcesStrip.css';

// ── ModSourcesStrip (Phase 8) ────────────────────────────────────────
//
// Renders the ten modulation sources as small colored dots. Each dot is
// pointer-draggable: on pointerdown we set window.__timbreDragSource +
// dispatch a window event ('timbre:moddrag:start') that Knob components
// listen to so they can light up as drop targets. While dragging, a
// ghost follower div tracks the cursor. On pointerup we clear the
// global state and dispatch 'timbre:moddrag:end'.
//
// Macros 1-4 also live in MacroBar as full-size knobs; here they
// double as draggable source dots so the user can grab them from
// either place.

interface ModSourcesStripProps {
  // Subset filter — defaults to all ten. Callers can pass a smaller
  // slice when embedding (e.g. excluding macros if they're rendered
  // elsewhere as draggable knobs).
  sources?: ModSourceId[];
}

export function ModSourcesStrip({ sources = MOD_SOURCES }: ModSourcesStripProps) {
  return (
    <div className="mod-sources-strip" role="group" aria-label="Modulation sources">
      <span className="mod-sources-heading">MOD</span>
      {sources.map((id) => (
        <ModSourceDot key={id} id={id} />
      ))}
    </div>
  );
}

interface ModSourceDotProps {
  id: ModSourceId;
  // Optional override for label visibility (some embeddings prefer
  // tooltip-only). Defaults to title attribute.
  size?: 'sm' | 'md';
}

export function ModSourceDot({ id, size = 'sm' }: ModSourceDotProps) {
  const [dragging, setDragging] = useState(false);
  const ghostRef = useRef<HTMLDivElement | null>(null);
  const draggingRef = useRef(false);

  // The ghost div mirrors the dragged dot under the cursor. We create it
  // lazily on pointerdown and remove it on pointerup so idle DOM stays
  // clean. Position is updated via direct style mutation (no React
  // re-render) to keep the follower silky at 60fps.
  const startDrag = useCallback(
    (e: React.PointerEvent) => {
      if (e.button !== 0) return;
      e.preventDefault();
      setDragSource(id);
      draggingRef.current = true;
      setDragging(true);
      window.dispatchEvent(new CustomEvent('timbre:moddrag:start', { detail: { source: id } }));

      const ghost = document.createElement('div');
      ghost.className = `mod-source-ghost mod-source-${id}`;
      ghost.style.left = `${e.clientX}px`;
      ghost.style.top = `${e.clientY}px`;
      document.body.appendChild(ghost);
      ghostRef.current = ghost;

      const onMove = (ev: PointerEvent) => {
        if (!ghostRef.current) return;
        ghostRef.current.style.left = `${ev.clientX}px`;
        ghostRef.current.style.top = `${ev.clientY}px`;
      };
      const onUp = () => {
        // Knob drop handler runs on its OWN pointerup before this global
        // listener (capture order). By the time we get here, any drop
        // has already fired onAssignMod. Just clean up.
        setDragSource(null);
        draggingRef.current = false;
        setDragging(false);
        if (ghostRef.current) {
          ghostRef.current.remove();
          ghostRef.current = null;
        }
        window.dispatchEvent(new CustomEvent('timbre:moddrag:end'));
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup', onUp);
      };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
    },
    [id],
  );

  // Safety: if the component unmounts mid-drag, scrub global state.
  useEffect(
    () => () => {
      if (draggingRef.current) {
        setDragSource(null);
        if (ghostRef.current) ghostRef.current.remove();
        window.dispatchEvent(new CustomEvent('timbre:moddrag:end'));
      }
    },
    [],
  );

  const cssVar = MOD_SOURCE_VARS[id];
  const label = MOD_SOURCE_LABELS[id];
  return (
    <button
      type="button"
      className={`mod-source-dot mod-source-${id} mod-source-${size}${dragging ? ' is-dragging' : ''}`}
      title={`${label} — drag onto a knob to modulate`}
      aria-label={`Modulation source ${label}`}
      onPointerDown={startDrag}
      style={{ ['--mod-color' as string]: `var(${cssVar})` }}
    >
      <span className="mod-source-glyph" aria-hidden="true" />
      <span className="mod-source-label">{compactLabel(id)}</span>
    </button>
  );
}

function compactLabel(id: ModSourceId): string {
  switch (id) {
    case 'lfo1': return 'L1';
    case 'lfo2': return 'L2';
    case 'env1': return 'E1';
    case 'env2': return 'E2';
    case 'macro1': return 'M1';
    case 'macro2': return 'M2';
    case 'macro3': return 'M3';
    case 'macro4': return 'M4';
    case 'velocity': return 'Vel';
    case 'keytrack': return 'Key';
  }
}
