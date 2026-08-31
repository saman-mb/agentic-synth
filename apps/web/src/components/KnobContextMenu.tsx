import { useEffect, useRef } from 'react';
import './KnobContextMenu.css';

// Phase G / #262 — right-click menu on every Knob. Three actions:
//   • Learn MIDI       — enter learn-mode for this knob.
//   • Clear mapping    — drop any captured CC for this knob.
//   • Show mapping     — toast / console line listing the current CC.
//
// Musician register (Phase 30 audit): "Learn MIDI", not "Map controller".
// Brand voice keeps it tactile — the producer is teaching the box, not
// configuring a controller assignment.

export interface KnobContextMenuProps {
  x: number;
  y: number;
  knobId: string;
  // Current mapping (if any) — used to label "Clear mapping" / "Show mapping"
  // intelligently. nullable so the menu still renders without a known map
  // (e.g. before the initial get_midi_mappings round-trip completes).
  currentMapping?: { cc: number; channel: number } | null;
  onLearn: (knobId: string) => void;
  onClear: (knobId: string) => void;
  onShow: (knobId: string) => void;
  onClose: () => void;
}

export function KnobContextMenu({
  x,
  y,
  knobId,
  currentMapping,
  onLearn,
  onClear,
  onShow,
  onClose,
}: KnobContextMenuProps) {
  const menuRef = useRef<HTMLDivElement | null>(null);

  // Dismiss on outside click / Escape / scroll. Re-anchored each open so
  // we don't accumulate listeners across multiple knob right-clicks.
  useEffect(() => {
    const onDocPointer = (e: PointerEvent) => {
      if (!menuRef.current) return;
      if (!menuRef.current.contains(e.target as Node)) onClose();
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    document.addEventListener('pointerdown', onDocPointer);
    document.addEventListener('keydown', onKey);
    window.addEventListener('scroll', onClose, { capture: true });
    return () => {
      document.removeEventListener('pointerdown', onDocPointer);
      document.removeEventListener('keydown', onKey);
      window.removeEventListener('scroll', onClose, { capture: true });
    };
  }, [onClose]);

  // Clamp to viewport so the menu never opens off-screen.
  const clampedStyle: React.CSSProperties = (() => {
    const w = 180;
    const h = 132;
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const left = Math.min(x, Math.max(0, vw - w - 8));
    const top = Math.min(y, Math.max(0, vh - h - 8));
    return { left, top };
  })();

  const hasMapping = currentMapping !== null && currentMapping !== undefined;

  return (
    <div
      ref={menuRef}
      className="knob-context-menu"
      role="menu"
      aria-label={`MIDI actions for ${knobId}`}
      style={clampedStyle}
    >
      <button
        type="button"
        role="menuitem"
        className="knob-context-item"
        onClick={() => {
          onLearn(knobId);
          onClose();
        }}
      >
        Learn MIDI
      </button>
      <button
        type="button"
        role="menuitem"
        className="knob-context-item"
        disabled={!hasMapping}
        onClick={() => {
          onClear(knobId);
          onClose();
        }}
      >
        {hasMapping ? `Clear CC${currentMapping?.cc}` : 'Clear mapping'}
      </button>
      <button
        type="button"
        role="menuitem"
        className="knob-context-item"
        disabled={!hasMapping}
        onClick={() => {
          onShow(knobId);
          onClose();
        }}
      >
        Show mapping
      </button>
    </div>
  );
}
