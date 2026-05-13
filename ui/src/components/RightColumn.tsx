import React, { useState } from 'react';
import { ChatInterface } from './ChatInterface';
import type { PatchPreviewData } from '../types/chat';
import './RightColumn.css';

// ── RightColumn (Phase 4) ────────────────────────────────────────────
//
// Top: visualiser placeholder (Phase 5 owns canvas oscilloscope/spec).
// Middle: AI prompt dock — wraps the existing ChatInterface in a
//         fixed-height card so the always-visible promise from
//         REBRAND.md §5 holds even when modules scroll.
// Bottom: Mod Matrix placeholder, collapsible (Phase 8 owns the
//         constellation/grid).

interface RightColumnProps {
  externalTranscript: string;
  onAudio: (buf: ArrayBuffer) => void;
  onSelectVariation: (preview: PatchPreviewData) => void;
}

export function RightColumn({
  externalTranscript,
  onAudio,
  onSelectVariation,
}: RightColumnProps) {
  // Mod matrix is collapsed by default — it's a Phase 8 placeholder so
  // we don't burn vertical space on a header users can't yet act on.
  const [modOpen, setModOpen] = useState(false);

  return (
    <aside className="right-column" aria-label="Visualiser, AI prompt, and modulation">
      {/* ── Visualiser placeholder (Phase 5) ─────────────────────── */}
      <section className="rc-card rc-visualizer" aria-label="Visualiser placeholder">
        <header className="rc-card-header">
          <span className="rc-card-title">Visualiser</span>
          <span className="rc-card-tag">Phase 5</span>
        </header>
        <div className="rc-visualizer-well" aria-hidden="true">
          <span className="rc-visualizer-hint">oscilloscope · spectrum · XY · wt</span>
        </div>
      </section>

      {/* ── AI prompt dock ──────────────────────────────────────── */}
      <section className="rc-card rc-prompt-dock" aria-label="AI prompt">
        <ChatInterface
          externalTranscript={externalTranscript}
          onAudio={onAudio}
          onSelectVariation={onSelectVariation}
        />
      </section>

      {/* ── Mod matrix placeholder (Phase 8) ─────────────────────── */}
      <section className={`rc-card rc-modmatrix${modOpen ? ' rc-modmatrix-open' : ''}`}>
        <button
          type="button"
          className="rc-card-header rc-modmatrix-header"
          aria-expanded={modOpen}
          aria-controls="rc-modmatrix-body"
          onClick={() => setModOpen((v) => !v)}
        >
          <span className="rc-card-chev" aria-hidden="true">{modOpen ? '▾' : '▸'}</span>
          <span className="rc-card-title">Modulation Matrix</span>
          <span className="rc-card-tag">Phase 8</span>
        </button>
        {modOpen && (
          <div id="rc-modmatrix-body" className="rc-modmatrix-body">
            <span className="rc-modmatrix-hint">
              src → dst   amt   curve   [+]
            </span>
          </div>
        )}
      </section>
    </aside>
  );
}
