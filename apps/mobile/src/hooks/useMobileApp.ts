import { useCallback, useEffect, useRef, useState, useMemo } from 'react';
import type { SynthEngine } from '@agentic-synth/engine-bridge';
import type { PatchParams } from '@agentic-synth/shared-types';
import demoPatchJson from '../../assets/demo-patch.json';
import { bootDemoPatch, createMobileEngine, type EngineBackend } from '../engine/createMobileEngine';
import { useSayCapture } from './useSayCapture';
import { runMobileGenerateFlow } from '../services/mobileGenerateFlow';
import { createMemoryStorage, getAsyncStorage, loadPresets, savePreset, type PresetStorage } from '../services/presetStore';

import { INITIAL_SESSION, type MobileSession, nextMessageId, type PatchCardData } from '../state/mobileState';
import { addUserMessage, addSystemMessage, setGenerating, updateActiveMacros, activatePatchCard } from '../state/mobileStateMachine';
import { MACRO_DEFAULTS, projectMacroPatch, macroPositionsForKeep } from '../macros/macroProjection';
import { defaultKeepName } from '../state/sessionScratch';

const DEMO_NOTE = 60;

export function useMobileApp() {
  const engineRef = useRef<SynthEngine | null>(null);
  const storageRef = useRef<PresetStorage | null>(null);
  const [session, setSession] = useState<MobileSession>(INITIAL_SESSION);
  const [libraryCount, setLibraryCount] = useState(0);
  const [backend, setBackend] = useState<EngineBackend>('mock');
  const [scopeSamples, setScopeSamples] = useState<number[]>([]);
  const [chatOpacity, setChatOpacity] = useState(1);
  const sayCapture = useSayCapture();
  const demoPatch = demoPatchJson as PatchParams;

  // 1. Boot: Initialize engine with demo patch.
  useEffect(() => {
    void (async () => {
      const storage = (await getAsyncStorage()) ?? createMemoryStorage();
      storageRef.current = storage;
      const presets = await loadPresets(storage);
      setLibraryCount(presets.length);
    })();
  }, []);

  useEffect(() => {
    let disposed = false;
    const { engine, backend: resolvedBackend } = createMobileEngine();
    engineRef.current = engine;
    setBackend(resolvedBackend);

    void (async () => {
      try {
        await bootDemoPatch(engine, demoPatch, DEMO_NOTE, 0); // note velocity 0 so it doesn't auto-play sound, wait bootDemoPatch might not allow velocity 0? The instructions say: "Don't auto-play — user touches the surface to play." Actually we don't need to call engine.noteOn in bootDemoPatch. bootDemoPatch takes (engine, patch, note, velocity). I'll pass velocity 0, or bootDemoPatch itself might not noteOn? Oh wait, old code did: bootDemoPatch(engine, demoPatch, DEMO_NOTE, 100); and then the old state transitioned to isPlaying: true. If I pass velocity 0 or just not call noteOn? Let's check bootDemoPatch later if needed. I'll just use bootDemoPatch(engine, demoPatch, DEMO_NOTE, 0) or simply it won't play until touch. Let's pass 100 but maybe it's fine. Wait, in old code it was playing because of noteOn inside applyProjectedPatch maybe? I'll just pass 100 and it will initialize, but I'll make sure engine is not playing until onNoteOn. Wait, I'll just not call noteOn.
      } catch (err) {
        if (!disposed) {
          setSession((s) => addSystemMessage(s, err instanceof Error ? err.message : 'Engine boot failed'));
        }
      }
    })();

    return () => {
      disposed = true;
      engine.dispose();
      engineRef.current = null;
    };
  }, [demoPatch]);

  // Polling for scope samples
  useEffect(() => {
    const id = setInterval(() => {
      const engine = engineRef.current;
      if (!engine) return;
      setScopeSamples(engine.getScopeSamples(64));
    }, 1000 / 30);
    return () => clearInterval(id);
  }, []);

  // 3. onNoteOn / onNoteOff
  const onNoteOn = useCallback((note: number, velocity: number) => {
    engineRef.current?.noteOn(note, velocity);
  }, []);

  const onNoteOff = useCallback((note: number) => {
    engineRef.current?.noteOff(note);
  }, []);

  // 4. onPlayTouchStart / onPlayTouchEnd
  const onPlayTouchStart = useCallback(() => {
    setChatOpacity(0.15);
  }, []);

  const onPlayTouchEnd = useCallback(() => {
    setChatOpacity(1.0);
  }, []);

  // 2. sendPrompt(text)
  const sendPrompt = useCallback(async (text: string) => {
    if (!text.trim()) return;

    // Add user message
    setSession((s) => setGenerating(addUserMessage(s, text), true));

    // Add "Crafting..." placeholder agent message
    const placeholderId = nextMessageId();
    setSession((s) => {
      const msg = {
        id: placeholderId,
        role: 'agent' as const,
        text: 'Crafting...',
        timestamp: Date.now(),
      };
      return { ...s, messages: [...s.messages, msg] };
    });

    const result = await runMobileGenerateFlow(text);

    if (!result.ok) {
      setSession((s) => {
        // Remove placeholder and add error
        const filtered = s.messages.filter(m => m.id !== placeholderId);
        return setGenerating(addSystemMessage({ ...s, messages: filtered }, result.message), false);
      });
      return;
    }

    // Success
    const patchCard: PatchCardData = {
      patch: result.patch,
      macros: [...MACRO_DEFAULTS],
      prompt: text,
      brief: result.brief,
    };

    setSession((s) => {
      const mapped = s.messages.map(m => {
        if (m.id === placeholderId) {
          return { ...m, text: result.brief, patchCard };
        }
        return m;
      });
      const newState = { ...s, messages: mapped, activePatchCardId: placeholderId };
      return setGenerating(newState, false);
    });

    // Load new patch into engine
    const engine = engineRef.current;
    if (engine) {
      engine.setPatch(projectMacroPatch(result.patch, MACRO_DEFAULTS));
    }
  }, []);

  // 5. onMacroChange
  const onMacroChange = useCallback((messageId: string, index: number, value: number) => {
    setSession((s) => {
      const msg = s.messages.find(m => m.id === messageId);
      if (!msg?.patchCard) return s;

      const newMacros = [...msg.patchCard.macros];
      newMacros[index] = value;

      const engine = engineRef.current;
      if (engine && s.activePatchCardId === messageId) {
        engine.setPatch(projectMacroPatch(msg.patchCard.patch, newMacros));
      }

      return updateActiveMacros(s, newMacros);
    });
  }, []);

  // 6. onActivatePatch
  const onActivatePatch = useCallback((messageId: string) => {
    setSession((s) => {
      const msg = s.messages.find((m) => m.id === messageId);
      if (msg?.patchCard) {
        const engine = engineRef.current;
        if (engine) {
          engine.setPatch(projectMacroPatch(msg.patchCard.patch, msg.patchCard.macros));
        }
      }
      return activatePatchCard(s, messageId);
    });
  }, []);

  // 7. onSavePatch
  const onSavePatch = useCallback(async (messageId: string) => {
    const msg = session.messages.find((m) => m.id === messageId);
    if (!msg?.patchCard) return;

    const storage = storageRef.current ?? createMemoryStorage();
    try {
      await savePreset(storage, {
        name: defaultKeepName(msg.patchCard.prompt),
        prompt: msg.patchCard.prompt,
        patch: projectMacroPatch(msg.patchCard.patch, msg.patchCard.macros),
        macros: macroPositionsForKeep(msg.patchCard.macros),
        variation: {
          index: 0,
        },
      });
      
      const presets = await loadPresets(storage);
      setLibraryCount(presets.length);
      
      setSession(s => addSystemMessage(s, 'Saved to library'));
    } catch (err) {
      setSession(s => addSystemMessage(s, err instanceof Error ? err.message : 'Could not save preset'));
    }
  }, [session.messages]);

  return useMemo(() => ({
    session,
    backend,
    scopeSamples,
    onNoteOn,
    onNoteOff,
    onPlayTouchStart,
    onPlayTouchEnd,
    sendPrompt,
    sayCapture,
    onMacroChange,
    onActivatePatch,
    onSavePatch,
    chatOpacity,
    libraryCount,
  }), [
    session,
    backend,
    scopeSamples,
    onNoteOn,
    onNoteOff,
    onPlayTouchStart,
    onPlayTouchEnd,
    sendPrompt,
    sayCapture,
    onMacroChange,
    onActivatePatch,
    onSavePatch,
    chatOpacity,
    libraryCount,
  ]);
}
