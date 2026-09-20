import { useCallback, useEffect, useRef, useState } from 'react';

export type WSReadyState = 0 | 1 | 2 | 3;

export interface UseWebSocketReturn {
  lastMessage: string | null;
  sendMessage: (msg: string) => void;
  sendBinary: (data: ArrayBuffer) => void;
  readyState: WSReadyState;
  // Subscribe to EVERY inbound message synchronously. lastMessage is
  // React state and gets batched/coalesced under React 18 automatic
  // batching when the C++ side emits multiple events back-to-back
  // (notifyPatch → notifyToken → notifyRationale → notifyDone) — only
  // the final state survives, intermediates are lost. This callback
  // fires once per message, no batching.
  subscribe: (cb: (msg: Record<string, unknown>) => void) => () => void;
}

import { callNative, isJuceAvailable, getJuce } from '../utils/juceBridge';

// Phase 5: when running inside the JUCE 8 WebBrowserComponent host,
// `window.__JUCE__.backend` is injected and we talk to native via
// event emit + getNativeFunction. In a plain browser (Vite dev outside
// JUCE), fall back to the legacy WebSocket transport so the dev loop
// still works without rebuilding the plugin.

// Route an outgoing JSON message to the matching native function with
// positional args (the C++ side reads args[0]/args[1] etc., so wrapping
// into a single object breaks every handler).
function dispatchNative(msg: Record<string, unknown>): void {
  switch (msg.type) {
    case 'knob_tweak':
      void callNative('knob_tweak', [msg.param, msg.value]);
      return;
    case 'generate':
      void callNative('generate', [msg.prompt, msg.sessionId]);
      return;
    case 'feedback':
      void callNative('feedback', [msg.messageId, msg.kind, msg.patch ?? null]);
      return;
    case 'get_dictionary':
      void callNative('get_dictionary', [])
        .then((result) => {
          emitInbound({ type: 'dictionary_data', ...(result as object) });
        })
        .catch((err) => console.warn('[bridge]', 'get_dictionary', 'failed:', err));
      return;
    case 'save_dictionary':
      void callNative('save_dictionary', [msg.entries]);
      return;
    case 'get_telemetry':
      void callNative('get_telemetry', [])
        .then((result) => {
          emitInbound({ type: 'telemetry_data', ...(result as object) });
        })
        .catch((err) => console.warn('[bridge]', 'get_telemetry', 'failed:', err));
      return;
    case 'set_telemetry_enabled':
      void callNative('set_telemetry_enabled', [msg.enabled]);
      return;
    case 'play_midi_note':
      // Audition keyboard: triggers a one-shot note via the JUCE engine.
      // duration_ms drives the C++-side scheduled note-off.
      void callNative('play_midi_note', [msg.note, msg.velocity, msg.duration_ms]);
      return;
    case 'note_on':
      void callNative('note_on', [msg.note, msg.velocity]);
      return;
    case 'note_off':
      void callNative('note_off', [msg.note]);
      return;
    default:
      // eslint-disable-next-line no-console
      console.warn('[bridge] unknown outgoing type:', msg.type);
  }
}

// Pull-query responses re-enter the inbound stream by calling this
// module-scoped emitter, which every active hook instance subscribes to.
type InboundListener = (msg: Record<string, unknown>) => void;
const inboundListeners = new Set<InboundListener>();

function emitInbound(msg: Record<string, unknown>): void {
  for (const l of inboundListeners) l(msg);
}

const INBOUND_EVENTS = [
  'token', 'patch', 'done', 'error', 'rationale',
  'suggest_variations', 'patch_update', 'transcript',
  // Two-step LLM flow: ENHANCER brief, emitted once per generate call
  // between submitPrompt and generateLlmPatch.
  'enhancement',
] as const;

export function useWebSocket(url: string): UseWebSocketReturn {
  const ws = useRef<WebSocket | null>(null);
  const [readyState, setReadyState] = useState<WSReadyState>(
    isJuceAvailable() ? (WebSocket.OPEN as WSReadyState) : WebSocket.CONNECTING,
  );
  const [lastMessage, setLastMessage] = useState<string | null>(null);

  // JUCE native bridge path
  useEffect(() => {
    if (!isJuceAvailable()) return;
    const juce = getJuce();
    if (!juce) return;

    const tokens: number[] = [];
    for (const name of INBOUND_EVENTS) {
      const id = juce.backend.addEventListener(name, (payload) => {
        // Fire BOTH React state (lastMessage) AND the inbound queue.
        // Reason: when the C++ side emits multiple events synchronously
        // (notifyPatch → notifyToken → notifyRationale → notifyDone all
        // back-to-back), React 18 batches setLastMessage and only the
        // final state propagates to effects — the intermediate events
        // get dropped on the floor. emitInbound runs synchronously per
        // event so all subscribers see every frame.
        const msg = { type: name, ...(payload as object) };
        setLastMessage(JSON.stringify(msg));
        emitInbound(msg);
      });
      tokens.push(id);
    }

    const listener: InboundListener = (msg) => setLastMessage(JSON.stringify(msg));
    inboundListeners.add(listener);

    return () => {
      for (const id of tokens) juce.backend.removeEventListener(id);
      inboundListeners.delete(listener);
    };
  }, []);

  // WebSocket path (pure-browser dev only)
  useEffect(() => {
    if (isJuceAvailable()) return;
    const socket = new WebSocket(url);
    ws.current = socket;
    socket.onopen = () => setReadyState(WebSocket.OPEN as WSReadyState);
    socket.onclose = () => setReadyState(WebSocket.CLOSED as WSReadyState);
    socket.onerror = () => setReadyState(WebSocket.CLOSED as WSReadyState);
    socket.onmessage = (e: MessageEvent) => {
      if (typeof e.data === 'string') setLastMessage(e.data);
    };
    return () => { socket.close(); };
  }, [url]);

  const sendMessage = useCallback((msg: string) => {
    if (isJuceAvailable()) {
      try {
        const parsed = JSON.parse(msg) as Record<string, unknown>;
        dispatchNative(parsed);
      } catch {
        // ignore malformed frames
      }
      return;
    }
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(msg);
    }
  }, []);

  const sendBinary = useCallback((data: ArrayBuffer) => {
    if (isJuceAvailable()) {
      // 16 kHz mono Float32 PCM → Int16 PCM → base64 string.
      // Old path passed Array<number> of doubles (~24 B/sample boxed
      // juce::var on the C++ side, ~38 KB per 100 ms chunk + JSON);
      // new path is one ~4.3 KB string per 100 ms chunk and lets the
      // C++ side decode on a worker thread. Int16 (16-bit signed PCM)
      // is the canonical Whisper input format and is bit-exact for the
      // dynamic range Whisper actually uses — no audible quality loss
      // vs Float32 at 16 kHz mono.
      const f32 = new Float32Array(data);
      const i16 = new Int16Array(f32.length);
      for (let i = 0; i < f32.length; i++) {
        const s = Math.max(-1, Math.min(1, f32[i]));
        i16[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
      }
      // Base64-encode the underlying bytes. Chunked btoa avoids the
      // "argument list too long" hazard fromCharCode hits on long
      // typed arrays (~64k+ args on some engines).
      const bytes = new Uint8Array(i16.buffer);
      let binary = '';
      const CHUNK = 0x8000;
      for (let i = 0; i < bytes.length; i += CHUNK) {
        binary += String.fromCharCode.apply(
          null,
          Array.from(bytes.subarray(i, i + CHUNK)),
        );
      }
      const b64 = btoa(binary);
      void callNative('push_audio_pcm', [b64]);
      return;
    }
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(data);
    }
  }, []);

  const subscribe = useCallback((cb: (msg: Record<string, unknown>) => void) => {
    inboundListeners.add(cb);
    return () => { inboundListeners.delete(cb); };
  }, []);

  return { lastMessage, sendMessage, sendBinary, readyState, subscribe };
}
