// ── Web-demo JUCE shim (issue #280) ─────────────────────────────────
//
// Fakes `window.__JUCE__` (the object JUCE 8's WebBrowserComponent with
// withNativeIntegrationEnabled(true) injects) so the unmodified React UI
// runs in a plain browser with no plugin backend. demo/bootstrap.ts
// installs this at module-evaluation time ONLY when window.__JUCE__ is
// absent — inside the WebView the real backend wins and this module is
// dead code.
//
// Wire parity with useSynthBridge.ts:48-74:
//   • JS → native: emitEvent('__juce__invoke', { name, params: [positional],
//     resultId }) — the shim resolves each call by emitting
//     '__juce__complete' { promiseId: resultId, result }.
//   • addEventListener returns numeric token ids; removeEventListener(id)
//     detaches; event payloads reach listeners synchronously and in order.
//
// There are zero side effects at import time: nothing runs until
// installWebDemoShim() is called from demo/bootstrap.ts, and the synth
// engine is only constructed at install (which in the plugin build never
// happens).
//
// Local persistence (dictionary / feedback / telemetry opt-in) lives in
// localStorage under agentic-synth.demo.* keys — best-effort, try/catch
// guarded for private-mode / quota failures.

import { makeDefaultPatch, type AgentModulationPlan, type PatchParams } from '@agentic-synth/shared-types';
import { createSynthEngine, setPatchParam, type SynthEngine } from '@agentic-synth/engine-bridge';
import { runGenerateFlow } from './generateFlow';
import { validatePatch } from './patchCodec';

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
const PRESETS_KEY = 'agentic-synth.demo.presets.v1';

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

// Mirrors StoredPresetWire in PresetList.tsx (PatchPreviewData === PatchParams).
interface StoredPreset {
  name: string;
  prompt?: string;
  created_ms?: number;
  patch: PatchParams;
}

function readPresets(): StoredPreset[] {
  return readJson<StoredPreset[]>(PRESETS_KEY, []);
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

  // Layout hook: the web-demo stylesheet keys off this class. Present
  // only in the browser demo — the plugin WebView never installs this
  // shim, so it never gets the class.
  document.body.classList.add('web-demo');

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

  const applyServerPatch = (patch: PatchParams, modulation?: AgentModulationPlan): void => {
    engine.setPatch(patch); // then
    engine.applyMacros(modulation ?? {}); // macros project onto the patch
    currentPatch = clonePatch(patch);
  };

  const emitError = (message: string): void => {
    emitEvent('error', { message });
  };

  // Lazily starts the engine so notes work without a prior successful
  // generate — the keydown/click that triggered the note IS the user
  // gesture, so the autoplay policy is satisfied here (the old
  // engineReady gate made every key silent after a failed generate).
  // A failed start emits the existing `error` event once (key repeat
  // must not spam the chat) until a later start succeeds.
  let startErrorEmitted = false;
  const ensureEngineForNotes = async (): Promise<boolean> => {
    try {
      await engine.ensureStarted();
      startErrorEmitted = false;
      return true;
    } catch {
      if (!startErrorEmitted) {
        startErrorEmitted = true;
        emitError('Could not start the audio engine — check that this browser allows audio, then try again.');
      }
      return false;
    }
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
      void runGenerateFlow({ emit: emitEvent, engine, applyServerPatch }, prompt);
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

    note_on: async (params) => {
      const note = asInt(params[0]);
      if (note === null || !(await ensureEngineForNotes())) return undefined;
      engine.noteOn(note, clampVelocity(params[1]));
      return undefined;
    },

    note_off: async (params) => {
      const note = asInt(params[0]);
      if (note === null || !(await ensureEngineForNotes())) return undefined;
      engine.noteOff(note);
      return undefined;
    },

    play_midi_note: async (params) => {
      const note = asInt(params[0]);
      const durationMs = asInt(params[2]);
      if (note === null || durationMs === null || !(await ensureEngineForNotes())) return undefined;
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

    // ── Phase D presets (#260) — localStorage stand-in for PresetStore ──
    commit_preset: (params) => {
      const name = params[0];
      const prompt = params[1];
      const patch = params[2];
      if (typeof name !== 'string' || name.trim().length === 0) return undefined;
      // Fail-closed like the plugin's PresetStore: never persist a patch
      // the engine could not load later.
      if (!validatePatch(patch).ok) return undefined;
      const trimmed = name.trim();
      const now = Date.now();
      const presets = readPresets().filter((p) => p.name !== trimmed);
      presets.push({
        name: trimmed,
        prompt: typeof prompt === 'string' ? prompt : '',
        created_ms: now,
        patch: patch as PatchParams,
      });
      writeJson(PRESETS_KEY, presets);
      emitEvent('preset_committed', { name: trimmed, created_ms: now });
      return undefined;
    },

    get_presets: () => ({ presets: readPresets() }),

    delete_preset: (params) => {
      const name = params[0];
      if (typeof name !== 'string') return undefined;
      writeJson(PRESETS_KEY, readPresets().filter((p) => p.name !== name));
      return undefined;
    },

    // Offline bounce needs a render host — out of scope for the browser
    // demo. Surface an honest bounce_complete so the UI toasts instead
    // of appearing to do nothing.
    bounce_patch: () => {
      emitEvent('bounce_complete', {
        ok: false,
        error: 'Bounce to wav is not available in the web demo.',
      });
      return undefined;
    },

    // ── Phase B morph (#249) — needs the agent backend; no-op in demo ──
    morph_request: () => undefined,

    // ── Phase G MIDI learn (#262) — needs the native MidiLearnStore ──
    // (Web MIDI routing into the demo engine is out of scope). The knob
    // context menu's clear path also updates its React state locally, so
    // a no-op here keeps the menu consistent.
    start_midi_learn: () => undefined,
    cancel_midi_learn: () => undefined,
    clear_midi_mapping: () => undefined,
    // The App-level mapping mirror starts empty and is updated via
    // `midi_learned` events only — an empty record is the accurate answer.
    get_midi_mappings: () => ({ mappings: {} }),

    // ── Phase H telemetry (#261) — append-only JSONL on native; no-op ──
    record_variation_picked: () => undefined,
    record_macro_tweak: () => undefined,
    record_ab_toggle: () => undefined,

    // ── Audio device settings ──
    // The web demo supports OUTPUT device selection only (push-to-talk
    // STT is out of scope, and the panel's MIDI input section uses Web
    // MIDI directly, not the bridge). `audio_settings_supported` mirrors
    // the panel's boolean contract and reports real setSinkId capability
    // so the section stays hidden on browsers without it.
    audio_settings_supported: () =>
      typeof AudioContext !== 'undefined' && 'setSinkId' in AudioContext.prototype,

    // Same shape as the standalone picker's wire surface, minus sample
    // rate / buffer size: { deviceId, label } per audiooutput device.
    // Labels are blank until the user grants mic permission — fall back
    // to a stable "Output N" so the picker is never empty-looking.
    list_output_devices: async () => {
      if (!navigator.mediaDevices?.enumerateDevices) return [];
      try {
        const devices = await navigator.mediaDevices.enumerateDevices();
        let n = 0;
        return devices
          .filter((d) => d.kind === 'audiooutput')
          .map((d) => ({ deviceId: d.deviceId, label: d.label || `Output ${++n}` }));
      } catch {
        return [];
      }
    },

    // Applies the selection through the engine's AudioContext. Resolves
    // false so SettingsPanel can surface its error state instead of
    // appearing to do nothing.
    set_output_device: async (params) => {
      const deviceId = params[0];
      if (typeof deviceId !== 'string') return false;
      try {
        await engine.setOutputDevice(deviceId);
        return true;
      } catch {
        return false;
      }
    },

    // The standalone dialog has no demo equivalent; the panel's web-demo
    // path renders an inline device picker instead of this button.
    open_audio_settings: () => false,

    // Speech-to-text is out of scope for the web demo; acknowledge the
    // audio so the push-to-talk UI resolves instead of hanging.
    push_audio_pcm: () => {
      emitEvent('transcript', { text: '[mic ready but speech-to-text disabled in web demo]' });
      return undefined;
    },
  };

  // ── promise plumbing (mirrors the JUCE getNativeFunction protocol) ──
  // One warning per unknown function name — a UI path that polls every
  // frame must not flood the console.
  const warnedUnknownFns = new Set<string>();
  addEventListener('__juce__invoke', (payload) => {
    const p = isRecord(payload) ? payload : {};
    const name = typeof p.name === 'string' ? p.name : null;
    const resultId = typeof p.resultId === 'number' ? p.resultId : null;
    if (name === null || resultId === null) return;
    const params = Array.isArray(p.params) ? p.params : [];
    const handler = nativeFns[name];
    let result: unknown = undefined;
    if (!handler) {
      if (!warnedUnknownFns.has(name)) {
        warnedUnknownFns.add(name);
        console.warn('[demo-shim] unhandled native function:', name);
      }
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
