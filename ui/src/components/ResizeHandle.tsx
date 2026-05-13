import { PointerEvent, useCallback, useRef, useState } from 'react';
import './ResizeHandle.css';

// ── ResizeHandle ─────────────────────────────────────────────────────
//
// Vertical drag bar used between the three columns of .app-body.
// 6px hit-target, 1px hairline on hover/active (var(--accent-primary)).
// Pointer capture pattern: pointerdown grabs the pointer; pointermove
// reports a continuous delta to the parent via onDrag; pointerup
// releases capture so the next drag starts fresh.
//
// The parent owns the column width state — this component is purely
// stateless w.r.t. the layout. It only tracks an `active` flag for
// styling.

interface ResizeHandleProps {
  ariaLabel: string;
  // Direction of drag: 'left' = handle sits on the LEFT edge of a column
  // (e.g. between left sidebar and middle), 'right' = on the RIGHT edge.
  // Affects which delta sign grows the column the parent is sizing.
  // For our app: left-handle drives leftPx (delta = clientX - startX);
  // right-handle drives rightPx (delta = startX - clientX, because the
  // right column grows as the cursor moves leftward).
  side: 'left' | 'right';
  // Current width controlled by this handle, in px.
  currentPx: number;
  // Apply a new width. Parent clamps + persists.
  onResize: (nextPx: number) => void;
  // Called once on pointerup so the parent can persist/commit.
  onCommit?: () => void;
}

export function ResizeHandle({
  ariaLabel,
  side,
  currentPx,
  onResize,
  onCommit,
}: ResizeHandleProps) {
  const [active, setActive] = useState(false);
  const startXRef = useRef(0);
  const startPxRef = useRef(0);

  const handlePointerDown = useCallback(
    (e: PointerEvent<HTMLDivElement>) => {
      // Only respond to primary button / touch / pen. Wheel-button drag
      // would surprise the user.
      if (e.button !== 0 && e.pointerType === 'mouse') return;
      e.preventDefault();
      (e.target as HTMLDivElement).setPointerCapture(e.pointerId);
      setActive(true);
      startXRef.current = e.clientX;
      startPxRef.current = currentPx;
    },
    [currentPx],
  );

  const handlePointerMove = useCallback(
    (e: PointerEvent<HTMLDivElement>) => {
      if (!active) return;
      const dx = e.clientX - startXRef.current;
      const next = side === 'left' ? startPxRef.current + dx : startPxRef.current - dx;
      onResize(next);
    },
    [active, side, onResize],
  );

  const handlePointerUp = useCallback(
    (e: PointerEvent<HTMLDivElement>) => {
      if (!active) return;
      (e.target as HTMLDivElement).releasePointerCapture(e.pointerId);
      setActive(false);
      if (onCommit) onCommit();
    },
    [active, onCommit],
  );

  return (
    <div
      className={`resize-handle${active ? ' is-active' : ''}`}
      role="separator"
      aria-orientation="vertical"
      aria-label={ariaLabel}
      tabIndex={-1}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
    >
      <span className="resize-handle-line" aria-hidden="true" />
    </div>
  );
}
