// ── Web-demo JUCE shim (issue #280) ─────────────────────────────────
//
// Fakes `window.__JUCE__` (the object JUCE 8's WebBrowserComponent with
// withNativeIntegrationEnabled(true) injects) so the unmodified React UI
// runs in a plain browser with no plugin backend. main.tsx installs this
// ONLY when window.__JUCE__ is absent — inside the WebView the real
// backend wins and this module is dead code.
//
// Wire parity with useSynthBridge.ts:48-74:
//   • JS → native: emitEvent('__juce__invoke', { name, params: [positional],
//     resultId }) — the shim resolves each call by emitting
//     '__juce__complete' { promiseId: resultId, result }.
//   • addEventListener returns numeric token ids; removeEventListener(id)
//     detaches; event payloads reach listeners synchronously and in order.
//
// There are zero side effects at import time: nothing runs until
// installWebDemoShim() is called from main.tsx, and the synth engine is
// only constructed at install (which in the plugin build never happens).
//
// Local persistence (dictionary / feedback / telemetry opt-in) lives in
// localStorage under agentic-synth.demo.* keys — best-effort, try/catch
// guarded for private-mode / quota failures.

import { makeDefaultPatch } from '../components/KnobGrid';
import type { AgentModulationPlan } from '../types/chat';
import type { PatchParams } from '../components/KnobGrid';
import { createSynthEngine, type SynthEngine } from '../webaudio/engine';
import { setPatchParam } from '../webaudio/paramMap';
import { runGenerateFlow } from './generateFlow';

// ── shapes (mirror useSynthBridge.ts / useWebSocket.ts interfaces) ──

export interface DemoJuceBackend {
  emitEvent: (name: string, payload: unknown) => void;
  addEventListener: (name: string, cb: (payload: unknown) => void) => number;
  removeEventListener: (id: number) => void;
}

export interface DemoJuceGlobal {
  backend: DemoJuceBackend;
  initialisationData: { __juce__functions: string[] };
}

type NativeHandler = (params: unknown[]) => unknown;

interface ListenerEntry {
  id: number;
  cb: (payload: unknown) => void;
}

// ── localStorage keys / helpers ──────────────────────────────────────

const DICT_KEY = 'agentic-synth.demo.dictionary.v1';
const FEEDBACK_KEY = 'agentic-synth.demo.feedback.v1';
const TELEMETRY_ENABLED_KEY = 'agentic-synth.demo.telemetry-enabled.v1';

function readJson<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key);
    if (raw === null) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

function writeJson(key: string, value: unknown): void {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // Private mode / quota exceeded — degrade to in-session only.
  }
}

interface FeedbackRecord {
  ts: number;
  message_id: string;
  kind: string;
  patch: unknown;
}

// TelemetrySummary / TelemetryData field-for-field with TelemetryDashboard.tsx.
function makeTelemetryPayload(enabled: boolean): Record<string, unknown> {
  return {
    enabled,
    summary: {
      total_generations: 0,
      error_count: 0,
      error_rate: 0,
      avg_latency_ms: 0,
      p50_latency_ms: 0,
      p95_latency_ms: 0,
      avg_tokens_per_second: 0,
    },
    records: [],
  };
}

// ── misc helpers ─────────────────────────────────────────────────────

// PatchParams is plain JSON data — a round-trip is a sufficient deep copy.
function clonePatch(patch: PatchParams): PatchParams {
  return JSON.parse(JSON.stringify(patch)) as PatchParams;
}

// Velocity normalization: AuditionKeyboard.tsx sends a fixed 0..1 velocity
// (VELOCITY = 0.8) for both note_on and play_midi_note, and the engine
// clamps to 0..1 internally — so the wire value passes through unchanged,
// clamped defensively. A missing/non-finite value falls back to 0.8, the
// component's own constant.
function clampVelocity(v: unknown): number {
  if (typeof v !== 'number' || !Number.isFinite(v)) return 0.8;
  return Math.min(1, Math.max(0, v));
}

function asInt(v: unknown): number | null {
  return typeof v === 'number' && Number.isFinite(v) ? Math.floor(v) : null;
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null;
}

// Only http(s) — never window.open an attacker-controlled javascript: URL.
function openExternalUrl(url: unknown): boolean {
  if (typeof url !== 'string' || url.length === 0) return false;
  try {
    const parsed = new URL(url);
    if (parsed.protocol !== 'https:' && parsed.protocol !== 'http:') return false;
    return window.open(parsed.toString(), '_blank', 'noopener') !== null;
  } catch {
    return false;
  }
}

// ── install ──────────────────────────────────────────────────────────

let installed = false;

export function installWebDemoShim(): void {
  if (typeof window === 'undefined') return;
  const w = window as unknown as { __JUCE__?: unknown };
  if (w.__JUCE__ || installed) return;
  installed = true;

  // ── event bus ────────────────────────────────────────────────────
  const listeners = new Map<string, ListenerEntry[]>();
  let nextListenerId = 1;

  const emitEvent = (name: string, payload: unknown): void => {
    const entries = listeners.get(name);
    if (!entries) return;
    // Snapshot: handlers may add/remove listeners mid-flight.
    for (const { cb } of [...entries]) cb(payload);
  };

  const addEventListener = (name: string, cb: (payload: unknown) => void): number => {
    const id = nextListenerId++;
    const entries = listeners.get(name) ?? [];
    entries.push({ id, cb });
    listeners.set(name, entries);
    return id;
  };

  const removeEventListener = (id: number): void => {
    for (const [name, entries] of listeners) {
      const idx = entries.findIndex((e) => e.id === id);
      if (idx !== -1) {
        entries.splice(idx, 1);
        if (entries.length === 0) listeners.delete(name);
        return;
      }
    }
  };

  // ── state ────────────────────────────────────────────────────────
  const engine: SynthEngine = createSynthEngine();
  // Current-patch snapshot for knob_tweak / feedback; starts at the same
  // default the UI renders, so those work before the first generation.
  let currentPatch: PatchParams = makeDefaultPatch();
  // True only after a successful ensureStarted() inside the generate flow.
  // Notes played into a dead engine emit an `error` event instead of
  // silence, so the demo user is never left guessing.
  let engineReady = false;

  const setEngineReady = (ready: boolean): void => {
    engineReady = ready;
  };

  const applyServerPatch = (patch: PatchParams, modulation?: AgentModulationPlan): void => {
    engine.setPatch(patch); // then
    engine.applyMacros(modulation ?? {}); // macros project onto the patch
    currentPatch = clonePatch(patch);
  };

  const emitError = (message: string): void => {
    emitEvent('error', { message });
  };

  // ── native function handlers (positional args) ───────────────────
  const nativeFns: Record<string, NativeHandler> = {
    // Fire-and-forget; results arrive as emitEvent(...) frames.
    generate: (params) => {
      // params[1] is the sessionId — unused: /api/generate is stateless.
      const prompt = params[0];
      if (typeof prompt !== 'string') {
        emitError('generate: expected a string prompt.');
        return undefined;
      }
      void runGenerateFlow({ emit: emitEvent, engine, applyServerPatch, setEngineReady }, prompt);
      return undefined;
    },

    knob_tweak: (params) => {
      const param = params[0];
      const value = params[1];
      if (typeof param !== 'string' || typeof value !== 'number' || !Number.isFinite(value)) {
        return undefined;
      }
      engine.setParam(param, value);
      setPatchParam(currentPatch, param, value);
      return undefined;
    },

    feedback: (params) => {
      const messageId = params[0];
      const kind = params[1];
      if (typeof messageId !== 'string' || typeof kind !== 'string') return undefined;
      const records = readJson<FeedbackRecord[]>(FEEDBACK_KEY, []);
      records.push({ ts: Date.now(), message_id: messageId, kind, patch: params[2] ?? null });
      writeJson(FEEDBACK_KEY, records);
      return undefined;
    },

    get_dictionary: () => ({ entries: readJson<unknown[]>(DICT_KEY, []) }),

    save_dictionary: (params) => {
      const entries = params[0];
      if (Array.isArray(entries)) writeJson(DICT_KEY, entries);
      return undefined;
    },

    get_telemetry: () => makeTelemetryPayload(readJson<boolean>(TELEMETRY_ENABLED_KEY, false)),

    set_telemetry_enabled: (params) => {
      const enabled = params[0];
      if (typeof enabled === 'boolean') writeJson(TELEMETRY_ENABLED_KEY, enabled);
      return undefined;
    },

    note_on: (params) => {
      const note = asInt(params[0]);
      if (note === null) return undefined;
      if (!engineReady) {
        emitError('Audio engine is not running — generate a patch first.');
        return undefined;
      }
      engine.noteOn(note, clampVelocity(params[1]));
      return undefined;
    },

    note_off: (params) => {
      const note = asInt(params[0]);
      if (note === null) return undefined;
      if (!engineReady) {
        emitError('Audio engine is not running — generate a patch first.');
        return undefined;
      }
      engine.noteOff(note);
      return undefined;
    },

    play_midi_note: (params) => {
      const note = asInt(params[0]);
      const durationMs = asInt(params[2]);
      if (note === null || durationMs === null) return undefined;
      if (!engineReady) {
        emitError('Audio engine is not running — generate a patch first.');
        return undefined;
      }
      engine.playMidiNote(note, clampVelocity(params[1]), durationMs);
      return undefined;
    },

    // Called by Visualizer.tsx once per RAF — must stay allocation-light
    // and synchronous (the shim resolves it in the same tick).
    getScopeSamples: (params) => {
      const n = asInt(params[0]) ?? 0;
      return engine.getScopeSamples(n);
    },

    open_external_url: (params) => openExternalUrl(params[0]),

    // Speech-to-text is out of scope for the web demo; acknowledge the
    // audio so the push-to-talk UI resolves instead of hanging.
    push_audio_pcm: () => {
      emitEvent('transcript', { text: '[mic ready but speech-to-text disabled in web demo]' });
      return undefined;
    },
  };

  // ── promise plumbing (mirrors the JUCE getNativeFunction protocol) ──
  addEventListener('__juce__invoke', (payload) => {
    const p = isRecord(payload) ? payload : {};
    const name = typeof p.name === 'string' ? p.name : null;
    const resultId = typeof p.resultId === 'number' ? p.resultId : null;
    if (name === null || resultId === null) return;
    const params = Array.isArray(p.params) ? p.params : [];
    const handler = nativeFns[name];
    let result: unknown = undefined;
    if (!handler) {
      console.warn('[demo-shim] unhandled native function:', name);
    } else {
      try {
        result = handler(params);
      } catch (err) {
        console.warn('[demo-shim] native function threw:', name, err);
      }
    }
    // None of the handlers return promises today, but Promise.resolve
    // keeps the completion path future-proof and matches JUCE's
    // NativeFnCompletion semantics.
    void Promise.resolve(result).then((r) => {
      emitEvent('__juce__complete', { promiseId: resultId, result: r });
    });
  });

  w.__JUCE__ = {
    backend: { emitEvent, addEventListener, removeEventListener },
    initialisationData: {
      __juce__functions: Object.keys(nativeFns),
    },
  } satisfies DemoJuceGlobal;
}
