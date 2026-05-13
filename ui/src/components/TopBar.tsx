import React, { useCallback, useState } from 'react';
import './TopBar.css';

// ── TopBar (Phase 6) ─────────────────────────────────────────────────
//
// Fixed 48px chrome strip. Hosts: wordmark, preset selector (still
// placeholder — the live preset browser lives in PresetsSidebar), A/B
// compare (Phase 6, wired), inline undo/redo, CPU/MIDI/OUT meters
// (Phase 5 placeholders), and the gear icon that opens ToolsDrawer.

type Theme = 'dark' | 'light';
export type ABSlot = 'A' | 'B';

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
  activeSlot: ABSlot;
  onSelectSlot: (slot: ABSlot) => void;
  onCopySlot: () => void;
  // Phase 10 §16 — Option+double-click on the wordmark triggers the
  // one-time synchronized 360° knob spin easter egg. Once per session.
  onAltDoubleClickLogo?: () => void;
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
  activeSlot,
  onSelectSlot,
  onCopySlot,
  onAltDoubleClickLogo,
}: TopBarProps) {
  // Cyan confirmation sweep token for the A/B copy button. Bumped on
  // every copy so consecutive copies retrigger the animation. Cleared
  // after the 1000ms sweep+hold+fade finishes.
  const [copySweepToken, setCopySweepToken] = useState<number>(0);
  const handleCopyClick = useCallback(() => {
    onCopySlot();
    setCopySweepToken((n) => n + 1);
    window.setTimeout(() => setCopySweepToken(0), 1000);
  }, [onCopySlot]);
  return (
    <header className="topbar" role="banner">
      {/* Left: wordmark — Option+double-click triggers knob-spin easter egg */}
      <div className="topbar-section topbar-left">
        <span
          className="topbar-wordmark"
          aria-label="TIMBRE"
          onDoubleClick={(e) => {
            if (e.altKey && onAltDoubleClickLogo) onAltDoubleClickLogo();
          }}
        >
          TIMBRE
        </span>
      </div>

      {/* Center-left: preset selector (placeholder — live browser is in sidebar) */}
      <div className="topbar-section topbar-preset" aria-label="Preset selector">
        <button type="button" className="topbar-preset-arrow" aria-label="Previous preset" disabled>‹</button>
        <button type="button" className="topbar-preset-dropdown" aria-haspopup="listbox" disabled>
          <span className="topbar-preset-name">init patch</span>
          <span className="topbar-preset-chev" aria-hidden="true">▾</span>
        </button>
        <button type="button" className="topbar-preset-arrow" aria-label="Next preset" disabled>›</button>
      </div>

      {/* Center: A/B compare (wired — Phase 6) */}
      <div className="topbar-section topbar-ab" role="group" aria-label="A/B compare">
        <button
          type="button"
          className={`topbar-ab-seg${activeSlot === 'A' ? ' topbar-ab-seg-active' : ''}`}
          aria-pressed={activeSlot === 'A'}
          aria-label="Select slot A"
          onClick={() => onSelectSlot('A')}
          title="Slot A (Space to toggle)"
        >
          A
        </button>
        <button
          type="button"
          key={`copy-${copySweepToken}`}
          className={`topbar-ab-copy${copySweepToken > 0 ? ' confirm-sweep' : ''}`}
          aria-label={`Copy slot ${activeSlot} to ${activeSlot === 'A' ? 'B' : 'A'}`}
          title={`Copy ${activeSlot} → ${activeSlot === 'A' ? 'B' : 'A'}`}
          onClick={handleCopyClick}
        >
          {activeSlot === 'A' ? '→' : '←'}
        </button>
        <button
          type="button"
          className={`topbar-ab-seg${activeSlot === 'B' ? ' topbar-ab-seg-active' : ''}`}
          aria-pressed={activeSlot === 'B'}
          aria-label="Select slot B"
          onClick={() => onSelectSlot('B')}
          title="Slot B (Space to toggle)"
        >
          B
        </button>
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
