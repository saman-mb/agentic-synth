import { useCallback, useRef, useState } from 'react';
import type { PatchParams } from '../components/KnobGrid';

// ── useABPatch (Phase A / #267) ──────────────────────────────────────
//
// Two-slot A/B compare for the play surface. Holds [currentPatch,
// previousPatch] and exposes a toggle that swaps which one is the
// "active" patch fed to the audio thread. Held MIDI notes survive the
// toggle because the existing audio dispatch (App.tsx → effectivePatch
// effect) only diffs knob_tweak frames; voice state is preserved.
//
// Lifecycle:
//   • observe(patch) — call whenever a new patch lands from the agent /
//     a variation click / etc. Shifts: previousPatch = currentPatch,
//     currentPatch = patch.
//   • toggle() — swap which slot is "active" (consumer reads .active).
//   • reset() — clear both slots (called on new prompt submission).
//
// The hook itself does NOT push patches to the engine. App.tsx already
// owns the bridge; we just track which patch the user wants audible and
// rely on the caller to wire that through.

export type ABSlotSide = 'current' | 'previous';

export interface UseABPatchApi {
  active: ABSlotSide;
  currentPatch: PatchParams | null;
  previousPatch: PatchParams | null;
  // The patch the audio thread should currently be playing. Equals
  // currentPatch when active === 'current', else previousPatch (with a
  // currentPatch fallback when previousPatch is still null).
  activePatch: PatchParams | null;
  observe: (patch: PatchParams) => void;
  toggle: () => void;
  setActive: (side: ABSlotSide) => void;
  reset: () => void;
  // True iff both slots are populated and differ — gates whether the
  // toggle is meaningful (no point comparing a patch to nothing).
  canCompare: boolean;
}

function patchesEqualRef(a: PatchParams | null, b: PatchParams | null): boolean {
  return a === b;
}

export function useABPatch(): UseABPatchApi {
  const [currentPatch, setCurrentPatch] = useState<PatchParams | null>(null);
  const [previousPatch, setPreviousPatch] = useState<PatchParams | null>(null);
  const [active, setActiveState] = useState<ABSlotSide>('current');

  // Suppress the next observe() call after a toggle / setActive — when
  // App.tsx pushes the toggled patch back through its standard path it
  // will land here as a "new" current; without this we'd shift previous
  // → current and the slot history would corrupt on every toggle.
  const suppressNextObserveRef = useRef(false);

  const observe = useCallback((patch: PatchParams) => {
    if (suppressNextObserveRef.current) {
      suppressNextObserveRef.current = false;
      return;
    }
    setCurrentPatch((prevCurrent) => {
      // No-op when the same identity flows back in (React strict-mode
      // double-render guard).
      if (prevCurrent === patch) return prevCurrent;
      setPreviousPatch(prevCurrent);
      return patch;
    });
    // A fresh patch always re-anchors the comparison to 'current' so
    // the producer hears the new thing they just generated.
    setActiveState('current');
  }, []);

  const toggle = useCallback(() => {
    setActiveState((s) => (s === 'current' ? 'previous' : 'current'));
    suppressNextObserveRef.current = true;
  }, []);

  const setActive = useCallback((side: ABSlotSide) => {
    setActiveState((cur) => {
      if (cur === side) return cur;
      suppressNextObserveRef.current = true;
      return side;
    });
  }, []);

  const reset = useCallback(() => {
    setCurrentPatch(null);
    setPreviousPatch(null);
    setActiveState('current');
    suppressNextObserveRef.current = false;
  }, []);

  const activePatch =
    active === 'current'
      ? currentPatch
      : previousPatch ?? currentPatch;

  const canCompare =
    currentPatch !== null && previousPatch !== null && !patchesEqualRef(currentPatch, previousPatch);

  return {
    active,
    currentPatch,
    previousPatch,
    activePatch,
    observe,
    toggle,
    setActive,
    reset,
    canCompare,
  };
}
