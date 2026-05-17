import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import './App.css';
import { AuditionKeyboard } from './components/AuditionKeyboard';
import { BootSplash } from './components/BootSplash';
import { makeDefaultPatch, PatchParams } from './components/KnobGrid';
import { MacroBar, MacroState } from './components/MacroBar';
import { ModulesGrid } from './components/ModulesGrid';
import { OnboardingOverlay } from './components/OnboardingOverlay';
import {
  BrowserEntry,
  loadStarred,
  saveStarred,
} from './components/PatchBrowser';
import { PlaySurface } from './components/PlaySurface';
import { HoodSlideOver, loadHoodOpen, saveHoodOpen } from './components/HoodSlideOver';
import { PresetsSidebar } from './components/PresetsSidebar';
import { ResizeHandle } from './components/ResizeHandle';
import { RightColumn } from './components/RightColumn';
import { ToolsDrawer } from './components/ToolsDrawer';
import { TopBar, ABSlot } from './components/TopBar';
import { useColumnLayout } from './hooks/useColumnLayout';
import { useWebSocket } from './hooks/useWebSocket';
import { usePatchHistory } from './hooks/usePatchHistory';
import { useUiAudioSettings } from './hooks/useUiAudioSettings';
import { playTapeStop, playVoicePip } from './data/uiAudio';
import {
  QUICK_START_PATCH,
  isOnboardingCompleted,
  markOnboardingCompleted,
} from './data/quickStartPreset';
import type { AgentModulationPlan, ChatMessage, PatchPreviewData } from './types/chat';
import {
  ModMatrix,
  ModConnection,
  ModSourceId,
  loadModMatrix,
  saveModMatrix,
  makeConnectionId,
  PARAM_RANGES,
  macroIndexOf,
} from './data/modulation';

type Theme = 'dark' | 'light';
type DrawerTab = 'dictionary' | 'telemetry' | 'history' | 'settings';

const KNOB_BRIDGE_URL = 'ws://localhost:9002';
const THEME_KEY = 'agentic-synth.theme.v1';
const AGENT_CONNECTION_PREFIX = 'agent-';

// Cap on session-only (unstarred) entries before the oldest is dropped.
const MAX_UNSTARRED_ENTRIES = 50;

function readStoredTheme(): Theme | null {
  if (typeof window === 'undefined') return null;
  const stored = window.localStorage.getItem(THEME_KEY);
  return stored === 'dark' || stored === 'light' ? stored : null;
}

function getInitialTheme(): Theme {
  if (typeof window === 'undefined') return 'dark';
  const stored = readStoredTheme();
  if (stored) return stored;
  return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

function makeEntryLabel(ts: number): string {
  const d = new Date(ts);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  const ss = String(d.getSeconds()).padStart(2, '0');
  return `Patch ${hh}:${mm}:${ss}`;
}

function makeEntryId(ts: number): string {
  return `pb-${ts.toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

// Phase 10 §16 — RTFM easter egg patch. Hardcoded gnarly FM tone:
// carrier 220Hz (semi = 0 + detune), modulator OSC2 ratio 3.14 with
// heavy depth, wide-open filter, slow swelling envelope. Returned as
// a full PatchParams so we can route it through handleLoadPreset.
function makeRtfmPatch(base: PatchParams): PatchParams {
  const p = JSON.parse(JSON.stringify(base)) as PatchParams;
  // OSC1 carrier — 220Hz reference.
  p.osc[0].type = 6;
  p.osc[0].enabled = 1;
  p.osc[0].volume = 0.9;
  p.osc[0].semitone_offset = -12; // an octave below default A4-ish
  p.osc[0].detune_cents = 0;
  p.osc[0].fm_ratio = 1;
  p.osc[0].fm_depth = 0.85;
  p.osc[0].pan = 0;
  // OSC2 modulator @ ratio 3.14, sprinkled wide.
  p.osc[1].type = 6;
  p.osc[1].enabled = 1;
  p.osc[1].volume = 0.4;
  p.osc[1].semitone_offset = 0;
  p.osc[1].fm_ratio = 3.14;
  p.osc[1].fm_depth = 0.95;
  p.osc[1].pan = -0.3;
  // OSC3 subharmonic shimmer.
  p.osc[2].type = 6;
  p.osc[2].enabled = 1;
  p.osc[2].volume = 0.25;
  p.osc[2].semitone_offset = 7; // perfect fifth
  p.osc[2].fm_ratio = 1.5;
  p.osc[2].fm_depth = 0.5;
  p.osc[2].pan = 0.3;
  // Filter — open, lots of drive.
  p.filter.cutoff_hz = 6200;
  p.filter.resonance = 0.55;
  p.filter.drive = 0.7;
  p.filter.env_mod = 0.3;
  // Amp env — slow swell.
  p.amp_env.attack_s = 0.4;
  p.amp_env.decay_s = 0.6;
  p.amp_env.sustain = 0.7;
  p.amp_env.release_s = 1.4;
  // LFO1 wobble on FM depth.
  p.lfo[0].rate_hz = 0.7;
  p.lfo[0].depth = 0.4;
  // Reverb to push it cathedral-shaped.
  p.reverb.size = 0.85;
  p.reverb.mix = 0.45;
  return p;
}

// Typed setters per param group. Each setter takes a literal-string
// union of the keys it accepts, so the compiler verifies that we only
// write known numeric fields — no `as Record<string, number>` casts.
type OscKey = keyof PatchParams['osc'][number];
type FilterKey = keyof PatchParams['filter'];
type EnvKey = keyof PatchParams['amp_env'];
type LfoKey = keyof PatchParams['lfo'][number];
type ReverbKey = keyof PatchParams['reverb'];
type DelayKey = keyof PatchParams['delay'];

const OSC_KEYS: ReadonlySet<OscKey> = new Set<OscKey>([
  'type', 'volume', 'detune_cents', 'semitone_offset', 'wavetable_pos',
  'fm_ratio', 'fm_depth', 'pulse_width', 'pan', 'enabled',
]);
const FILTER_KEYS: ReadonlySet<FilterKey> = new Set<FilterKey>([
  'type', 'cutoff_hz', 'resonance', 'env_mod', 'key_track', 'drive',
]);
const ENV_KEYS: ReadonlySet<EnvKey> = new Set<EnvKey>([
  'attack_s', 'decay_s', 'sustain', 'release_s',
]);
const LFO_KEYS: ReadonlySet<LfoKey> = new Set<LfoKey>([
  'waveform', 'target', 'rate_hz', 'depth', 'phase_offset', 'bpm_sync',
]);
const REVERB_KEYS: ReadonlySet<ReverbKey> = new Set<ReverbKey>([
  'size', 'damping', 'width', 'mix',
]);
const DELAY_KEYS: ReadonlySet<DelayKey> = new Set<DelayKey>([
  'time_s', 'feedback', 'mix', 'stereo', 'bpm_sync',
]);

function applyParamToPatch(patch: PatchParams, param: string, value: number): PatchParams {
  const p = JSON.parse(JSON.stringify(patch)) as PatchParams;
  const parts = param.split('.');
  if (parts[0] === 'osc' && parts.length === 3) {
    const i = parseInt(parts[1], 10);
    const k = parts[2] as OscKey;
    if (i >= 0 && i < p.osc.length && OSC_KEYS.has(k)) p.osc[i][k] = value;
  } else if (parts[0] === 'filter' && parts.length === 2) {
    const k = parts[1] as FilterKey;
    if (FILTER_KEYS.has(k)) p.filter[k] = value;
  } else if (parts[0] === 'amp_env' && parts.length === 2) {
    const k = parts[1] as EnvKey;
    if (ENV_KEYS.has(k)) p.amp_env[k] = value;
  } else if (parts[0] === 'filter_env' && parts.length === 2) {
    const k = parts[1] as EnvKey;
    if (ENV_KEYS.has(k)) p.filter_env[k] = value;
  } else if (parts[0] === 'lfo' && parts.length === 3) {
    const i = parseInt(parts[1], 10);
    const k = parts[2] as LfoKey;
    if (i >= 0 && i < p.lfo.length && LFO_KEYS.has(k)) p.lfo[i][k] = value;
  } else if (parts[0] === 'reverb' && parts.length === 2) {
    const k = parts[1] as ReverbKey;
    if (REVERB_KEYS.has(k)) p.reverb[k] = value;
  } else if (parts[0] === 'delay' && parts.length === 2) {
    const k = parts[1] as DelayKey;
    if (DELAY_KEYS.has(k)) p.delay[k] = value;
  } else if (param === 'master_gain') {
    p.master_gain = value;
  } else if (param === 'portamento_s') {
    p.portamento_s = value;
  } else if (param === 'voice_count') {
    p.voice_count = value;
  }
  return p;
}

// Flatten a patch into the dotted param-key form used on the wire.
// Used during undo/redo to diff two patches and forward only the changed
// knob_tweak frames to the C++ engine.
function flattenPatch(p: PatchParams): Record<string, number> {
  const out: Record<string, number> = {};
  p.osc.forEach((o, i) => {
    for (const [k, v] of Object.entries(o)) out[`osc.${i}.${k}`] = v as number;
  });
  for (const [k, v] of Object.entries(p.filter)) out[`filter.${k}`] = v as number;
  for (const [k, v] of Object.entries(p.amp_env)) out[`amp_env.${k}`] = v as number;
  for (const [k, v] of Object.entries(p.filter_env)) out[`filter_env.${k}`] = v as number;
  p.lfo.forEach((l, i) => {
    for (const [k, v] of Object.entries(l)) out[`lfo.${i}.${k}`] = v as number;
  });
  for (const [k, v] of Object.entries(p.reverb)) out[`reverb.${k}`] = v as number;
  for (const [k, v] of Object.entries(p.delay)) out[`delay.${k}`] = v as number;
  out['master_gain'] = p.master_gain;
  out['portamento_s'] = p.portamento_s;
  out['voice_count'] = p.voice_count;
  return out;
}

function diffPatch(prev: PatchParams, next: PatchParams): Record<string, number> {
  const a = flattenPatch(prev);
  const b = flattenPatch(next);
  const out: Record<string, number> = {};
  for (const k of Object.keys(b)) {
    if (a[k] !== b[k]) out[k] = b[k];
  }
  return out;
}

function normaliseModDestination(target: string): string {
  return target.replace(/\[(\d+)\]/g, '.$1');
}

function macroSourceForIndex(index: number): ModSourceId {
  return (`macro${Math.min(4, Math.max(1, index + 1))}`) as ModSourceId;
}

export function App() {
  // Patch state is owned by usePatchHistory. The "current" patch is always
  // history[cursor].patch — never store it in a separate setState.
  const ph = usePatchHistory(useMemo(() => makeDefaultPatch(), []));
  const patch = ph.history[ph.cursor].patch;

  // Sticky set of params edited in the most recent agent generation.
  // Persists until the NEXT agent generation arrives (no transient flash).
  const [lastAgentEditBatch, setLastAgentEditBatch] = useState<Set<string>>(new Set());
  // Monotonically-increasing token bumped each time a patch lands from a
  // non-user source (agent / preset / variation). ModulesGrid watches this
  // to open a one-shot animation window for the orchestrated knob settle.
  const [patchLoadToken, setPatchLoadToken] = useState<number>(0);
  const bumpPatchLoad = useCallback(() => setPatchLoadToken((n) => n + 1), []);
  const [transcript, setTranscript] = useState('');
  const [theme, setTheme] = useState<Theme>(getInitialTheme);
  // Boot splash visibility — tracked in state (not a ref) so React unmounts
  // the overlay after 1.8s. Initialized true on first mount only; HMR keeps
  // state across module reloads so we don't re-trigger the splash mid-dev.
  const [splashVisible, setSplashVisible] = useState<boolean>(true);

  // ToolsDrawer (Dictionary / Telemetry / History / Settings — gear icon).
  const [toolsOpen, setToolsOpen] = useState<boolean>(false);
  const [toolsTab, setToolsTab] = useState<DrawerTab>('dictionary');

  // ── Phase C onboarding (#256) ────────────────────────────────────────
  // Show the 3-step tour on first launch. Persistence via localStorage
  // (timbre.onboarding.completed). Once flipped, the overlay never
  // reappears for the life of the install — neither Skip nor Got it
  // distinguishes between "user finished" and "user dismissed".
  const [onboardingActive, setOnboardingActive] = useState<boolean>(
    () => !isOnboardingCompleted(),
  );
  // Bridge between ChatInterface's first-patch event and the overlay's
  // step-2 gate. Flipped exactly once per session, the first time a
  // `patch` wire event reaches the chat.
  const [firstPatchLanded, setFirstPatchLanded] = useState<boolean>(false);
  const handleFirstPatchLanded = useCallback(() => setFirstPatchLanded(true), []);

  // First-launch seed message — primes the chat with the cinematic
  // Kubrick pad so the producer can audition something premium the
  // moment they hit a key, without typing anything first. Built once on
  // mount; if onboarding has already been completed the seed is empty
  // (no nostalgic bubble on return visits).
  const initialChatMessages = useMemo<ChatMessage[]>(() => {
    if (isOnboardingCompleted()) return [];
    return [
      {
        id: 'quickstart-seed',
        role: 'assistant',
        content: 'Hit a key. Or describe a sound.',
        patch: QUICK_START_PATCH,
      },
    ];
  }, []);

  // Apply the quick-start patch to the engine on first mount so the
  // audition keyboard actually plays the cinematic Kubrick pad instead
  // of the bare default. Guarded so it only fires when the seed bubble
  // is present (i.e. only on first launch).
  const quickStartAppliedRef = useRef<boolean>(false);
  useEffect(() => {
    if (quickStartAppliedRef.current) return;
    if (initialChatMessages.length === 0) return;
    quickStartAppliedRef.current = true;
    // Defer one tick so usePatchHistory has mounted its initial entry
    // and lastSentEffectiveRef has primed against the default patch —
    // the engine then receives the kubrick-pad as a diff'd batch.
    // eslint-disable-next-line @typescript-eslint/no-use-before-define
    Promise.resolve().then(() => seedQuickStartPatch());
    // seedQuickStartPatch is declared below; resolved at runtime via closure
    // since this effect doesn't list it as a dep (we want to fire exactly once).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [initialChatMessages.length]);

  // Hood-open state (Phase A / #271). Session-persisted only — fresh
  // launches always show the simple play surface first per the brand's
  // first-impression contract.
  const [hoodOpen, setHoodOpen] = useState<boolean>(() => loadHoodOpen());
  useEffect(() => { saveHoodOpen(hoodOpen); }, [hoodOpen]);
  const openHood = useCallback(() => setHoodOpen(true), []);
  const closeHood = useCallback(() => setHoodOpen(false), []);

  // ── UI audio settings (Phase 10 §17) ──────────────────────────────
  // Both default OFF — TIMBRE is "mostly silent". Settings persist via
  // localStorage. The voice pip fires after a fresh transcript arrives;
  // the tape-stop thunk fires on any patch-load token bump.
  const uiAudio = useUiAudioSettings();

  // ── Easter egg state (Phase 10 §16) ───────────────────────────────
  // Spin token — bumped by Option+double-click on the wordmark. Each
  // knob watches the token and runs a 600ms synchronized 360° rotation
  // on its indicator. Gated to once-per-session via sessionStorage.
  const [spinToken, setSpinToken] = useState<number>(0);
  const triggerKnobSpin = useCallback(() => {
    try {
      if (window.sessionStorage.getItem('timbre:knob-spin-used') === '1') return;
      window.sessionStorage.setItem('timbre:knob-spin-used', '1');
    } catch {
      // session storage may throw in private modes — fall through.
    }
    setSpinToken((n) => n + 1);
  }, []);

  // A/B compare slots (Phase 6). Both seed from the current patch. The
  // "inactive" slot stores a frozen snapshot of what that slot looked
  // like last time we switched away from it. Switching slots writes the
  // current patch into the previously-active slot, then loads the newly-
  // active slot via the standard agent-batch path (so it lands on knobs,
  // bridge, AND history). Slots are session-local — not persisted.
  const [activeSlot, setActiveSlot] = useState<ABSlot>('A');
  const [inactiveSlotPatch, setInactiveSlotPatch] = useState<PatchParams>(
    () => makeDefaultPatch(),
  );

  // ── Modulation matrix (Phase 8) ───────────────────────────────────
  // Frontend-only routing for now: each connection ties a mod source
  // (LFO/ENV/Macro/Velocity/Keytrack) to a destination param key, with
  // a bipolar amount and an enable flag. Persisted to localStorage so
  // user-created routes survive reloads. The engine continues to wire
  // LFO/ENV via the existing `lfo.target` enum on the patch itself.
  const [modMatrix, setModMatrix] = useState<ModMatrix>(() => loadModMatrix());
  useEffect(() => { saveModMatrix(modMatrix); }, [modMatrix]);

  const handleAssignMod = useCallback(
    (sourceId: string, destinationKey: string) => {
      const source = sourceId as ModSourceId;
      setModMatrix((prev) => {
        // If a connection from this source to this destination already
        // exists, leave the matrix alone (avoid silently piling up
        // duplicates from accidental re-drops). Otherwise append a new
        // default connection at 0.5 amount, enabled.
        const exists = prev.connections.some(
          (c) => c.source === source && c.destination === destinationKey,
        );
        if (exists) return prev;
        const next: ModConnection = {
          id: makeConnectionId(),
          source,
          destination: destinationKey,
          amount: 0.5,
          enabled: true,
        };
        return { connections: [...prev.connections, next] };
      });
    },
    [],
  );

  const handleUpdateConnection = useCallback(
    (id: string, patch: Partial<ModConnection>) => {
      setModMatrix((prev) => ({
        connections: prev.connections.map((c) => (c.id === id ? { ...c, ...patch } : c)),
      }));
    },
    [],
  );

  const handleDeleteConnection = useCallback((id: string) => {
    setModMatrix((prev) => ({
      connections: prev.connections.filter((c) => c.id !== id),
    }));
  }, []);

  const handleAddConnection = useCallback(
    (source: ModSourceId, destination: string) => {
      setModMatrix((prev) => ({
        connections: [
          ...prev.connections,
          {
            id: makeConnectionId(),
            source,
            destination,
            amount: 0.5,
            enabled: true,
          },
        ],
      }));
    },
    [],
  );

  // Macros. 0..1 normalized values; names persist via localStorage so user
  // rename survives reloads. Agent generations may also relabel the four
  // macros and replace their own generated matrix routes.
  const MACRO_NAMES_KEY = 'timbre:macro-names';
  const [macros, setMacros] = useState<MacroState[]>(() => {
    let names: string[] = ['Macro 1', 'Macro 2', 'Macro 3', 'Macro 4'];
    try {
      const raw = window.localStorage.getItem(MACRO_NAMES_KEY);
      if (raw) {
        const parsed = JSON.parse(raw) as string[];
        if (Array.isArray(parsed) && parsed.length === 4) names = parsed;
      }
    } catch {
      // ignore
    }
    return names.map((label) => ({ label, value: 0 }));
  });

  const applyAgentModulationPlan = useCallback((plan?: AgentModulationPlan) => {
    if (!plan) return;
    const agentConnections: ModConnection[] = [];
    const labels: Array<string | null> = [null, null, null, null];
    const ts = Date.now().toString(36);

    if (Array.isArray(plan.macros)) {
      plan.macros.slice(0, 4).forEach((macro, macroIndex) => {
        const label = (macro.name ?? macro.label ?? '').trim();
        if (label) labels[macroIndex] = label;

        macro.routes?.forEach((route, routeIndex) => {
          const destination = normaliseModDestination(route.target);
          if (!PARAM_RANGES[destination]) return;
          agentConnections.push({
            id: `${AGENT_CONNECTION_PREFIX}${ts}-${macroIndex}-${routeIndex}`,
            source: macroSourceForIndex(macroIndex),
            destination,
            amount: Math.max(-1, Math.min(1, route.amount)),
            enabled: true,
          });
        });
      });
    }

    if (labels.some(Boolean)) {
      setMacros((prev) =>
        prev.map((macro, index) => (labels[index] ? { ...macro, label: labels[index] as string } : macro)),
      );
    }

    if (agentConnections.length === 0) return;
    setModMatrix((prev) => ({
      connections: [
        ...prev.connections.filter((c) => !c.id.startsWith(AGENT_CONNECTION_PREFIX)),
        ...agentConnections,
      ],
    }));
  }, []);

  // Apply theme to <html data-theme="..."> on every change so all CSS custom
  // properties switch atomically. No persistence on mount — only on explicit
  // user toggle, so OS-preference users keep tracking the OS.
  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme);
  }, [theme]);

  // Follow OS preference mid-session ONLY when the user hasn't picked a theme.
  useEffect(() => {
    const mql = window.matchMedia('(prefers-color-scheme: light)');
    const onChange = (e: MediaQueryListEvent) => {
      if (readStoredTheme()) return; // user has an explicit pick
      setTheme(e.matches ? 'light' : 'dark');
    };
    mql.addEventListener('change', onChange);
    return () => mql.removeEventListener('change', onChange);
  }, []);

  const toggleTheme = useCallback(() => {
    setTheme((prev) => {
      const next: Theme = prev === 'dark' ? 'light' : 'dark';
      try {
        window.localStorage.setItem(THEME_KEY, next);
      } catch {
        // localStorage may throw in private modes; ignore.
      }
      return next;
    });
  }, []);

  // Patch browser entries. Starred entries persist via localStorage; unstarred
  // are session-only and capped at MAX_UNSTARRED_ENTRIES.
  const [browserEntries, setBrowserEntries] = useState<BrowserEntry[]>(() => loadStarred());
  // Tracks the timestamp of the last agent-sourced history entry we've already
  // captured into the browser, so re-renders don't double-add the same patch.
  const lastCapturedAgentTs = useRef<number>(0);
  // Set during a browser-driven recall so the capture effect skips the
  // resulting history push (otherwise recall would clone the entry).
  const suppressCaptureRef = useRef<boolean>(false);

  const { lastMessage, sendMessage, sendBinary, readyState } = useWebSocket(KNOB_BRIDGE_URL);

  // ── Macro projection (Phase 13) ───────────────────────────────────
  // Apply the sum of all enabled macro ModConnections on top of the
  // base patch. Each connection contributes:
  //
  //   delta = macro.value (0..1) * connection.amount (-1..1) * paramRange
  //
  // The result is clamped to the destination param's natural [min,max]
  // range. Returns the base reference when nothing applies (so React
  // identity stays stable and the no-op path doesn't allocate).
  //
  // Sums-across-macros: multiple macros into the same destination
  // accumulate into one delta before being applied & clamped, so the
  // ordering of connections in the matrix doesn't matter.
  const computeEffectivePatch = useCallback(
    (base: PatchParams, mat: ModMatrix, macs: MacroState[]): PatchParams => {
      const deltas: Record<string, number> = {};
      let anyApplied = false;
      for (const c of mat.connections) {
        if (!c.enabled) continue;
        const idx = macroIndexOf(c.source);
        if (idx < 0) continue;
        const macVal = macs[idx]?.value ?? 0;
        if (macVal === 0 || c.amount === 0) continue;
        const range = PARAM_RANGES[c.destination];
        if (!range) continue;
        const span = range.max - range.min;
        const delta = macVal * c.amount * span;
        deltas[c.destination] = (deltas[c.destination] ?? 0) + delta;
        anyApplied = true;
      }
      if (!anyApplied) return base;
      const baseFlat = flattenPatch(base);
      let next = base;
      for (const [dest, d] of Object.entries(deltas)) {
        const baseVal = baseFlat[dest];
        if (baseVal === undefined) continue;
        const range = PARAM_RANGES[dest];
        const raw = baseVal + d;
        const clamped = Math.max(range.min, Math.min(range.max, raw));
        next = applyParamToPatch(next, dest, clamped);
      }
      return next;
    },
    [],
  );

  // Effective patch = base patch + active macro modulation. This is what
  // the C++ engine actually hears.
  const effectivePatch = useMemo(
    () => computeEffectivePatch(patch, modMatrix, macros),
    [patch, modMatrix, macros, computeEffectivePatch],
  );

  // Single-source-of-truth bridge dispatch. Every change to the
  // effective patch — whether driven by knob move, agent batch, preset
  // load, A/B switch, undo/redo, or a macro turning — is diffed against
  // what we last sent to the engine and forwarded as knob_tweak frames.
  //
  // Macro modulation is intentionally NOT pushed into history: history
  // tracks the base patch only, so undo reverts the committed musical
  // state and the macro automation re-applies on top.
  const lastSentEffectiveRef = useRef<PatchParams | null>(null);
  useEffect(() => {
    const prev = lastSentEffectiveRef.current;
    if (prev === null) {
      // First render: prime the ref but don't broadcast — the engine's
      // own init state matches ours.
      lastSentEffectiveRef.current = effectivePatch;
      return;
    }
    if (prev === effectivePatch) return;
    const diff = diffPatch(prev, effectivePatch);
    lastSentEffectiveRef.current = effectivePatch;
    for (const [p, v] of Object.entries(diff)) {
      sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
    }
  }, [effectivePatch, sendMessage]);

  // Apply a batch of param edits as if the agent had just produced them.
  // Replaces lastAgentEditBatch so the sticky badge tracks only the latest generation.
  // Engine notification is handled by the effectivePatch effect.
  const applyAgentBatch = useCallback(
    (params: Record<string, number>, source: 'agent' | 'variation' = 'agent') => {
      let next = patch;
      for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
      ph.push(next, source);
      setLastAgentEditBatch(new Set(Object.keys(params)));
      bumpPatchLoad();
    },
    [patch, ph, bumpPatchLoad],
  );

  useEffect(() => {
    if (!lastMessage) return;
    try {
      const msg = JSON.parse(lastMessage) as Record<string, unknown>;

      if (msg.type === 'patch_update' && msg.patch && typeof msg.patch === 'object') {
        const fullPatch = msg.patch as PatchParams;
        const params = diffPatch(patch, fullPatch);
        if (Object.keys(params).length === 0) {
          applyAgentModulationPlan(msg.modulation as AgentModulationPlan | undefined);
          return;
        }
        ph.push(fullPatch, 'agent');
        setLastAgentEditBatch(new Set(Object.keys(params)));
        bumpPatchLoad();
        applyAgentModulationPlan(msg.modulation as AgentModulationPlan | undefined);
      } else if (msg.type === 'patch_update' && msg.params && typeof msg.params === 'object') {
        const params = msg.params as Record<string, number>;
        let next = patch;
        for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
        ph.push(next, 'agent');
        // Sticky: replace the batch wholesale; no timer-driven clear.
        setLastAgentEditBatch(new Set(Object.keys(params)));
        bumpPatchLoad();
        applyAgentModulationPlan(msg.modulation as AgentModulationPlan | undefined);
      } else if (msg.type === 'transcript' && typeof msg.text === 'string') {
        setTranscript(msg.text as string);
      }
    } catch {
      // ignore malformed frames
    }
    // We intentionally depend only on lastMessage; reading `patch` and `ph`
    // through closure is fine because pushes always derive from the latest
    // render's state, and stale reads only show up if multiple WS frames
    // arrive in the same render — not the case here.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [lastMessage]);

  // Watch ph.history for newly-pushed `agent`/`variation` entries and snapshot
  // each into the patch browser. Tracked by timestamp so re-renders don't
  // double-record. Non-starred entries are capped; oldest unstarred is dropped.
  useEffect(() => {
    const top = ph.history[ph.cursor];
    if (!top) return;
    if (top.source !== 'agent' && top.source !== 'variation') return;
    if (top.timestamp <= lastCapturedAgentTs.current) return;
    lastCapturedAgentTs.current = top.timestamp;
    // Skip captures triggered by a browser recall (we don't want to clone
    // the same entry forever each time it's recalled).
    if (suppressCaptureRef.current) {
      suppressCaptureRef.current = false;
      return;
    }

    const ts = top.timestamp;
    const newEntry: BrowserEntry = {
      id: makeEntryId(ts),
      label: makeEntryLabel(ts),
      patch: top.patch,
      timestamp: ts,
      starred: false,
    };

    setBrowserEntries((prev) => {
      const next = [newEntry, ...prev];
      // Cap unstarred entries: keep all starred, plus up to N newest unstarred.
      const starred = next.filter((e) => e.starred);
      const unstarred = next.filter((e) => !e.starred);
      const trimmedUnstarred = unstarred.slice(0, MAX_UNSTARRED_ENTRIES);
      return [...trimmedUnstarred, ...starred].sort((a, b) => b.timestamp - a.timestamp);
    });
  }, [ph.history, ph.cursor]);

  // Persist only starred entries to localStorage on every change.
  useEffect(() => {
    saveStarred(browserEntries);
  }, [browserEntries]);

  // Recall a patch from the browser: route through applyAgentBatch so it lands
  // on knobs, the bridge, AND the undo/redo history (as a 'variation' entry).
  const handleBrowserSelect = useCallback(
    (selected: PatchParams) => {
      // Diff selected against the current patch to produce a param map.
      // Reusing diffPatch keeps the wire traffic minimal (only changed params).
      const diff = diffPatch(patch, selected);
      if (Object.keys(diff).length === 0) return;
      suppressCaptureRef.current = true;
      applyAgentBatch(diff, 'variation');
    },
    [applyAgentBatch, patch],
  );

  const handleBrowserStar = useCallback((id: string) => {
    setBrowserEntries((prev) =>
      prev.map((e) => (e.id === id ? { ...e, starred: !e.starred } : e)),
    );
  }, []);

  const handleBrowserRename = useCallback((id: string, newLabel: string) => {
    setBrowserEntries((prev) =>
      prev.map((e) => (e.id === id ? { ...e, label: newLabel } : e)),
    );
  }, []);

  const handleBrowserClear = useCallback(() => {
    setBrowserEntries((prev) => prev.filter((e) => e.starred));
  }, []);

  const handleKnobChange = useCallback(
    (param: string, value: number) => {
      const next = applyParamToPatch(patch, param, value);
      ph.push(next, 'user');
      // Engine notification handled by the effectivePatch effect.
    },
    [patch, ph],
  );

  // Wired into ChatInterface — "Use A" / "Use B" buttons hand the chosen
  // variation's preview data back here, where we treat it like a fresh agent batch.
  const handleSelectVariation = useCallback(
    (preview: PatchPreviewData, modulation?: AgentModulationPlan) => {
      applyAgentBatch(diffPatch(patch, preview), 'variation');
      applyAgentModulationPlan(modulation);
    },
    [applyAgentBatch, applyAgentModulationPlan, patch],
  );

  const handleAudio = useCallback(
    (buf: ArrayBuffer) => {
      sendBinary(buf);
    },
    [sendBinary],
  );

  // ── Preset load (Phase 6) ──────────────────────────────────────────
  // Sidebar hands us a full PatchParams. Diff against current to forward
  // only the changed knobs over the WS bridge, then push as a 'preset'
  // entry so undo/redo and history capture work uniformly. Suppress the
  // browser-capture so we don't snapshot the recall itself.
  const handleLoadPreset = useCallback(
    (next: PatchParams) => {
      const diff = diffPatch(patch, next);
      if (Object.keys(diff).length === 0) return;
      let nextPatch = patch;
      for (const [p, v] of Object.entries(diff)) {
        nextPatch = applyParamToPatch(nextPatch, p, v);
      }
      suppressCaptureRef.current = true;
      ph.push(nextPatch, 'preset');
      bumpPatchLoad();
      // Engine notification handled by the effectivePatch effect.
    },
    [patch, ph, bumpPatchLoad],
  );

  // ── Preset audition (Phase 13) ────────────────────────────────────
  // Ephemerally push a preset to the engine WITHOUT touching React
  // history/patch state. Used by PresetsSidebar's hover-preview: a
  // 300ms timer fires after pointer-enter; on pointer-leave the prior
  // engine state is restored via cancelAudition().
  //
  // We diff against `lastSentEffectiveRef` (what the engine currently
  // hears) rather than `patch`, so subsequent macro changes or knob
  // moves during audition don't double-overwrite.
  const auditionRevertRef = useRef<Record<string, number> | null>(null);
  const auditionPreset = useCallback(
    (next: PatchParams) => {
      const cur = lastSentEffectiveRef.current ?? patch;
      // If a prior audition is still active, keep its revert map (we
      // want to restore to the pre-audition state, not to the prior
      // audition's preset).
      if (!auditionRevertRef.current) {
        const revert: Record<string, number> = {};
        const curFlat = flattenPatch(cur);
        const nextFlat = flattenPatch(next);
        for (const k of Object.keys(nextFlat)) {
          if (curFlat[k] !== nextFlat[k]) revert[k] = curFlat[k];
        }
        auditionRevertRef.current = revert;
      }
      // Push the preset values to the engine directly.
      const diff = diffPatch(cur, next);
      for (const [p, v] of Object.entries(diff)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [patch, sendMessage],
  );

  const cancelAudition = useCallback(() => {
    const revert = auditionRevertRef.current;
    if (!revert) return;
    auditionRevertRef.current = null;
    for (const [p, v] of Object.entries(revert)) {
      sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
    }
  }, [sendMessage]);

  // When a hover audition is confirmed (click), forget the revert map
  // so the normal handleLoadPreset path commits cleanly.
  const commitAudition = useCallback(() => {
    auditionRevertRef.current = null;
  }, []);

  // Phase 10 §16 — RTFM easter egg. ChatInterface detects the prompt
  // locally and calls back here; we synthesise a gnarly FM patch from
  // the current patch baseline and route it through the standard preset
  // load path so all the normal animations + history capture run.
  const handleRtfmEasterEgg = useCallback(() => {
    handleLoadPreset(makeRtfmPatch(patch));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [patch]);

  // Phase C onboarding (#256) — seed the engine with the quick-start
  // patch so the audition keyboard plays the cinematic Kubrick pad on
  // first launch without any user input. Wired via handleLoadPreset so
  // history + browser capture stays consistent. Casting through the
  // PatchPreviewData shape works because QUICK_START_PATCH is itself a
  // full PatchParams.
  const seedQuickStartPatch = useCallback(() => {
    handleLoadPreset(QUICK_START_PATCH as PatchParams);
  }, [handleLoadPreset]);

  // ── Voice transcribe pip (Phase 10 §17) ──────────────────────────
  // Whenever a fresh transcript arrives AND the user has opted in,
  // play a 40ms pip. Skip the very first render (transcript = '').
  const lastTranscriptRef = useRef<string>('');
  useEffect(() => {
    const prev = lastTranscriptRef.current;
    lastTranscriptRef.current = transcript;
    if (!transcript) return;
    if (transcript === prev) return;
    if (uiAudio.voicePip) playVoicePip();
  }, [transcript, uiAudio.voicePip]);

  // ── Patch-load tape-stop thunk (Phase 10 §17) ────────────────────
  // Fires on every patchLoadToken bump (preset, agent, A/B, RTFM) when
  // the user has opted in. Skip the initial token value of 0.
  const lastPatchTokenRef = useRef<number>(0);
  useEffect(() => {
    const prev = lastPatchTokenRef.current;
    lastPatchTokenRef.current = patchLoadToken;
    if (patchLoadToken === prev) return;
    if (patchLoadToken === 0) return;
    if (uiAudio.patchThunk) playTapeStop();
  }, [patchLoadToken, uiAudio.patchThunk]);

  // ── A/B slot switching (Phase 6) ───────────────────────────────────
  // Pattern: when toggling, snapshot the current patch into the "leaving"
  // slot, then load the "entering" slot via the same diff+push path. The
  // shared history continues to track everything as a 'preset' entry
  // (semantically: a slot recall *is* a preset recall).
  const switchSlot = useCallback(
    (target: ABSlot) => {
      if (target === activeSlot) return;
      const cur = patch;
      const incoming = inactiveSlotPatch;
      setInactiveSlotPatch(cur);
      setActiveSlot(target);
      // Load the incoming slot's patch. Diff vs current and push.
      const diff = diffPatch(cur, incoming);
      if (Object.keys(diff).length === 0) return;
      let nextPatch = cur;
      for (const [p, v] of Object.entries(diff)) {
        nextPatch = applyParamToPatch(nextPatch, p, v);
      }
      suppressCaptureRef.current = true;
      ph.push(nextPatch, 'preset');
      bumpPatchLoad();
      // Engine notification handled by the effectivePatch effect.
    },
    [activeSlot, inactiveSlotPatch, patch, ph, bumpPatchLoad],
  );

  // Copy active slot's patch into the inactive slot.
  const handleCopySlot = useCallback(() => {
    setInactiveSlotPatch(JSON.parse(JSON.stringify(patch)) as PatchParams);
  }, [patch]);

  // Spacebar toggles A/B (skip when an editable target has focus).
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.code !== 'Space' && e.key !== ' ') return;
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
        return;
      }
      e.preventDefault();
      switchSlot(activeSlot === 'A' ? 'B' : 'A');
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [activeSlot, switchSlot]);

  // ── Macros ─────────────────────────────────────────────────────────
  // Macro values project through enabled mod-matrix routes into the
  // effective patch above. Persist explicit label changes so renames
  // survive page reloads.
  const handleMacroChange = useCallback((index: number, value: number) => {
    setMacros((prev) => {
      const next = prev.slice();
      next[index] = { ...next[index], value };
      return next;
    });
  }, []);

  const handleMacroRename = useCallback((index: number, label: string) => {
    setMacros((prev) => {
      const next = prev.slice();
      next[index] = { ...next[index], label };
      try {
        window.localStorage.setItem(
          MACRO_NAMES_KEY,
          JSON.stringify(next.map((m) => m.label)),
        );
      } catch {
        // ignore
      }
      return next;
    });
  }, []);

  // Undo/redo just walk the history cursor — the effectivePatch effect
  // observes the resulting base-patch change and forwards the diff to
  // the engine.
  const handleUndo = useCallback(() => {
    ph.undo();
  }, [ph]);

  const handleRedo = useCallback(() => {
    ph.redo();
  }, [ph]);

  // Cmd/Ctrl+Z = undo, Cmd/Ctrl+Shift+Z = redo. Skip when an editable target
  // (INPUT/TEXTAREA/contenteditable) has focus — same pattern as
  // AuditionKeyboard's global key listener.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (!(e.metaKey || e.ctrlKey)) return;
      if (e.key.toLowerCase() !== 'z') return;
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
        return;
      }
      e.preventDefault();
      if (e.shiftKey) handleRedo();
      else handleUndo();
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [handleUndo, handleRedo]);

  // Cmd/Ctrl+K = focus the AI prompt input. Skip when an editable target
  // already has focus (so users typing in another input aren't yanked away)
  // and skip when the Tools drawer is open (it owns Cmd+K for its own
  // search). The prompt input is identified by class .prompt-input — added
  // by ChatInterface — and we focus the first match in the document.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (!(e.metaKey || e.ctrlKey)) return;
      if (e.key.toLowerCase() !== 'k') return;
      if (toolsOpen) return; // let the drawer claim Cmd+K when it's open
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
        return;
      }
      const promptEl = document.querySelector<HTMLTextAreaElement>('.prompt-input');
      if (!promptEl) return;
      e.preventDefault();
      promptEl.focus();
      // Scroll into view in case the chat dock is partially clipped.
      promptEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [toolsOpen]);

  // AuditionKeyboard "ready" — true once the bridge is connected so the
  // user knows their click will actually trigger a note. WebSocket.OPEN === 1.
  const auditionReady = readyState === 1;

  // ── Column layout (Phase 14 — drag-to-resize) ─────────────────────
  // Drives .app-body's grid template via CSS vars on the host node.
  // Widths persist to localStorage and survive reloads.
  const cols = useColumnLayout();

  return (
    <div className="app-layout">
      {splashVisible ? <BootSplash onDone={() => setSplashVisible(false)} /> : null}

      <TopBar
        onUndo={handleUndo}
        onRedo={handleRedo}
        canUndo={ph.canUndo}
        canRedo={ph.canRedo}
        cursor={ph.cursor}
        historyLength={ph.history.length}
        theme={theme}
        onToggleTheme={toggleTheme}
        onOpenTools={() => setToolsOpen(true)}
        activeSlot={activeSlot}
        onSelectSlot={switchSlot}
        onCopySlot={handleCopySlot}
        onAltDoubleClickLogo={triggerKnobSpin}
      />

      <MacroBar
        macros={macros}
        onMacroChange={handleMacroChange}
        onMacroRename={handleMacroRename}
      />

      <div className="app-body" ref={cols.bindHost}>
        <PresetsSidebar
          currentPatch={patch}
          onLoadPreset={handleLoadPreset}
          onAuditionStart={auditionPreset}
          onAuditionEnd={cancelAudition}
          onAuditionCommit={commitAudition}
        />
        <ResizeHandle
          ariaLabel="Resize presets sidebar"
          side="left"
          currentPx={cols.leftPx}
          onResize={cols.setLeftPx}
        />
        <div className="app-center-column">
          <PlaySurface
            macros={macros}
            onMacroChange={handleMacroChange}
            patch={patch}
            onKnobChange={handleKnobChange}
            onOpenHood={openHood}
            activeSlot={activeSlot}
            onSelectSlot={switchSlot}
            onToggleSlot={() => switchSlot(activeSlot === 'A' ? 'B' : 'A')}
          />
          <HoodSlideOver open={hoodOpen} onClose={closeHood}>
            <ModulesGrid
              patch={patch}
              agentKeys={lastAgentEditBatch}
              onKnobChange={handleKnobChange}
              patchLoadToken={patchLoadToken}
              modMatrix={modMatrix}
              onAssignMod={handleAssignMod}
              spinToken={spinToken}
              onUpdateConnection={handleUpdateConnection}
              onDeleteConnection={handleDeleteConnection}
              onAddConnection={handleAddConnection}
            />
          </HoodSlideOver>
        </div>
        <ResizeHandle
          ariaLabel="Resize right column"
          side="right"
          currentPx={cols.rightPx}
          onResize={cols.setRightPx}
        />
        <RightColumn
          externalTranscript={transcript}
          onAudio={handleAudio}
          onSelectVariation={handleSelectVariation}
          onRtfmEasterEgg={handleRtfmEasterEgg}
          initialMessages={initialChatMessages}
          onFirstPatchLanded={handleFirstPatchLanded}
        />
      </div>

      <div className="app-keyboard">
        <AuditionKeyboard sendRaw={sendMessage} ready={auditionReady} />
      </div>

      <ToolsDrawer
        open={toolsOpen}
        onClose={() => setToolsOpen(false)}
        activeTab={toolsTab}
        onTabChange={setToolsTab}
        sendMessage={sendMessage}
        lastMessage={lastMessage}
        browserEntries={browserEntries}
        onBrowserSelect={handleBrowserSelect}
        onBrowserStar={handleBrowserStar}
        onBrowserRename={handleBrowserRename}
        onBrowserClear={handleBrowserClear}
        voicePip={uiAudio.voicePip}
        patchThunk={uiAudio.patchThunk}
        onVoicePipChange={uiAudio.setVoicePip}
        onPatchThunkChange={uiAudio.setPatchThunk}
      />

      {onboardingActive && (
        <OnboardingOverlay
          patchLanded={firstPatchLanded}
          onComplete={() => {
            markOnboardingCompleted();
            setOnboardingActive(false);
          }}
        />
      )}
    </div>
  );
}
