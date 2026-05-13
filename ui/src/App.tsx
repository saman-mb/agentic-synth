import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import './App.css';
import { AuditionKeyboard } from './components/AuditionKeyboard';
import { BootSplash } from './components/BootSplash';
import { makeDefaultPatch, PatchParams } from './components/KnobGrid';
import { MacroBar, MacroState } from './components/MacroBar';
import { ModulesGrid } from './components/ModulesGrid';
import {
  BrowserEntry,
  loadStarred,
  saveStarred,
} from './components/PatchBrowser';
import { PresetsSidebar } from './components/PresetsSidebar';
import { RightColumn } from './components/RightColumn';
import { ToolsDrawer } from './components/ToolsDrawer';
import { TopBar, ABSlot } from './components/TopBar';
import { useWebSocket } from './hooks/useWebSocket';
import { usePatchHistory } from './hooks/usePatchHistory';
import type { PatchPreviewData } from './types/chat';

type Theme = 'dark' | 'light';
type DrawerTab = 'dictionary' | 'telemetry' | 'history';

const KNOB_BRIDGE_URL = 'ws://localhost:9002';
const THEME_KEY = 'agentic-synth.theme.v1';

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

function applyParamToPatch(patch: PatchParams, param: string, value: number): PatchParams {
  const p = JSON.parse(JSON.stringify(patch)) as PatchParams;
  const parts = param.split('.');
  if (parts[0] === 'osc' && parts.length === 3) {
    const i = parseInt(parts[1], 10);
    if (i >= 0 && i < p.osc.length) (p.osc[i] as Record<string, number>)[parts[2]] = value;
  } else if (parts[0] === 'filter' && parts.length === 2) {
    (p.filter as Record<string, number>)[parts[1]] = value;
  } else if (parts[0] === 'amp_env' && parts.length === 2) {
    (p.amp_env as Record<string, number>)[parts[1]] = value;
  } else if (parts[0] === 'filter_env' && parts.length === 2) {
    (p.filter_env as Record<string, number>)[parts[1]] = value;
  } else if (parts[0] === 'lfo' && parts.length === 3) {
    const i = parseInt(parts[1], 10);
    if (i >= 0 && i < p.lfo.length) (p.lfo[i] as Record<string, number>)[parts[2]] = value;
  } else if (parts[0] === 'reverb' && parts.length === 2) {
    (p.reverb as Record<string, number>)[parts[1]] = value;
  } else if (parts[0] === 'delay' && parts.length === 2) {
    (p.delay as Record<string, number>)[parts[1]] = value;
  } else if (param === 'master_gain') {
    p.master_gain = value;
  } else if (param === 'portamento_s') {
    p.portamento_s = value;
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

// Map the chat-side PatchPreviewData shape onto the full PatchParams param keys
// the knob grid + transport understand. Used when committing an A/B variation.
function previewToParamMap(p: PatchPreviewData): Record<string, number> {
  return {
    'filter.cutoff_hz': p.cutoffHz,
    'filter.resonance': p.resonance,
    'amp_env.attack_s': p.attackS,
    'amp_env.sustain': p.sustainLevel,
    'lfo.0.depth': p.lfoDepth,
    'reverb.mix': p.reverbMix,
  };
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

  // ToolsDrawer (Dictionary / Telemetry / History — opened via gear icon).
  const [toolsOpen, setToolsOpen] = useState<boolean>(false);
  const [toolsTab, setToolsTab] = useState<DrawerTab>('dictionary');

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

  // Macros (Phase 6 — visual + rename). Routing to params is deferred
  // to Phase 8 (mod matrix). 0..1 normalized values; names persist via
  // localStorage so user rename survives reloads.
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

  // Apply a batch of param edits as if the agent had just produced them.
  // Replaces lastAgentEditBatch so the sticky badge tracks only the latest generation.
  const applyAgentBatch = useCallback(
    (params: Record<string, number>, source: 'agent' | 'variation' = 'agent') => {
      let next = patch;
      for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
      ph.push(next, source);
      setLastAgentEditBatch(new Set(Object.keys(params)));
      bumpPatchLoad();
      // Forward to the audio bridge so the C++ engine sees the change too.
      for (const [p, v] of Object.entries(params)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [patch, ph, sendMessage, bumpPatchLoad],
  );

  useEffect(() => {
    if (!lastMessage) return;
    try {
      const msg = JSON.parse(lastMessage) as Record<string, unknown>;

      if (msg.type === 'patch_update' && msg.params && typeof msg.params === 'object') {
        const params = msg.params as Record<string, number>;
        let next = patch;
        for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
        ph.push(next, 'agent');
        // Sticky: replace the batch wholesale; no timer-driven clear.
        setLastAgentEditBatch(new Set(Object.keys(params)));
        bumpPatchLoad();
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
      sendMessage(JSON.stringify({ type: 'knob_tweak', param, value }));
    },
    [patch, ph, sendMessage],
  );

  // Wired into ChatInterface — "Use A" / "Use B" buttons hand the chosen
  // variation's preview data back here, where we treat it like a fresh agent batch.
  const handleSelectVariation = useCallback(
    (preview: PatchPreviewData) => {
      applyAgentBatch(previewToParamMap(preview), 'variation');
    },
    [applyAgentBatch],
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
      for (const [p, v] of Object.entries(diff)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [patch, ph, sendMessage, bumpPatchLoad],
  );

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
      for (const [p, v] of Object.entries(diff)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [activeSlot, inactiveSlotPatch, patch, ph, sendMessage, bumpPatchLoad],
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

  // ── Macros (Phase 6) ───────────────────────────────────────────────
  // Visual + rename only. Modulation routing lands with the mod matrix
  // in Phase 8 (macros project onto assigned param targets via depth +
  // polarity). Persist label changes so renames survive page reloads.
  const handleMacroChange = useCallback((index: number, value: number) => {
    setMacros((prev) => {
      const next = prev.slice();
      next[index] = { ...next[index], value };
      return next;
    });
    // TODO Phase 8: forward macro value to mod-matrix routing — for each
    // (param, depth, polarity) target assigned to this macro, emit a
    // knob_tweak frame applying the projection.
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

  // Propagate a rolled-back patch to the C++ engine: diff against the patch
  // we were just on and forward each changed param as a knob_tweak frame.
  const propagateRollback = useCallback(
    (rolled: PatchParams, previous: PatchParams) => {
      const diff = diffPatch(previous, rolled);
      for (const [p, v] of Object.entries(diff)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [sendMessage],
  );

  const handleUndo = useCallback(() => {
    const previous = patch;
    const rolled = ph.undo();
    if (rolled) propagateRollback(rolled, previous);
  }, [patch, ph, propagateRollback]);

  const handleRedo = useCallback(() => {
    const previous = patch;
    const rolled = ph.redo();
    if (rolled) propagateRollback(rolled, previous);
  }, [patch, ph, propagateRollback]);

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
      />

      <MacroBar
        macros={macros}
        onMacroChange={handleMacroChange}
        onMacroRename={handleMacroRename}
      />

      <div className="app-body">
        <PresetsSidebar
          currentPatch={patch}
          onLoadPreset={handleLoadPreset}
        />
        <ModulesGrid
          patch={patch}
          agentKeys={lastAgentEditBatch}
          onKnobChange={handleKnobChange}
          patchLoadToken={patchLoadToken}
        />
        <RightColumn
          externalTranscript={transcript}
          onAudio={handleAudio}
          onSelectVariation={handleSelectVariation}
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
      />
    </div>
  );
}
