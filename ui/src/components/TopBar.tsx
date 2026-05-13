import React from 'react';
import './TopBar.css';

// ── TopBar (Phase 4) ─────────────────────────────────────────────────
//
// Fixed-height (48px) chrome strip at the top of the TIMBRE layout.
// Hosts: wordmark, preset selector (Phase 6 placeholder), A/B compare
// (Phase 6 placeholder), inline undo/redo (wired to usePatchHistory in
// App.tsx), CPU/MIDI/OUT meters (Phase 5 placeholders), and the gear
// icon that opens ToolsDrawer (Dictionary / Telemetry / History).
//
// Only Undo/Redo + Theme + gear are functional today; everything else
// is presentational scaffolding the later phases will hydrate.

type Theme = 'dark' | 'light';

interface TopBarProps {
  onUndo: () => void;
  onRedo: () => void;
  canUndo: boolean;
  canRedo: boolean;
  cursor: number;
  historyLength: number;
  theme: Theme;
  onToggleTheme: () => void;
  onOpenTools: () => void;
}

export function TopBar({
  onUndo,
  onRedo,
  canUndo,
  canRedo,
  cursor,
  historyLength,
  theme,
  onToggleTheme,
  onOpenTools,
}: TopBarProps) {
  return (
    <header className="topbar" role="banner">
      {/* Left: wordmark */}
      <div className="topbar-section topbar-left">
        <span className="topbar-wordmark" aria-label="TIMBRE">TIMBRE</span>
      </div>

      {/* Center-left: preset selector (placeholder — Phase 6) */}
      <div className="topbar-section topbar-preset" aria-label="Preset selector">
        <button type="button" className="topbar-preset-arrow" aria-label="Previous preset" disabled>‹</button>
        <button type="button" className="topbar-preset-dropdown" aria-haspopup="listbox" disabled>
          <span className="topbar-preset-name">init patch</span>
          <span className="topbar-preset-chev" aria-hidden="true">▾</span>
        </button>
        <button type="button" className="topbar-preset-arrow" aria-label="Next preset" disabled>›</button>
      </div>

      {/* Center: A/B compare (placeholder — Phase 6) */}
      <div className="topbar-section topbar-ab" role="group" aria-label="A/B compare">
        <button type="button" className="topbar-ab-seg topbar-ab-seg-active" aria-pressed="true" disabled>A</button>
        <button type="button" className="topbar-ab-copy" aria-label="Copy A to B" title="Copy A → B" disabled>→</button>
        <button type="button" className="topbar-ab-seg" aria-pressed="false" disabled>B</button>
      </div>

      {/* Center-right: undo/redo (functional) */}
      <div className="topbar-section topbar-history" role="group" aria-label="History">
        <button
          type="button"
          className="topbar-icon-btn"
          onClick={onUndo}
          disabled={!canUndo}
          aria-label="Undo last patch change"
          title="Undo (Cmd/Ctrl+Z)"
        >
          ↶
        </button>
        <button
          type="button"
          className="topbar-icon-btn"
          onClick={onRedo}
          disabled={!canRedo}
          aria-label="Redo"
          title="Redo (Cmd/Ctrl+Shift+Z)"
        >
          ↷
        </button>
        <span className="topbar-history-count" aria-live="polite">
          {cursor + 1}/{historyLength}
        </span>
      </div>

      {/* Right: meters (placeholders — Phase 5) */}
      <div className="topbar-section topbar-meters" aria-label="Performance">
        <span className="topbar-meter">
          <span className="topbar-meter-label">CPU</span>
          <span className="topbar-meter-value">4%</span>
        </span>
        <span className="topbar-meter">
          <span className="topbar-meter-label">MIDI</span>
          <span className="topbar-led" aria-hidden="true" />
        </span>
        <span className="topbar-meter topbar-meter-out">
          <span className="topbar-meter-label">OUT</span>
          <span className="topbar-meter-bar" aria-hidden="true">
            <span className="topbar-meter-bar-fill" />
          </span>
        </span>
      </div>

      {/* Far right: theme + gear */}
      <div className="topbar-section topbar-tools">
        <button
          type="button"
          className="topbar-icon-btn"
          onClick={onToggleTheme}
          aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
          title={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
        >
          {theme === 'dark' ? '☀' : '☾'}
        </button>
        <button
          type="button"
          className="topbar-icon-btn"
          onClick={onOpenTools}
          aria-label="Open tools (dictionary, telemetry, history)"
          title="Tools"
        >
          ⚙
        </button>
      </div>
    </header>
  );
}
