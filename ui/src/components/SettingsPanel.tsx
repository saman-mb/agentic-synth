import React, { useEffect, useMemo, useState } from 'react';
import './SettingsPanel.css';
import { playTapeStop, playVoicePip } from '../data/uiAudio';

// ── SettingsPanel (Phase 10 §17 + Phase 14 expansion) ───────────────
//
// Lives inside ToolsDrawer as its own tab. UI surface for user
// preferences: confirmation sounds, theme, motion, MIDI input.
//
// Side-effects (root attrs, localStorage) live inside this component
// so wiring stays local; App.tsx need not know about theme/motion/MIDI.

// ── Persistence keys ───────────────────────────────────────────────
const THEME_KEY = 'timbre:theme';            // 'auto' | 'dark' | 'light'
const MOTION_KEY = 'timbre:motion';          // 'full' | 'reduced' | 'off'
const MIDI_INPUT_KEY = 'timbre:midi-input';  // string id; '' = Any

type ThemeMode = 'auto' | 'dark' | 'light';
type MotionMode = 'full' | 'reduced' | 'off';

function readString<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  if (typeof window === 'undefined') return fallback;
  try {
    const v = window.localStorage.getItem(key);
    return (allowed as readonly string[]).includes(v ?? '') ? (v as T) : fallback;
  } catch {
    return fallback;
  }
}

function writeString(key: string, value: string): void {
  if (typeof window === 'undefined') return;
  try {
    window.localStorage.setItem(key, value);
  } catch {
    // private-mode browsers may throw — ignore.
  }
}

// ── Root-attribute appliers ────────────────────────────────────────
//
// Theme:  data-theme="dark"|"light" or no attr (auto = CSS media query)
// Motion: data-motion="off" only when fully disabled. 'reduced' is
//         already covered by the existing CSS @media
//         (prefers-reduced-motion) rules; 'full' is the default.
function applyTheme(mode: ThemeMode): void {
  const root = document.documentElement;
  if (mode === 'auto') {
    // Defer to OS preference via CSS. App.tsx also sets data-theme on
    // its own state changes; we tolerate that — the user can re-pick.
    root.removeAttribute('data-theme');
  } else {
    root.setAttribute('data-theme', mode);
  }
}

function applyMotion(mode: MotionMode): void {
  const root = document.documentElement;
  if (mode === 'off') {
    root.setAttribute('data-motion', 'off');
  } else {
    // 'full' and 'reduced' both leave the attr off — 'reduced' is
    // honored by the existing prefers-reduced-motion CSS rules.
    root.removeAttribute('data-motion');
  }
}

// ── MIDI input enumeration ─────────────────────────────────────────
interface MidiPortInfo {
  id: string;
  name: string;
}

interface MidiState {
  supported: boolean;
  inputs: MidiPortInfo[];
}

function useMidiInputs(): MidiState {
  const [state, setState] = useState<MidiState>({
    supported: typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator,
    inputs: [],
  });

  useEffect(() => {
    if (!state.supported) return;
    let cancelled = false;
    // requestMIDIAccess returns a Promise<MIDIAccess>; we don't bring in
    // @types/webmidi here, so use a permissive cast.
    const req = (navigator as unknown as {
      requestMIDIAccess: () => Promise<{
        inputs: Map<string, { id: string; name: string | null }>;
        addEventListener?: (type: string, cb: () => void) => void;
        onstatechange?: (() => void) | null;
      }>;
    }).requestMIDIAccess();
    req.then((access) => {
      if (cancelled) return;
      const list: MidiPortInfo[] = [];
      access.inputs.forEach((port) => {
        list.push({ id: port.id, name: port.name ?? port.id });
      });
      setState({ supported: true, inputs: list });
      // Re-enumerate on hot-plug. addEventListener is the modern API.
      const refresh = () => {
        const updated: MidiPortInfo[] = [];
        access.inputs.forEach((port) => {
          updated.push({ id: port.id, name: port.name ?? port.id });
        });
        setState({ supported: true, inputs: updated });
      };
      if (typeof access.addEventListener === 'function') {
        access.addEventListener('statechange', refresh);
      } else {
        access.onstatechange = refresh;
      }
    }).catch(() => {
      // User denied permission, or runtime threw — degrade gracefully.
      if (!cancelled) setState({ supported: false, inputs: [] });
    });
    return () => {
      cancelled = true;
    };
  }, [state.supported]);

  return state;
}

export interface SettingsPanelProps {
  voicePip: boolean;
  patchThunk: boolean;
  onVoicePipChange: (v: boolean) => void;
  onPatchThunkChange: (v: boolean) => void;
}

export function SettingsPanel({
  voicePip,
  patchThunk,
  onVoicePipChange,
  onPatchThunkChange,
}: SettingsPanelProps) {
  // ── Theme override ────────────────────────────────────────────
  const [theme, setTheme] = useState<ThemeMode>(() =>
    readString<ThemeMode>(THEME_KEY, ['auto', 'dark', 'light'], 'auto'),
  );
  useEffect(() => {
    writeString(THEME_KEY, theme);
    applyTheme(theme);
  }, [theme]);

  // ── Motion override ───────────────────────────────────────────
  const [motion, setMotion] = useState<MotionMode>(() =>
    readString<MotionMode>(MOTION_KEY, ['full', 'reduced', 'off'], 'full'),
  );
  useEffect(() => {
    writeString(MOTION_KEY, motion);
    applyMotion(motion);
  }, [motion]);

  // ── MIDI input selection ──────────────────────────────────────
  const midi = useMidiInputs();
  const [midiInputId, setMidiInputId] = useState<string>(() => {
    if (typeof window === 'undefined') return '';
    try {
      return window.localStorage.getItem(MIDI_INPUT_KEY) ?? '';
    } catch {
      return '';
    }
  });
  useEffect(() => {
    writeString(MIDI_INPUT_KEY, midiInputId);
    // TODO(phase-15+): wire midiInputId into the actual MIDI router so
    // only the chosen port forwards note/CC traffic. Today the bridge
    // accepts events from any port (legacy "Any" behavior).
  }, [midiInputId]);

  const midiOptions = useMemo(() => {
    return [{ id: '', name: 'Any' }, ...midi.inputs];
  }, [midi.inputs]);

  return (
    <div className="settings-panel" role="region" aria-label="Settings">
      <header className="settings-panel-header">
        <h2 className="settings-panel-title">Settings</h2>
        <p className="settings-panel-subtitle">
          TIMBRE is mostly silent. Opt in to confirmation sounds below.
        </p>
      </header>

      <section className="settings-group" aria-label="UI sound feedback">
        <h3 className="settings-group-title">UI sound feedback</h3>

        <label className="settings-row">
          <input
            type="checkbox"
            checked={voicePip}
            onChange={(e) => onVoicePipChange(e.target.checked)}
            aria-describedby="settings-voice-desc"
          />
          <div className="settings-row-text">
            <span className="settings-row-label">Voice transcription</span>
            <span id="settings-voice-desc" className="settings-row-desc">
              40ms sine pip at A4 when a voice prompt finishes transcribing.
            </span>
          </div>
          <button
            type="button"
            className="settings-row-preview"
            onClick={playVoicePip}
            aria-label="Preview voice transcription pip"
            title="Preview"
          >
            Preview
          </button>
        </label>

        <label className="settings-row">
          <input
            type="checkbox"
            checked={patchThunk}
            onChange={(e) => onPatchThunkChange(e.target.checked)}
            aria-describedby="settings-patch-desc"
          />
          <div className="settings-row-text">
            <span className="settings-row-label">Patch load</span>
            <span id="settings-patch-desc" className="settings-row-desc">
              Soft tape-stop thunk when a patch lands (preset, agent, or A/B).
            </span>
          </div>
          <button
            type="button"
            className="settings-row-preview"
            onClick={playTapeStop}
            aria-label="Preview patch load thunk"
            title="Preview"
          >
            Preview
          </button>
        </label>
      </section>

      {/* ── Theme override ──────────────────────────────────────── */}
      <section className="settings-group" aria-label="Theme">
        <h3 className="settings-group-title">Theme</h3>
        <div className="settings-row" role="radiogroup" aria-label="Theme mode">
          <div className="settings-row-text">
            <span className="settings-row-label">Appearance</span>
            <span className="settings-row-desc">
              Auto follows your system preference. Dark or Light overrides it.
            </span>
            <div className="settings-radio-row">
              {(['auto', 'dark', 'light'] as const).map((mode) => (
                <label key={mode} className="settings-radio">
                  <input
                    type="radio"
                    name="settings-theme"
                    value={mode}
                    checked={theme === mode}
                    onChange={() => setTheme(mode)}
                  />
                  <span className="settings-radio-label">
                    {mode === 'auto' ? 'Auto' : mode === 'dark' ? 'Dark' : 'Light'}
                  </span>
                </label>
              ))}
            </div>
          </div>
        </div>
      </section>

      {/* ── Motion override ─────────────────────────────────────── */}
      <section className="settings-group" aria-label="Motion">
        <h3 className="settings-group-title">Motion</h3>
        <div className="settings-row" role="radiogroup" aria-label="Motion mode">
          <div className="settings-row-text">
            <span className="settings-row-label">Animations</span>
            <span className="settings-row-desc">
              Full keeps all motion. Reduced honors your system's reduced-motion
              setting. Off disables every animation and transition.
            </span>
            <div className="settings-radio-row">
              {(['full', 'reduced', 'off'] as const).map((mode) => (
                <label key={mode} className="settings-radio">
                  <input
                    type="radio"
                    name="settings-motion"
                    value={mode}
                    checked={motion === mode}
                    onChange={() => setMotion(mode)}
                  />
                  <span className="settings-radio-label">
                    {mode === 'full' ? 'Full' : mode === 'reduced' ? 'Reduced' : 'Off'}
                  </span>
                </label>
              ))}
            </div>
          </div>
        </div>
      </section>

      {/* ── MIDI input selector ─────────────────────────────────── */}
      <section className="settings-group" aria-label="MIDI Input">
        <h3 className="settings-group-title">MIDI Input</h3>
        <div className="settings-row">
          <div className="settings-row-text">
            <span className="settings-row-label">Input port</span>
            {midi.supported ? (
              <>
                <span className="settings-row-desc">
                  Choose which MIDI device feeds note &amp; CC into TIMBRE.
                  "Any" accepts events from every connected port.
                </span>
                <select
                  className="settings-select"
                  value={midiInputId}
                  onChange={(e) => setMidiInputId(e.target.value)}
                  aria-label="MIDI input device"
                >
                  {midiOptions.map((opt) => (
                    <option key={opt.id || 'any'} value={opt.id}>
                      {opt.name}
                    </option>
                  ))}
                </select>
              </>
            ) : (
              <span className="settings-row-disabled">
                MIDI input selection unavailable
              </span>
            )}
          </div>
        </div>
      </section>
    </div>
  );
}
