import { useCallback, useEffect, useRef, useState } from 'react';

import type { CaptureSubstate, MicPermission, SpeechTranscriber } from '../services/speechCapture';
import {
  createDefaultTranscriber,
  openMicSettings,
  queryMicPermission,
  requestMicPermission,
} from '../services/speechCapture';

export type InputMode = 'voice' | 'text';

export interface SayCaptureState {
  substate: CaptureSubstate;
  mode: InputMode;
  draftText: string;
  micPermission: MicPermission;
  statusLine: string;
}

const INITIAL: SayCaptureState = {
  substate: 'idle',
  mode: 'voice',
  draftText: '',
  micPermission: 'undetermined',
  statusLine: '',
};

export function useSayCapture(transcriber?: SpeechTranscriber) {
  const txRef = useRef(transcriber ?? createDefaultTranscriber());
  const [state, setState] = useState<SayCaptureState>(INITIAL);

  useEffect(() => {
    void (async () => {
      const perm = await queryMicPermission();
      setState((s) => ({
        ...s,
        micPermission: perm,
        substate: perm === 'denied' ? 'denied' : s.substate,
        mode: perm === 'denied' ? 'text' : s.mode,
        statusLine: perm === 'denied' ? 'Microphone off — type instead.' : s.statusLine,
      }));
    })();
  }, []);

  const setDraft = useCallback((draftText: string) => {
    setState((s) => ({ ...s, draftText }));
  }, []);

  const tapRecord = useCallback(async () => {
    if (state.substate === 'recording') {
      setState((s) => ({ ...s, substate: 'transcribing', statusLine: 'Getting that…' }));
      try {
        const text = await txRef.current.stop();
        if (!text.trim()) {
          setState((s) => ({
            ...s,
            substate: 'idle',
            mode: 'text',
            statusLine: "Didn't catch that — edit or re-record.",
          }));
          return;
        }
        setState((s) => ({
          ...s,
          substate: 'idle',
          draftText: text.trim(),
          mode: 'text',
          statusLine: '',
        }));
      } catch {
        setState((s) => ({
          ...s,
          substate: 'idle',
          statusLine: 'Voice capture failed — type instead.',
          mode: 'text',
        }));
      }
      return;
    }

    let perm = state.micPermission;
    if (perm !== 'granted') {
      perm = await requestMicPermission();
    }
    if (perm === 'denied') {
      setState((s) => ({
        ...s,
        micPermission: 'denied',
        substate: 'denied',
        mode: 'text',
        statusLine: 'Microphone off — type instead.',
      }));
      return;
    }

    try {
      await txRef.current.start();
      setState((s) => ({
        ...s,
        micPermission: 'granted',
        substate: 'recording',
        statusLine: 'Listening… tap Stop when done.',
      }));
    } catch {
      setState((s) => ({
        ...s,
        substate: 'idle',
        statusLine: 'Could not start microphone.',
        mode: 'text',
      }));
    }
  }, [state.micPermission, state.substate]);

  const cancelCapture = useCallback(async () => {
    await txRef.current.cancel();
    setState((s) => ({
      ...s,
      substate: s.micPermission === 'denied' ? 'denied' : 'idle',
      statusLine: '',
    }));
  }, []);

  const openSettings = useCallback(() => {
    openMicSettings();
  }, []);

  const reset = useCallback(() => {
    void txRef.current.cancel();
    setState(INITIAL);
  }, []);

  const canSend = state.draftText.trim().length > 0;

  return {
    ...state,
    setDraft,
    tapRecord,
    cancelCapture,
    openSettings,
    reset,
    canSend,
    isRecording: state.substate === 'recording',
    micDisabled: state.micPermission === 'denied',
  };
}
