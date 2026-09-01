import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { SynthEngine } from '@agentic-synth/engine-bridge';
import type { PatchParams } from '@agentic-synth/shared-types';
import demoPatchJson from '../../assets/demo-patch.json';
import { crossfadePatches } from '../audio/crossfade';
import { bootDemoPatch, createMobileEngine, type EngineBackend } from '../engine/createMobileEngine';
import { useMacroKnobs } from './useMacroKnobs';
import { useSayCapture } from './useSayCapture';
import { macroPositionsForKeep, projectMacroPatch } from '../macros/macroProjection';
import { runMobileGenerateFlow } from '../services/mobileGenerateFlow';
import {
  createMemoryStorage,
  getAsyncStorage,
  savePreset,
  type PresetStorage,
} from '../services/presetStore';
import { fetchVariation, type VariationItem } from '../services/variationFlow';
import { INITIAL_SESSION, type MobileSession } from '../state/mobileState';
import { bootToHear, transitionSession } from '../state/mobileStateMachine';
import {
  defaultKeepName,
  EMPTY_SCRATCH,
  type SessionScratch,
} from '../state/sessionScratch';

const DEMO_NOTE = 60;

function applyProjectedPatch(engine: SynthEngine, base: PatchParams, macros: number[]): void {
  engine.setPatch(projectMacroPatch(base, macros));
  void engine.ensureStarted();
  engine.noteOn(DEMO_NOTE, 100);
}

export function useMobileApp() {
  const engineRef = useRef<SynthEngine | null>(null);
  const crossfadeToken = useRef(0);
  const storageRef = useRef<PresetStorage | null>(null);
  const [session, setSession] = useState<MobileSession>(INITIAL_SESSION);
  const [scratch, setScratch] = useState<SessionScratch>(EMPTY_SCRATCH);
  const [backend, setBackend] = useState<EngineBackend>('mock');
  const [scopeSamples, setScopeSamples] = useState<number[]>([]);
  const [generating, setGenerating] = useState(false);
  const [variationLoading, setVariationLoading] = useState(false);
  const [keeping, setKeeping] = useState(false);
  const sayCapture = useSayCapture();
  const macroKnobs = useMacroKnobs(engineRef);
  const demoPatch = demoPatchJson as PatchParams;

  useEffect(() => {
    void (async () => {
      storageRef.current = (await getAsyncStorage()) ?? createMemoryStorage();
    })();
  }, []);

  useEffect(() => {
    let disposed = false;
    const { engine, backend: resolvedBackend } = createMobileEngine();
    engineRef.current = engine;
    setBackend(resolvedBackend);

    void (async () => {
      try {
        await bootDemoPatch(engine, demoPatch, DEMO_NOTE, 100);
        if (!disposed) {
          setSession((s) => bootToHear(s, 'Demo patch · offline ready'));
        }
      } catch (err) {
        if (!disposed) {
          setSession((s) => ({
            ...s,
            state: 'error',
            returnState: 'idle',
            statusMessage: err instanceof Error ? err.message : 'Engine boot failed',
            isPlaying: false,
          }));
        }
      }
    })();

    return () => {
      disposed = true;
      engine.dispose();
      engineRef.current = null;
    };
  }, [demoPatch]);

  useEffect(() => {
    if (!session.isPlaying) {
      setScopeSamples([]);
      return undefined;
    }
    const id = setInterval(() => {
      const engine = engineRef.current;
      if (!engine) return;
      setScopeSamples(engine.getScopeSamples(64));
    }, 1000 / 30);
    return () => clearInterval(id);
  }, [session.isPlaying]);

  const loadPatchSession = useCallback(
    async (
      patch: PatchParams,
      prompt: string,
      brief: string,
      variationIndex = 0,
      seed?: number,
    ) => {
      const engine = engineRef.current;
      if (!engine) return;
      macroKnobs.resetDrag();
      const positions = macroKnobs.bindBasePatch(patch);
      applyProjectedPatch(engine, patch, positions);
      const variation: VariationItem = {
        index: variationIndex,
        patch,
        seed,
        source: seed !== undefined ? 'local' : 'api',
      };
      setScratch({
        prompt,
        brief,
        basePatch: patch,
        variations: [variation],
        selectedVariationIndex: 0,
        keepNameDraft: defaultKeepName(prompt),
      });
      setSession((s) =>
        transitionSession(
          { ...s, isPlaying: true },
          'shape',
          { statusMessage: 'Playing your patch' },
        ),
      );
    },
    [macroKnobs],
  );

  const openSay = useCallback(() => {
    sayCapture.reset();
    setSession((s) => transitionSession(s, 'say', { statusMessage: '' }));
  }, [sayCapture]);

  const cancelSay = useCallback(() => {
    sayCapture.reset();
    setSession((s) => transitionSession(s, 'idle', { statusMessage: '' }));
  }, [sayCapture]);

  const sendPrompt = useCallback(async () => {
    const prompt = sayCapture.draftText.trim();
    if (!prompt) {
      setSession((s) => ({
        ...s,
        statusMessage: "Didn't catch that — type or record a description.",
      }));
      return;
    }

    macroKnobs.resetDrag();
    setGenerating(true);
    setSession((s) =>
      transitionSession(s, 'hear', { statusMessage: 'Building your sound…' }),
    );

    const result = await runMobileGenerateFlow(prompt);
    setGenerating(false);

    if (!result.ok) {
      setSession((s) => ({
        ...transitionSession(s, 'say', { statusMessage: result.message }),
        isPlaying: false,
      }));
      return;
    }

    await loadPatchSession(result.patch, prompt, result.brief);
  }, [loadPatchSession, macroKnobs, sayCapture.draftText]);

  const swipeVariation = useCallback(
    async (direction: 1 | -1) => {
      const engine = engineRef.current;
      if (!engine || !scratch.prompt) return;
      if (macroKnobs.isDragging()) return;

      const token = ++crossfadeToken.current;
      setVariationLoading(true);

      const currentPatch =
        macroKnobs.getBasePatch() ??
        scratch.variations[scratch.selectedVariationIndex]?.patch ??
        scratch.basePatch;
      const fromPatch = projectMacroPatch(currentPatch, macroKnobs.positions);
      const nextIndex = Math.max(0, scratch.variations.length);
      const item = await fetchVariation(scratch.prompt, nextIndex, currentPatch);

      if (token !== crossfadeToken.current) {
        setVariationLoading(false);
        return;
      }

      const positions = macroKnobs.bindBasePatch(item.patch);
      const toPatch = projectMacroPatch(item.patch, positions);

      await crossfadePatches((p) => engine.setPatch(p), fromPatch, toPatch);

      if (token !== crossfadeToken.current) return;

      engine.noteOn(DEMO_NOTE, 100);
      setScratch((s) => ({
        ...s,
        basePatch: item.patch,
        variations:
          direction === 1
            ? [...s.variations, item]
            : [item, ...s.variations],
        selectedVariationIndex: direction === 1 ? s.variations.length : 0,
      }));
      setVariationLoading(false);
      setSession((s) =>
        s.state === 'shape'
          ? s
          : transitionSession(s, 'shape', { statusMessage: 'Variation loaded' }),
      );
    },
    [macroKnobs, scratch],
  );

  const openVariations = useCallback(() => {
    setSession((s) => transitionSession(s, 'variations', { statusMessage: '' }));
  }, []);

  const backToShape = useCallback(() => {
    setSession((s) => transitionSession(s, 'shape', { statusMessage: '' }));
  }, []);

  const selectVariation = useCallback(
    async (index: number) => {
      const engine = engineRef.current;
      const item = scratch.variations[index];
      if (!engine || !item) return;
      macroKnobs.resetDrag();
      const positions = macroKnobs.bindBasePatch(item.patch);
      applyProjectedPatch(engine, item.patch, positions);
      setScratch((s) => ({
        ...s,
        basePatch: item.patch,
        selectedVariationIndex: index,
      }));
      setSession((s) => transitionSession(s, 'shape', { statusMessage: 'Variation selected' }));
    },
    [macroKnobs, scratch.variations],
  );

  const requestMoreVariations = useCallback(async () => {
    setSession((s) =>
      transitionSession(s, 'hear', { statusMessage: 'Fetching more variations…' }),
    );
    await swipeVariation(1);
    setSession((s) => transitionSession(s, 'variations', { statusMessage: '' }));
  }, [swipeVariation]);

  const openKeep = useCallback(() => {
    setScratch((s) => ({
      ...s,
      keepNameDraft: s.keepNameDraft || defaultKeepName(s.prompt),
    }));
    setSession((s) => transitionSession(s, 'keep', { statusMessage: '' }));
  }, []);

  const cancelKeep = useCallback(() => {
    setSession((s) => transitionSession(s, 'shape', { statusMessage: '' }));
  }, []);

  const confirmKeep = useCallback(async () => {
    const storage = storageRef.current ?? createMemoryStorage();
    const base = macroKnobs.getBasePatch() ?? scratch.basePatch;
    if (!base || !scratch.prompt) {
      setSession((s) => ({
        ...s,
        state: 'error',
        returnState: 'keep',
        statusMessage: 'Nothing to keep yet',
      }));
      return;
    }

    setKeeping(true);
    try {
      const selected = scratch.variations[scratch.selectedVariationIndex];
      await savePreset(storage, {
        name: scratch.keepNameDraft.trim() || defaultKeepName(scratch.prompt),
        prompt: scratch.prompt,
        patch: projectMacroPatch(base, macroKnobs.positions),
        macros: macroPositionsForKeep(macroKnobs.positions),
        variation: {
          index: selected?.index ?? 0,
          seed: selected?.seed,
        },
      });
      setKeeping(false);
      setScratch(EMPTY_SCRATCH);
      setSession((s) =>
        transitionSession(
          { ...s, isPlaying: false },
          'idle',
          { statusMessage: 'Saved to your library' },
        ),
      );
    } catch (err) {
      setKeeping(false);
      setSession((s) => ({
        ...s,
        state: 'error',
        returnState: 'keep',
        statusMessage: err instanceof Error ? err.message : 'Could not save preset',
      }));
    }
  }, [macroKnobs, scratch]);

  const regenerate = useCallback(async () => {
    if (!scratch.prompt) return;
    macroKnobs.resetDrag();
    setGenerating(true);
    setSession((s) =>
      transitionSession(s, 'hear', { statusMessage: 'Regenerating…' }),
    );
    const result = await runMobileGenerateFlow(scratch.prompt);
    setGenerating(false);
    if (!result.ok) {
      setSession((s) => ({
        ...transitionSession(s, 'shape', { statusMessage: result.message }),
      }));
      return;
    }
    await loadPatchSession(result.patch, scratch.prompt, result.brief);
  }, [loadPatchSession, macroKnobs, scratch.prompt]);

  const togglePlay = useCallback(() => {
    const engine = engineRef.current;
    if (!engine) return;
    setSession((s) => {
      const nextPlaying = !s.isPlaying;
      if (nextPlaying) {
        engine.noteOn(DEMO_NOTE, 100);
      } else {
        engine.noteOff(DEMO_NOTE);
      }
      return { ...s, isPlaying: nextPlaying };
    });
  }, []);

  const setKeepName = useCallback((name: string) => {
    setScratch((s) => ({ ...s, keepNameDraft: name }));
  }, []);

  return useMemo(
    () => ({
      session,
      scratch,
      backend,
      scopeSamples,
      togglePlay,
      openSay,
      cancelSay,
      sendPrompt,
      sayCapture,
      generating,
      macroKnobs,
      swipeVariation,
      openVariations,
      backToShape,
      selectVariation,
      requestMoreVariations,
      openKeep,
      cancelKeep,
      confirmKeep,
      setKeepName,
      regenerate,
      variationLoading,
      keeping,
    }),
    [
      backend,
      cancelKeep,
      cancelSay,
      confirmKeep,
      generating,
      keeping,
      macroKnobs,
      openKeep,
      openSay,
      openVariations,
      backToShape,
      regenerate,
      requestMoreVariations,
      sayCapture,
      scopeSamples,
      scratch,
      selectVariation,
      sendPrompt,
      session,
      setKeepName,
      swipeVariation,
      togglePlay,
      variationLoading,
    ],
  );
}
