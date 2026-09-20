import { useCallback, useEffect, useRef, useState } from 'react';
import type { WireIncoming, WireOutgoing } from '@agentic-synth/shared-types';

export type BridgeStatus = 'connecting' | 'open' | 'closed' | 'error';

const WS_URL = 'ws://localhost:8765';
const RECONNECT_DELAY_MS = 3000;

import { callNative, isJuceAvailable, getJuce } from '../utils/juceBridge';

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

// Phase D / #260 — promise-returning wrapper for `get_presets`. Consumers
// that want the saved-sounds list call this directly rather than going
// through `send({ type: 'get_presets' })` which drops the result.
export async function fetchPresets(): Promise<unknown> {
  if (!isJuceAvailable()) return { presets: [] };
  return callNative('get_presets', []);
}

// ── Audio device settings ────────────────────────────────────────────
// JUCE hides its device picker behind an "Options" button in the standalone
// title bar. The SETTINGS panel offers it instead, but only where a picker
// exists: false in the browser dev server, and false under VST3/AU where the
// host owns audio I/O.
export async function audioSettingsSupported(): Promise<boolean> {
  if (!isJuceAvailable()) return false;
  try {
    return (await callNative('audio_settings_supported', [])) === true;
  } catch {
    // Older binary without the native function — degrade to hiding the row.
    return false;
  }
}

// Opens the wrapper's device dialog. Resolves false when unavailable so the
// caller can surface that rather than appearing to do nothing.
export async function openAudioSettings(): Promise<boolean> {
  if (!isJuceAvailable()) return false;
  try {
    return (await callNative('open_audio_settings', [])) === true;
  } catch {
    return false;
  }
}

// ── Web-demo output device picker (#280) ─────────────────────────────
// Output-only: the standalone wrapper owns sample rate / buffer size and
// hardware MIDI, and push-to-talk STT is disabled in the demo. These
// resolve against the demo shim's list_output_devices / set_output_device
// native functions; SettingsPanel only calls them in web-demo mode (it
// keys off the shim's body class), so the plugin never invokes them and
// the unregistered-name promise would never resolve there anyway.
export interface AudioOutputDevice {
  deviceId: string;
  label: string;
}

export async function listAudioOutputDevices(): Promise<AudioOutputDevice[]> {
  if (!isJuceAvailable()) return [];
  try {
    const res = await callNative('list_output_devices', []);
    if (!Array.isArray(res)) return [];
    return res.filter(
      (d): d is AudioOutputDevice =>
        isOutputDeviceShape(d),
    );
  } catch {
    return [];
  }
}

function isOutputDeviceShape(d: unknown): d is AudioOutputDevice {
  return (
    typeof d === 'object' && d !== null &&
    typeof (d as { deviceId?: unknown }).deviceId === 'string' &&
    typeof (d as { label?: unknown }).label === 'string'
  );
}

// Resolves false on unsupported browsers / failed switches so the panel
// can surface its error state rather than appearing to do nothing.
export async function setAudioOutputDevice(deviceId: string): Promise<boolean> {
  if (!isJuceAvailable()) return false;
  try {
    return (await callNative('set_output_device', [deviceId])) === true;
  } catch {
    return false;
  }
}

interface UseSynthBridgeReturn {
  status: BridgeStatus;
  send: (msg: WireOutgoing) => void;
  lastMessage: WireIncoming | null;
  // Synchronous per-message subscribe. Use instead of `lastMessage`
  // when the C++ side fires multiple events in one tick — React state
  // gets batched and intermediates are lost.
  subscribe: (cb: (msg: WireIncoming) => void) => () => void;
}

// Module-scoped per-event subscriber set. Bypasses React state so every
// event fires every subscriber synchronously in order.
type SyncListener = (msg: WireIncoming) => void;
const syncListeners = new Set<SyncListener>();
function fireSync(msg: WireIncoming): void {
  for (const cb of syncListeners) cb(msg);
}

export function useSynthBridge(): UseSynthBridgeReturn {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [status, setStatus] = useState<BridgeStatus>(isJuceAvailable() ? 'open' : 'connecting');
  const [lastMessage, setLastMessage] = useState<WireIncoming | null>(null);
  // Bridge messages may arrive in bursts (notifyPatch → notifyToken →
  // notifyRationale → notifyDone all in one C++ tick). React 18 batches
  // setLastMessage so only the last value survives — intermediates get
  // dropped. We buffer them in a ref-backed queue and flush ONE per
  // microtask via setLastMessage, guaranteeing every consumer sees every
  // message in order.
  const queueRef = useRef<WireIncoming[]>([]);
  const flushScheduledRef = useRef(false);

  const enqueueMessage = useCallback((msg: WireIncoming) => {
    // Fire synchronous subscribers FIRST — no batching, no loss.
    fireSync(msg);
    queueRef.current.push(msg);
    if (flushScheduledRef.current) return;
    flushScheduledRef.current = true;
    queueMicrotask(() => {
      flushScheduledRef.current = false;
      const next = queueRef.current.shift();
      if (next !== undefined) setLastMessage(next);
      // If more remain (multi-burst), drain on subsequent microtasks so
      // React can render between each setLastMessage call.
      if (queueRef.current.length > 0) {
        flushScheduledRef.current = true;
        Promise.resolve().then(() => {
          flushScheduledRef.current = false;
          const m = queueRef.current.shift();
          if (m !== undefined) setLastMessage(m);
          // Keep draining as long as queue non-empty.
          const tick = () => {
            const x = queueRef.current.shift();
            if (x !== undefined) {
              setLastMessage(x);
              if (queueRef.current.length > 0) Promise.resolve().then(tick);
            }
          };
          if (queueRef.current.length > 0) Promise.resolve().then(tick);
        });
      }
    });
  }, []);

  // ── JUCE native bridge path ────────────────────────────────────────────────
  useEffect(() => {
    if (!isJuceAvailable()) return;
    const juce = getJuce();
    if (!juce) return;

    const tokens: number[] = [];
    const wrap = (type: WireIncoming['type'], extract?: (p: unknown) => Partial<WireIncoming>) => {
      const id = juce.backend.addEventListener(type, (payload) => {
        const base = { type } as unknown as WireIncoming;
        if (extract) {
          enqueueMessage({ ...base, ...extract(payload) } as WireIncoming);
        } else {
          enqueueMessage({ ...base, ...(payload as object) } as WireIncoming);
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
    // Two-step LLM flow: ENHANCER brief arrives once per generate call.
    wrap('enhancement');
    // Phase B simple-view (#249) — explicit morph reply from C++.
    wrap('variations_ready');
    // Phase C failure-state UX (#269) — calm banner for LLM-offline /
    // prompt-unclear / safety-block / mic-denied.
    wrap('failure');
    // Phase D commit-UX (#260) — fired after a successful PresetStore.save.
    wrap('preset_committed');
    // Phase D export-to-track (#268) — fired after the offline bounce
    // finishes (or fails / cancels). UI shows the "Saved to <path>" toast.
    wrap('bounce_complete');
    // Phase G / #247 — autocorrelation pitch detection result from the
    // push-to-talk audio buffer.
    wrap('hum_pitch_detected');
    // Phase G / #262 — MIDI learn store captured the next CC for the
    // currently-learning knob.
    wrap('midi_learned');
    juce.backend.addEventListener('patch_update', (payload) => {
      enqueueMessage({ type: 'patch_update' as unknown as WireIncoming['type'], ...(payload as object) } as unknown as WireIncoming);
    });
    juce.backend.addEventListener('transcript', (payload) => {
      enqueueMessage({ type: 'transcript' as unknown as WireIncoming['type'], ...(payload as object) } as unknown as WireIncoming);
    });

    return () => {
      for (const id of tokens) juce.backend.removeEventListener(id);
    };
  }, [enqueueMessage]);

  // ── WebSocket path (browser-only dev) ──────────────────────────────────────
  const connect = useCallback(() => {
    if (isJuceAvailable()) return;
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
    if (isJuceAvailable()) {
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
        case 'play_midi_note': {
          // Audition keyboard one-shot: fire-and-forget; C++ schedules
          // matched note-off via duration_ms.
          void callNative('play_midi_note', [msg.note, msg.velocity, msg.duration_ms]);
          return;
        }
        case 'note_on': {
          void callNative('note_on', [msg.note, msg.velocity]);
          return;
        }
        case 'note_off': {
          void callNative('note_off', [msg.note]);
          return;
        }
        case 'morph_request': {
          // Phase B simple-view (#249) — fire-and-forget. C++ replies via
          // the `variations_ready` event listener wired above.
          void callNative('morph_request', []);
          return;
        }
        case 'commit_preset': {
          // Phase D / #260 — persist this patch. C++ resolves the promise
          // immediately and fires `preset_committed` on success.
          void callNative('commit_preset', [msg.name, msg.prompt ?? '', msg.patch ?? null]);
          return;
        }
        case 'get_presets': {
          // Promise resolution carries the payload; callers wanting the
          // result use callNativePresets() below instead of the bus.
          void callNative('get_presets', []);
          return;
        }
        case 'delete_preset': {
          void callNative('delete_preset', [msg.name]);
          return;
        }
        case 'bounce_patch': {
          // Phase D / #268 — fire-and-forget; C++ opens FileChooser then
          // emits `bounce_complete`.
          void callNative('bounce_patch', [msg.patch ?? null, msg.suggestedName ?? 'tambra-bounce']);
          return;
        }
        case 'start_midi_learn': {
          // Phase G / #262 — enter learn mode for this knob.
          void callNative('start_midi_learn', [msg.knob_id]);
          return;
        }
        case 'cancel_midi_learn': {
          void callNative('cancel_midi_learn', []);
          return;
        }
        case 'clear_midi_mapping': {
          void callNative('clear_midi_mapping', [msg.knob_id]);
          return;
        }
        case 'get_midi_mappings': {
          void callNative('get_midi_mappings', []);
          return;
        }
        case 'record_variation_picked': {
          // Phase H / #261 — fire-and-forget telemetry. C++ appends one
          // JSONL line per event; no return value to await.
          void callNative('record_variation_picked', [
            msg.strategy_id,
            msg.label,
            msg.time_since_arrival_ms,
          ]);
          return;
        }
        case 'record_macro_tweak': {
          void callNative('record_macro_tweak', [msg.macro_index, msg.value, msg.dwell_ms]);
          return;
        }
        case 'record_ab_toggle': {
          void callNative('record_ab_toggle', [msg.from_slot, msg.to_slot]);
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

  const subscribe = useCallback((cb: (msg: WireIncoming) => void) => {
    syncListeners.add(cb);
    return () => { syncListeners.delete(cb); };
  }, []);

  return { status, send, lastMessage, subscribe };
}
