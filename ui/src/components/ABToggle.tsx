import { useEffect } from 'react';
import './ABToggle.css';

// ── ABToggle (Phase A / #267) ────────────────────────────────────────
//
// Pill-style A/B compare control, 60×24px monospace. Two halves
// labelled A and B; active half is accent, inactive is tertiary.
// Producer convention — keep the literal letters per Brand Guardian.
//
// Interaction:
//   • Click either half to make it active.
//   • Spacebar — global toggle (skipped when an editable element is
//     focused so it doesn't fight chat / rename inputs).
//   • Backslash — also toggles. `Tab` is intentionally not hijacked
//     (it's the user's keyboard-navigation tool already).
//
// State lives in the parent. This component is a controlled pill: it
// renders `active` and calls `onToggle` / `onSetActive` on input.

export type ABSide = 'A' | 'B';

interface ABToggleProps {
  active: ABSide;
  onSetActive: (side: ABSide) => void;
  onToggle: () => void;
  // When false, the toggle is rendered but visibly muted — there's
  // nothing to compare against yet, so a click still works but the
  // affordance reads as "ready, not yet active".
  ready?: boolean;
  // Optional aria label override for callers that want to clarify
  // context (e.g. mid-chat "A/B compare current vs previous patch").
  ariaLabel?: string;
}

export function ABToggle({
  active,
  onSetActive,
  onToggle,
  ready = true,
  ariaLabel,
}: ABToggleProps) {
  // Global keyboard listeners — backslash + spacebar both toggle.
  // Skip when an editable element has focus (chat textarea, macro
  // rename input, etc.) so we never steal characters from typing.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      // Backslash only — spacebar is already handled in App.tsx for the
      // legacy TopBar A/B slots and we don't want a double-toggle.
      if (e.key !== '\\') return;
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
      onToggle();
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [onToggle]);

  return (
    <div
      className={`ab-toggle${ready ? '' : ' ab-toggle--muted'}`}
      role="group"
      aria-label={ariaLabel ?? 'A/B compare'}
    >
      <button
        type="button"
        className={`ab-toggle-half${active === 'A' ? ' is-active' : ''}`}
        aria-pressed={active === 'A'}
        aria-label="Select A"
        onClick={() => onSetActive('A')}
        title="A — current"
      >
        A
      </button>
      <span className="ab-toggle-divider" aria-hidden="true" />
      <button
        type="button"
        className={`ab-toggle-half${active === 'B' ? ' is-active' : ''}`}
        aria-pressed={active === 'B'}
        aria-label="Select B"
        onClick={() => onSetActive('B')}
        title="B — previous"
      >
        B
      </button>
    </div>
  );
}
