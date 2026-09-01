import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { SynthEngine } from '@agentic-synth/engine-bridge';
import type { PatchParams } from '@agentic-synth/shared-types';
import demoPatchJson from '../../assets/demo-patch.json';
import { bootDemoPatch, createMobileEngine, type EngineBackend } from '../engine/createMobileEngine';
import { useSayCapture } from './useSayCapture';
import { runMobileGenerateFlow } from '../services/mobileGenerateFlow';
import { INITIAL_SESSION, type MobileSession } from '../state/mobileState';
import { bootToHear, transitionSession } from '../state/mobileStateMachine';

const DEMO_NOTE = 60;

function applyPatchToEngine(
  engine: SynthEngine,
  patch: PatchParams,
  modulation?: Parameters<SynthEngine['applyMacros']>[0],
): void {
  engine.setPatch(patch);
  if (modulation) engine.applyMacros(modulation);
  void engine.ensureStarted();
  engine.noteOn(DEMO_NOTE, 100);
}

export function useMobileApp() {
  const engineRef = useRef<SynthEngine | null>(null);
  const [session, setSession] = useState<MobileSession>(INITIAL_SESSION);
  const [backend, setBackend] = useState<EngineBackend>('mock');
  const [scopeSamples, setScopeSamples] = useState<number[]>([]);
  const [generating, setGenerating] = useState(false);
  const sayCapture = useSayCapture();
  const demoPatch = demoPatchJson as PatchParams;

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

    const engine = engineRef.current;
    if (!engine) {
      setSession((s) => ({
        ...s,
        state: 'error',
        returnState: 'say',
        statusMessage: 'Audio engine unavailable',
        isPlaying: false,
      }));
      return;
    }

    try {
      applyPatchToEngine(engine, result.patch, result.modulation);
      setSession((s) =>
        transitionSession(
          { ...s, isPlaying: true },
          'shape',
          { statusMessage: 'Playing your patch' },
        ),
      );
    } catch (err) {
      setSession((s) => ({
        ...s,
        state: 'error',
        returnState: 'say',
        statusMessage: err instanceof Error ? err.message : 'Could not play patch',
        isPlaying: false,
      }));
    }
  }, [sayCapture.draftText]);

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

  return useMemo(
    () => ({
      session,
      backend,
      scopeSamples,
      togglePlay,
      openSay,
      cancelSay,
      sendPrompt,
      sayCapture,
      generating,
    }),
    [
      backend,
      cancelSay,
      generating,
      openSay,
      sayCapture,
      scopeSamples,
      sendPrompt,
      session,
      togglePlay,
    ],
  );
}
