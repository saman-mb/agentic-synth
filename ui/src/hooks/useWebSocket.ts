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

// Phase 5: when running inside the JUCE 8 WebBrowserComponent host,
// `window.__JUCE__.backend` is injected and we talk to native via
// event emit + getNativeFunction. In a plain browser (Vite dev outside
// JUCE), fall back to the legacy WebSocket transport so the dev loop
// still works without rebuilding the plugin.
interface JuceBackend {
  emitEvent: (name: string, payload: unknown) => void;
  addEventListener: (name: string, cb: (payload: unknown) => void) => number;
  removeEventListener: (id: number) => void;
}
interface JuceGlobal { backend: JuceBackend }

function getJuce(): JuceGlobal | null {
  const j = (window as unknown as { __JUCE__?: JuceGlobal }).__JUCE__;
  return j ?? null;
}

const usingJuce = getJuce() !== null;

// Promise shim matching JUCE's bundled getNativeFunction wire format:
// emit __juce__invoke with {name, params, resultId}, listen on
// __juce__complete for {promiseId, result}. Native function lambdas
// in WebUiComponent.cpp call completion(juce::var{...}) which JUCE
// routes back as __juce__complete. Verified against
// third_party/JUCE/modules/juce_gui_extra/native/javascript/index.js.
interface PendingPromise {
  resolve: (value: unknown) => void;
  reject: (err: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}
const pendingPromises = new Map<number, PendingPromise>();

// ID-namespacing: JUCE's bundled getNativeFunction PromiseHandler also
// listens on __juce__complete with its own counter starting at 0. We
// start ours at a large offset so our IDs never collide with JUCE's,
// and the __juce__complete listener below ignores anything below the
// offset (it belongs to JUCE's internal handler, not us).
const PROMISE_ID_OFFSET = 1_000_000;
let nextPromiseId = PROMISE_ID_OFFSET;
let completeListenerWired = false;

const DEFAULT_TIMEOUT_MS = 10_000;

function ensureCompleteListener(juce: JuceGlobal): void {
  if (completeListenerWired) return;
  completeListenerWired = true;
  juce.backend.addEventListener('__juce__complete', (payload) => {
    const p = payload as { promiseId: number; result: unknown };
    // Namespace guard: IDs below offset belong to JUCE's bundled
    // getNativeFunction handler — ignore so we don't double-handle.
    if (typeof p.promiseId !== 'number' || p.promiseId < PROMISE_ID_OFFSET) return;
    const entry = pendingPromises.get(p.promiseId);
    if (entry) {
      clearTimeout(entry.timer);
      pendingPromises.delete(p.promiseId);
      entry.resolve(p.result);
    }
  });
}

// Timeout-based reject prevents the resolver from sitting in
// pendingPromises forever if the C++ side never calls completion(...)
// (lambda throws, app teardown mid-call, etc). On timeout we delete
// the entry and reject so callers can surface the failure.
function callNative(
  name: string,
  args: unknown[],
  timeoutMs: number = DEFAULT_TIMEOUT_MS,
): Promise<unknown> {
  const juce = getJuce();
  if (!juce) return Promise.reject(new Error('JUCE backend not present'));
  ensureCompleteListener(juce);
  const id = nextPromiseId++;
  return new Promise<unknown>((resolve, reject) => {
    const timer = setTimeout(() => {
      if (pendingPromises.delete(id)) {
        reject(new Error('callNative timeout: ' + name));
      }
    }, timeoutMs);
    pendingPromises.set(id, { resolve, reject, timer });
    juce.backend.emitEvent('__juce__invoke', { name, params: args, resultId: id });
  });
}

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
] as const;

export function useWebSocket(url: string): UseWebSocketReturn {
  const ws = useRef<WebSocket | null>(null);
  const [readyState, setReadyState] = useState<WSReadyState>(
    usingJuce ? (WebSocket.OPEN as WSReadyState) : WebSocket.CONNECTING,
  );
  const [lastMessage, setLastMessage] = useState<string | null>(null);

  // JUCE native bridge path
  useEffect(() => {
    if (!usingJuce) return;
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
    if (usingJuce) return;
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
    if (usingJuce) {
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
    if (usingJuce) {
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
