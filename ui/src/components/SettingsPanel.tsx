import React from 'react';
import './SettingsPanel.css';
import { playTapeStop, playVoicePip } from '../data/uiAudio';

// ── SettingsPanel (Phase 10 §17) ────────────────────────────────────
//
// Lives inside ToolsDrawer as its own tab. Today: just the two opt-in
// UI sound toggles. Designed to grow as more user preferences land.

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
    </div>
  );
}
