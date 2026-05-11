import { useCallback, useEffect, useRef, useState } from 'react';
import type { WireIncoming, WireOutgoing } from '../types/chat';

export type BridgeStatus = 'connecting' | 'open' | 'closed' | 'error';

const WS_URL = 'ws://localhost:8765';
const RECONNECT_DELAY_MS = 3000;

// Phase 4: JUCE 8's WebBrowserComponent with withNativeIntegrationEnabled(true)
// injects `window.__JUCE__.backend` — verified against
// third_party/JUCE/modules/juce_gui_extra/native/javascript/index.js. We switch
// to the native bridge when present; pure-browser dev keeps the WebSocket path.
//
// API contract (mirrors WebUiComponent.cpp registrations):
//   • Push events from C++ → JS arrive on
//       window.__JUCE__.backend.addEventListener(name, payload => { ... })
//     where name is one of:
//       token, patch, done, error, rationale,
//       suggest_variations, patch_update, transcript
//   • Request/response calls JS → C++ use getNativeFunction(name)(...args)
//     which returns a Promise. Names registered on the C++ side:
//       knob_tweak, generate, feedback,
//       get_dictionary, save_dictionary,
//       get_telemetry, set_telemetry_enabled,
//       push_audio_pcm
interface JuceBackend {
  emitEvent: (name: string, payload: unknown) => void;
  addEventListener: (name: string, cb: (payload: unknown) => void) => number;
  removeEventListener: (id: number) => void;
}
interface JuceGlobal {
  backend: JuceBackend;
  initialisationData?: { __juce__functions?: string[] };
}

function getJuce(): JuceGlobal | null {
  const j = (window as unknown as { __JUCE__?: JuceGlobal }).__JUCE__;
  return j ?? null;
}

const usingJuce = getJuce() !== null;

// Promise-shaped native function shim. JUCE's bundled JS provides this via
// getNativeFunction(name); we replicate the minimal behaviour here (matching
// index.js promise plumbing) so callers see a Promise without importing the
// JUCE module directly. The wire protocol is "__juce__invoke" with resultId,
// resolved by "__juce__complete".
const pendingPromises = new Map<number, (value: unknown) => void>();
let nextPromiseId = 0;

function ensurePromiseListener(juce: JuceGlobal): void {
  // Wire once per page. Subsequent calls are no-ops.
  if ((juce as unknown as { __ourCompleteWired__?: boolean }).__ourCompleteWired__) return;
  (juce as unknown as { __ourCompleteWired__?: boolean }).__ourCompleteWired__ = true;
  juce.backend.addEventListener('__juce__complete', (payload) => {
    const p = payload as { promiseId: number; result: unknown };
    const resolver = pendingPromises.get(p.promiseId);
    if (resolver) {
      pendingPromises.delete(p.promiseId);
      resolver(p.result);
    }
  });
}

function callNative(name: string, args: unknown[]): Promise<unknown> {
  const juce = getJuce();
  if (!juce) return Promise.reject(new Error('JUCE backend not present'));
  ensurePromiseListener(juce);
  const id = nextPromiseId++;
  return new Promise<unknown>((resolve) => {
    pendingPromises.set(id, resolve);
    juce.backend.emitEvent('__juce__invoke', { name, params: args, resultId: id });
  });
}

interface UseSynthBridgeReturn {
  status: BridgeStatus;
  send: (msg: WireOutgoing) => void;
  lastMessage: WireIncoming | null;
}

export function useSynthBridge(): UseSynthBridgeReturn {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [status, setStatus] = useState<BridgeStatus>(usingJuce ? 'open' : 'connecting');
  const [lastMessage, setLastMessage] = useState<WireIncoming | null>(null);

  // ── JUCE native bridge path ────────────────────────────────────────────────
  useEffect(() => {
    if (!usingJuce) return;
    const juce = getJuce();
    if (!juce) return;

    const tokens: number[] = [];
    const wrap = (type: WireIncoming['type'], extract?: (p: unknown) => Partial<WireIncoming>) => {
      const id = juce.backend.addEventListener(type, (payload) => {
        const base = { type } as unknown as WireIncoming;
        if (extract) {
          setLastMessage({ ...base, ...extract(payload) } as WireIncoming);
        } else {
          setLastMessage({ ...base, ...(payload as object) } as WireIncoming);
        }
      });
      tokens.push(id);
    };

    wrap('token');
    wrap('patch');
    wrap('done');
    wrap('error');
    wrap('rationale');
    wrap('suggest_variations');
    // patch_update and transcript aren't in the WireIncoming union today;
    // App.tsx parses them via useWebSocket's raw lastMessage. To preserve
    // observable parity we still route them through setLastMessage as
    // best-effort generic objects (consumers will see them via their own
    // hooks once those migrate in a follow-up).
    juce.backend.addEventListener('patch_update', (payload) => {
      setLastMessage({ type: 'patch_update' as unknown as WireIncoming['type'], ...(payload as object) } as unknown as WireIncoming);
    });
    juce.backend.addEventListener('transcript', (payload) => {
      setLastMessage({ type: 'transcript' as unknown as WireIncoming['type'], ...(payload as object) } as unknown as WireIncoming);
    });

    return () => {
      for (const id of tokens) juce.backend.removeEventListener(id);
    };
  }, []);

  // ── WebSocket path (browser-only dev) ──────────────────────────────────────
  const connect = useCallback(() => {
    if (usingJuce) return;
    if (wsRef.current?.readyState === WebSocket.OPEN) return;

    setStatus('connecting');
    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => setStatus('open');

    ws.onmessage = (ev: MessageEvent) => {
      try {
        const parsed = JSON.parse(ev.data as string) as WireIncoming;
        setLastMessage(parsed);
      } catch {
        // ignore malformed frames
      }
    };

    ws.onclose = () => {
      setStatus('closed');
      reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
    };

    ws.onerror = () => {
      setStatus('error');
      ws.close();
    };
  }, []);

  useEffect(() => {
    connect();
    return () => {
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connect]);

  const send = useCallback((msg: WireOutgoing) => {
    if (usingJuce) {
      // Map WireOutgoing variants onto registered native function names.
      switch (msg.type) {
        case 'generate': {
          void callNative('generate', [msg.prompt, msg.sessionId]);
          return;
        }
        case 'feedback': {
          void callNative('feedback', [msg.messageId, msg.kind, msg.patch ?? null]);
          return;
        }
        // Spread each known WireOutgoing variant onto positional `params`
        // matching the C++ native function arity in WebUiComponent.cpp.
        // Wrapping into a single object arg breaks args[0]/args[1] reads
        // on the C++ side, so each known type is handled explicitly here.
        default: {
          const m = msg as { type: string } & Record<string, unknown>;
          switch (m.type) {
            case 'knob_tweak':
              void callNative('knob_tweak', [m.param, m.value]);
              return;
            case 'get_dictionary':
              void callNative('get_dictionary', []);
              return;
            case 'save_dictionary':
              void callNative('save_dictionary', [m.entries]);
              return;
            case 'get_telemetry':
              void callNative('get_telemetry', []);
              return;
            case 'set_telemetry_enabled':
              void callNative('set_telemetry_enabled', [m.enabled]);
              return;
            case 'push_audio_pcm':
              void callNative('push_audio_pcm', [m.pcm]);
              return;
            default:
              // Unknown message types are dropped silently rather than
              // accidentally invoking a same-named native function with
              // the wrong shape. Log so misconfig surfaces in DevTools.
              // eslint-disable-next-line no-console
              console.warn('[bridge] unknown WireOutgoing type:', m.type);
              return;
          }
        }
      }
    }
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg));
    }
  }, []);

  return { status, send, lastMessage };
}
