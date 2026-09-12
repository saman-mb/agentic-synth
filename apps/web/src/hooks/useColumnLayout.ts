import { useCallback, useEffect, useRef, useState } from 'react';

// ── useColumnLayout (Phase 14 layout fix) ───────────────────────────
//
// Manages live-resizable column widths for the three-column app body
// (PresetsSidebar | ModulesGrid | RightColumn). Widths are written to
// CSS custom properties on a host element so the grid re-flows without
// re-rendering React on every pointer move — only the final commit
// touches localStorage.
//
// API:
//   - leftPx / rightPx: current pinned widths in px
//   - setLeftPx / setRightPx: imperative setters (used by ResizeHandle)
//   - bindHost(ref): attach the host (.app-body) so we can sync CSS vars
//
// Persistence key: `timbre:layout-cols` — { left: number, right: number }.

const STORAGE_KEY = 'timbre:layout-cols';

export interface ColumnLayout {
  leftPx: number;
  rightPx: number;
}

export interface ColumnConstraints {
  leftMin: number;
  leftMax: number;
  rightMin: number;
  rightMax: number;
  // Middle column min — used to clamp left/right so the modules grid
  // never drops below a usable width during drag.
  middleMin: number;
}

export const DEFAULT_CONSTRAINTS: ColumnConstraints = {
  leftMin: 180,
  leftMax: 360,
  rightMin: 360,
  rightMax: 900,
  middleMin: 480,
};

// Compute the default right column as 40% of the viewport width, capped
// to a sensible range. We resolve at hook init time so first paint uses
// the right proportions even before localStorage hits.
function computeDefaultRight(): number {
  if (typeof window === 'undefined') return 480;
  const vw = window.innerWidth || 1280;
  const pct = Math.round(vw * 0.4);
  return Math.max(DEFAULT_CONSTRAINTS.rightMin, Math.min(DEFAULT_CONSTRAINTS.rightMax, pct));
}

function loadStored(): ColumnLayout | null {
  if (typeof window === 'undefined') return null;
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<ColumnLayout>;
    if (
      typeof parsed.leftPx === 'number' &&
      typeof parsed.rightPx === 'number' &&
      Number.isFinite(parsed.leftPx) &&
      Number.isFinite(parsed.rightPx)
    ) {
      return { leftPx: parsed.leftPx, rightPx: parsed.rightPx };
    }
  } catch {
    // ignore — fall through to defaults
  }
  return null;
}

function persist(layout: ColumnLayout) {
  if (typeof window === 'undefined') return;
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(layout));
  } catch {
    // localStorage may throw in private mode — ignore.
  }
}

export function useColumnLayout(constraints: ColumnConstraints = DEFAULT_CONSTRAINTS) {
  const [layout, setLayout] = useState<ColumnLayout>(() => {
    const stored = loadStored();
    if (stored) return stored;
    return { leftPx: 220, rightPx: computeDefaultRight() };
  });

  // Host ref — points at the .app-body grid. We write CSS vars here on
  // every layout change so the grid re-flows without restyling siblings.
  const hostRef = useRef<HTMLDivElement | null>(null);

  // Apply CSS vars whenever the layout changes (or when the host binds).
  const applyVars = useCallback(
    (host: HTMLElement | null, l: ColumnLayout) => {
      if (!host) return;
      host.style.setProperty('--col-left-width', `${l.leftPx}px`);
      host.style.setProperty('--col-right-width', `${l.rightPx}px`);
    },
    [],
  );

  useEffect(() => {
    applyVars(hostRef.current, layout);
  }, [layout, applyVars]);

  // Persist layout on every commit. Throttle would be nice but drag
  // events already throttle via pointermove; commits land on pointerup.
  useEffect(() => {
    persist(layout);
  }, [layout]);

  // Clamp helper — honours per-side mins and a global middle-min derived
  // from the live viewport width. The handle drag uses this so the user
  // never pulls a column past usability.
  const clamp = useCallback(
    (next: ColumnLayout): ColumnLayout => {
      const vw = typeof window !== 'undefined' ? window.innerWidth : 1280;
      let l = Math.max(constraints.leftMin, Math.min(constraints.leftMax, next.leftPx));
      let r = Math.max(constraints.rightMin, Math.min(constraints.rightMax, next.rightPx));
      // Middle column must stay >= middleMin. If l + r leaves less than
      // that for the middle, push back on whichever side moved.
      const middle = vw - l - r;
      if (middle < constraints.middleMin) {
        const overflow = constraints.middleMin - middle;
        // Prefer trimming the side that grew vs current state.
        if (next.leftPx > layout.leftPx) {
          l = Math.max(constraints.leftMin, l - overflow);
        } else if (next.rightPx > layout.rightPx) {
          r = Math.max(constraints.rightMin, r - overflow);
        } else {
          // Both stable — trim right (which has more headroom).
          r = Math.max(constraints.rightMin, r - overflow);
        }
      }
      return { leftPx: l, rightPx: r };
    },
    [constraints, layout.leftPx, layout.rightPx],
  );

  const setLeftPx = useCallback(
    (px: number) => {
      setLayout((prev) => clamp({ ...prev, leftPx: px }));
    },
    [clamp],
  );

  const setRightPx = useCallback(
    (px: number) => {
      setLayout((prev) => clamp({ ...prev, rightPx: px }));
    },
    [clamp],
  );

  // Bind the .app-body host. Called once via callback ref so we get the
  // node as soon as React mounts it. Re-applies CSS vars on bind.
  const bindHost = useCallback(
    (node: HTMLDivElement | null) => {
      hostRef.current = node;
      applyVars(node, layout);
    },
    [applyVars, layout],
  );

  return {
    leftPx: layout.leftPx,
    rightPx: layout.rightPx,
    setLeftPx,
    setRightPx,
    bindHost,
    constraints,
  };
}
