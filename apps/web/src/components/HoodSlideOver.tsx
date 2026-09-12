import { ReactNode, useEffect, useRef } from 'react';
import './HoodSlideOver.css';

// ── HoodSlideOver (Phase A / #271) ───────────────────────────────────
//
// Slide-over wrapper that hosts the full ModulesGrid when the user
// clicks "Open the hood" on the PlaySurface. 480px wide overlay,
// translates in from left over 320ms with var(--ease-considered).
// Below 720px viewport: collapses to a full-screen modal.
//
// Dismiss paths:
//   • "Close" button top-left
//   • Esc key
//   • Click on the dimmed backdrop (outside the overlay)
//
// Honors prefers-reduced-motion — drops the slide animation for users
// who request reduced motion (instant on/off).
//
// The overlay positions itself absolutely over its container (a parent
// with position: relative). The chat dock on the right stays
// interactive — the backdrop only covers the play-surface region.

interface HoodSlideOverProps {
  open: boolean;
  onClose: () => void;
  children: ReactNode;
}

export function HoodSlideOver({ open, onClose, children }: HoodSlideOverProps) {
  const panelRef = useRef<HTMLDivElement>(null);
  const closeBtnRef = useRef<HTMLButtonElement>(null);

  // Esc to close. Window-level listener so the focus can be anywhere
  // — including back on the play-surface (which is still visible
  // through the backdrop dim). Skip when an editable element has focus
  // so we don't kill in-flight chat composition.
  useEffect(() => {
    if (!open) return;
    const handler = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return;
      const t = e.target as HTMLElement | null;
      if (
        t &&
        (t.tagName === 'INPUT' ||
          t.tagName === 'TEXTAREA' ||
          t.isContentEditable)
      ) {
        return;
      }
      e.preventDefault();
      onClose();
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [open, onClose]);

  // Focus the Close button on open so keyboard users land somewhere
  // sensible. Defer one tick so the transition has started.
  useEffect(() => {
    if (!open) return;
    const id = window.requestAnimationFrame(() => {
      closeBtnRef.current?.focus();
    });
    return () => window.cancelAnimationFrame(id);
  }, [open]);

  // Don't render anything when closed — keeps the underlying ModulesGrid
  // unmounted (no fine-grain animation thrash, no orphan listeners).
  if (!open) return null;

  return (
    <div
      className="hood-slide-over"
      role="dialog"
      aria-modal="true"
      aria-label="Full knob layout"
    >
      <button
        type="button"
        className="hood-slide-over-backdrop"
        aria-label="Close the hood"
        tabIndex={-1}
        onClick={onClose}
      />
      <div
        ref={panelRef}
        className="hood-slide-over-panel"
        role="document"
      >
        <header className="hood-slide-over-header">
          <button
            ref={closeBtnRef}
            type="button"
            className="hood-slide-over-close"
            onClick={onClose}
            aria-label="Close the hood"
          >
            Close
          </button>
        </header>
        <div className="hood-slide-over-body">{children}</div>
      </div>
    </div>
  );
}

// ── Persistence helpers ───────────────────────────────────────────────
// Per-session — sessionStorage so fresh launches always show the simple
// play surface first per the brand's first-impression contract.

const HOOD_OPEN_KEY = 'timbre:hood-open';

export function loadHoodOpen(): boolean {
  if (typeof window === 'undefined') return false;
  try {
    return window.sessionStorage.getItem(HOOD_OPEN_KEY) === '1';
  } catch {
    return false;
  }
}

export function saveHoodOpen(open: boolean): void {
  if (typeof window === 'undefined') return;
  try {
    if (open) window.sessionStorage.setItem(HOOD_OPEN_KEY, '1');
    else window.sessionStorage.removeItem(HOOD_OPEN_KEY);
  } catch {
    // private mode — silently no-op
  }
}
