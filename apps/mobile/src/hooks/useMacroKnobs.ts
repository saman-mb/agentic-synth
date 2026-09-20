import { type RefObject, useCallback, useRef, useState } from 'react';
import type { PatchParams } from '@agentic-synth/shared-types';
import type { SynthEngine } from '@agentic-synth/engine-bridge';

import {
  clampMacroPositions,
  MACRO_DEFAULTS,
  projectMacroPatch,
} from '../macros/macroProjection';

export function useMacroKnobs(engineRef: RefObject<SynthEngine | null>) {
  const basePatchRef = useRef<PatchParams | null>(null);
  const [positions, setPositions] = useState<number[]>([...MACRO_DEFAULTS]);
  const draggingRef = useRef(false);

  const bindBasePatch = useCallback((patch: PatchParams, starts?: number[]) => {
    basePatchRef.current = patch;
    const clamped = clampMacroPositions(starts ?? [...MACRO_DEFAULTS]);
    setPositions(clamped);
    return clamped;
  }, []);

  const applyPositions = useCallback(
    (next: number[]) => {
      const engine = engineRef.current;
      const base = basePatchRef.current;
      if (!engine || !base) return;
      const clamped = clampMacroPositions(next);
      setPositions(clamped);
      engine.setPatch(projectMacroPatch(base, clamped));
    },
    [engineRef],
  );

  const setMacro = useCallback(
    (index: number, value: number) => {
      const next = [...positions];
      next[index] = value;
      applyPositions(next);
    },
    [applyPositions, positions],
  );

  const onDragStart = useCallback(() => {
    draggingRef.current = true;
  }, []);

  const onDragEnd = useCallback(() => {
    draggingRef.current = false;
  }, []);

  const resetDrag = useCallback(() => {
    draggingRef.current = false;
  }, []);

  return {
    positions,
    setMacro,
    bindBasePatch,
    onDragStart,
    onDragEnd,
    resetDrag,
    isDragging: () => draggingRef.current,
    getBasePatch: () => basePatchRef.current,
  };
}

export type MacroKnobsApi = ReturnType<typeof useMacroKnobs>;
