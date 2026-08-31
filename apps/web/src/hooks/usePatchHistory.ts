import { useCallback, useReducer } from 'react';
import type { PatchParams } from '../components/KnobGrid';

// ─── Patch history hook ──────────────────────────────────────────────────────
//
// Linear undo/redo stack for patch state. Pushing a new entry while mid-history
// truncates the redo tail (standard text-editor semantics). Pushing the exact
// same patch twice in a row is a no-op (cheap JSON deep-equal). The very first
// entry is always `init`, so the first user/agent change lands at index 1.
//
// History is capped at MAX_ENTRIES to bound memory; oldest entries are dropped
// when the cap is exceeded (cursor is adjusted so it still points at the same
// logical entry).

const MAX_ENTRIES = 64;

export type PatchHistorySource = 'user' | 'agent' | 'variation' | 'preset' | 'init';

export interface PatchHistoryEntry {
  patch: PatchParams;
  source: PatchHistorySource;
  timestamp: number;
}

export interface UsePatchHistoryReturn {
  history: PatchHistoryEntry[];
  cursor: number;
  push: (patch: PatchParams, source: PatchHistorySource) => void;
  undo: () => PatchParams | null;
  redo: () => PatchParams | null;
  canUndo: boolean;
  canRedo: boolean;
  reset: (patch: PatchParams) => void;
}

interface State {
  history: PatchHistoryEntry[];
  cursor: number;
}

type Action =
  | { type: 'push'; patch: PatchParams; source: PatchHistorySource }
  | { type: 'undo' }
  | { type: 'redo' }
  | { type: 'reset'; patch: PatchParams };

function serialise(p: PatchParams): string {
  return JSON.stringify(p);
}

function makeInit(patch: PatchParams): State {
  return {
    history: [{ patch, source: 'init', timestamp: Date.now() }],
    cursor: 0,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'push': {
      const current = state.history[state.cursor];
      // No-op on identical consecutive push (cheap JSON deep-equal).
      if (current && serialise(current.patch) === serialise(action.patch)) {
        return state;
      }
      // Truncate redo tail at cursor, then append.
      const trimmed = state.history.slice(0, state.cursor + 1);
      trimmed.push({
        patch: action.patch,
        source: action.source,
        timestamp: Date.now(),
      });
      // Enforce cap by dropping from the head.
      const overflow = trimmed.length - MAX_ENTRIES;
      const next = overflow > 0 ? trimmed.slice(overflow) : trimmed;
      return { history: next, cursor: next.length - 1 };
    }
    case 'undo': {
      if (state.cursor <= 0) return state;
      return { history: state.history, cursor: state.cursor - 1 };
    }
    case 'redo': {
      if (state.cursor >= state.history.length - 1) return state;
      return { history: state.history, cursor: state.cursor + 1 };
    }
    case 'reset': {
      return makeInit(action.patch);
    }
    default:
      return state;
  }
}

export function usePatchHistory(initialPatch: PatchParams): UsePatchHistoryReturn {
  const [state, dispatch] = useReducer(reducer, initialPatch, makeInit);

  const push = useCallback((patch: PatchParams, source: PatchHistorySource) => {
    dispatch({ type: 'push', patch, source });
  }, []);

  // undo/redo dispatch and synchronously return the target patch by reading
  // from the same `state.history` snapshot. Components rerender on the next
  // pass with the new cursor, but the returned patch lets the caller hand the
  // rolled-back values to the audio bridge immediately.
  const undo = useCallback((): PatchParams | null => {
    if (state.cursor <= 0) return null;
    const target = state.history[state.cursor - 1];
    dispatch({ type: 'undo' });
    return target.patch;
  }, [state]);

  const redo = useCallback((): PatchParams | null => {
    if (state.cursor >= state.history.length - 1) return null;
    const target = state.history[state.cursor + 1];
    dispatch({ type: 'redo' });
    return target.patch;
  }, [state]);

  const reset = useCallback((patch: PatchParams) => {
    dispatch({ type: 'reset', patch });
  }, []);

  return {
    history: state.history,
    cursor: state.cursor,
    push,
    undo,
    redo,
    canUndo: state.cursor > 0,
    canRedo: state.cursor < state.history.length - 1,
    reset,
  };
}
