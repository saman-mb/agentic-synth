import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { SynthEngine } from '@agentic-synth/engine-bridge';
import type { PatchParams } from '@agentic-synth/shared-types';
import demoPatchJson from '../../assets/demo-patch.json';
import { bootDemoPatch, createMobileEngine, type EngineBackend } from '../engine/createMobileEngine';
import { INITIAL_SESSION, type MobileSession } from '../state/mobileState';
import { bootToHear } from '../state/mobileStateMachine';

const DEMO_NOTE = 60;

export function useMobileApp() {
  const engineRef = useRef<SynthEngine | null>(null);
  const [session, setSession] = useState<MobileSession>(INITIAL_SESSION);
  const [backend, setBackend] = useState<EngineBackend>('mock');
  const [scopeSamples, setScopeSamples] = useState<number[]>([]);
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
    }),
    [backend, scopeSamples, session, togglePlay],
  );
}
