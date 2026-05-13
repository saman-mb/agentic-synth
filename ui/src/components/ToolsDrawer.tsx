import React, { useCallback, useEffect } from 'react';
import { SemanticDictionary } from './SemanticDictionary';
import { TelemetryDashboard } from './TelemetryDashboard';
import { PatchBrowser, BrowserEntry } from './PatchBrowser';
import type { PatchParams } from './KnobGrid';
import './ToolsDrawer.css';

// ── ToolsDrawer (Phase 4) ────────────────────────────────────────────
//
// Secondary tools moved off the top-level layout into a side drawer
// opened from the TopBar gear icon. Internal tabs preserve the
// existing Dictionary / Telemetry / History components without
// rewriting them. Closing the drawer is wired to the parent.

type DrawerTab = 'dictionary' | 'telemetry' | 'history';

const TAB_ORDER: DrawerTab[] = ['dictionary', 'telemetry', 'history'];
const TAB_LABEL: Record<DrawerTab, string> = {
  dictionary: 'Dictionary',
  telemetry: 'Telemetry',
  history: 'History',
};

interface ToolsDrawerProps {
  open: boolean;
  onClose: () => void;
  activeTab: DrawerTab;
  onTabChange: (tab: DrawerTab) => void;

  // WebSocket bridge (Dictionary + Telemetry both subscribe).
  sendMessage: (msg: string) => void;
  lastMessage: string | null;

  // Patch browser
  browserEntries: BrowserEntry[];
  onBrowserSelect: (patch: PatchParams) => void;
  onBrowserStar: (id: string) => void;
  onBrowserRename: (id: string, label: string) => void;
  onBrowserClear: () => void;
}

export function ToolsDrawer({
  open,
  onClose,
  activeTab,
  onTabChange,
  sendMessage,
  lastMessage,
  browserEntries,
  onBrowserSelect,
  onBrowserStar,
  onBrowserRename,
  onBrowserClear,
}: ToolsDrawerProps) {
  // Close on Escape, matching standard drawer/modal affordances.
  useEffect(() => {
    if (!open) return;
    const handler = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        onClose();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [open, onClose]);

  const onTabKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLDivElement>) => {
      const idx = TAB_ORDER.indexOf(activeTab);
      let next: number | null = null;
      switch (e.key) {
        case 'ArrowRight': next = (idx + 1) % TAB_ORDER.length; break;
        case 'ArrowLeft':  next = (idx - 1 + TAB_ORDER.length) % TAB_ORDER.length; break;
        case 'Home':       next = 0; break;
        case 'End':        next = TAB_ORDER.length - 1; break;
        default: return;
      }
      if (next !== null) {
        e.preventDefault();
        onTabChange(TAB_ORDER[next]);
      }
    },
    [activeTab, onTabChange],
  );

  if (!open) return null;

  return (
    <div className="tools-drawer-overlay" role="dialog" aria-modal="true" aria-label="Tools">
      <button
        type="button"
        className="tools-drawer-backdrop"
        aria-label="Close tools"
        onClick={onClose}
      />
      <section className="tools-drawer">
        <header className="tools-drawer-header">
          <div
            className="tools-drawer-tabs"
            role="tablist"
            aria-label="Tools"
            onKeyDown={onTabKeyDown}
          >
            {TAB_ORDER.map((key) => {
              const selected = activeTab === key;
              return (
                <button
                  key={key}
                  id={`tools-tab-${key}`}
                  type="button"
                  role="tab"
                  aria-selected={selected}
                  aria-controls={`tools-panel-${key}`}
                  tabIndex={selected ? 0 : -1}
                  className={`tools-drawer-tab${selected ? ' tools-drawer-tab-active' : ''}`}
                  onClick={() => onTabChange(key)}
                >
                  {TAB_LABEL[key]}
                </button>
              );
            })}
          </div>
          <button
            type="button"
            className="tools-drawer-close"
            onClick={onClose}
            aria-label="Close tools"
            title="Close (Esc)"
          >
            ×
          </button>
        </header>

        <div className="tools-drawer-body">
          {activeTab === 'dictionary' && (
            <div id="tools-panel-dictionary" role="tabpanel" aria-labelledby="tools-tab-dictionary">
              <SemanticDictionary sendMessage={sendMessage} lastMessage={lastMessage} />
            </div>
          )}
          {activeTab === 'telemetry' && (
            <div id="tools-panel-telemetry" role="tabpanel" aria-labelledby="tools-tab-telemetry">
              <TelemetryDashboard sendMessage={sendMessage} lastMessage={lastMessage} />
            </div>
          )}
          {activeTab === 'history' && (
            <div id="tools-panel-history" role="tabpanel" aria-labelledby="tools-tab-history">
              <PatchBrowser
                entries={browserEntries}
                onSelect={onBrowserSelect}
                onStar={onBrowserStar}
                onRename={onBrowserRename}
                onClear={onBrowserClear}
              />
            </div>
          )}
        </div>
      </section>
    </div>
  );
}
