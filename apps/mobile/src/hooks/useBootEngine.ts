import { useEffect, useRef, useState } from 'react';

import type { SynthEngine } from '@agentic-synth/engine-bridge';

import { bootDemoPatch, createMobileEngine, type EngineBackend } from '../engine/createMobileEngine';
import { DEMO_PATCH } from '../assets/demoPatch';
import { INITIAL_SESSION, type MobileSession } from '../state/mobileState';
import { bootToHear } from '../state/mobileStateMachine';

export function useBootEngine(): MobileSession & {
  engine: SynthEngine | null;
  backend: EngineBackend | null;
  scopeSamples: number[];
} {
  const engineRef = useRef<SynthEngine | null>(null);
  const [session, setSession] = useState<MobileSession>(INITIAL_SESSION);
  const [backend, setBackend] = useState<EngineBackend | null>(null);
  const [scopeSamples, setScopeSamples] = useState<number[]>([]);

  useEffect(() => {
    let cancelled = false;
    let scopeTimer: ReturnType<typeof setInterval> | undefined;

    (async () => {
      try {
        const { engine, backend: resolvedBackend } = createMobileEngine({ forceMock: true });
        engineRef.current = engine;
        await bootDemoPatch(engine, DEMO_PATCH);
        if (cancelled) return;
        setBackend(resolvedBackend);
        setSession(bootToHear(INITIAL_SESSION));

        scopeTimer = setInterval(() => {
          if (!engineRef.current) return;
          setScopeSamples(engineRef.current.getScopeSamples(48));
        }, 1000 / 30);
      } catch (err) {
        if (cancelled) return;
        setSession({
          state: 'error',
          statusMessage: err instanceof Error ? err.message : 'Engine boot failed',
          isPlaying: false,
        });
      }
    })();

    return () => {
      cancelled = true;
      if (scopeTimer) clearInterval(scopeTimer);
      engineRef.current?.dispose();
      engineRef.current = null;
    };
  }, []);

  return {
    ...session,
    engine: engineRef.current,
    backend,
    scopeSamples,
  };
}
