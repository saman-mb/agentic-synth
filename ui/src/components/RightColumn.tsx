import React from 'react';
import { ChatInterface } from './ChatInterface';
import { Visualizer } from './Visualizer';
import { ModMatrixPanel } from './ModMatrixPanel';
import type { PatchPreviewData } from '../types/chat';
import type { ModConnection, ModMatrix, ModSourceId } from '../data/modulation';
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
  modMatrix: ModMatrix;
  onUpdateConnection: (id: string, patch: Partial<ModConnection>) => void;
  onDeleteConnection: (id: string) => void;
  onAddConnection: (source: ModSourceId, destination: string) => void;
}

export function RightColumn({
  externalTranscript,
  onAudio,
  onSelectVariation,
  modMatrix,
  onUpdateConnection,
  onDeleteConnection,
  onAddConnection,
}: RightColumnProps) {
  return (
    <aside className="right-column" aria-label="Visualiser, AI prompt, and modulation">
      {/* ── Visualiser (Phase 5) ─────────────────────────────────── */}
      <section className="rc-card rc-visualizer" aria-label="Visualiser">
        <Visualizer />
      </section>

      {/* ── AI prompt dock ──────────────────────────────────────── */}
      <section className="rc-card rc-prompt-dock" aria-label="AI prompt">
        <ChatInterface
          externalTranscript={externalTranscript}
          onAudio={onAudio}
          onSelectVariation={onSelectVariation}
        />
      </section>

      {/* ── Mod matrix (Phase 8 — list + constellation views) ───── */}
      <ModMatrixPanel
        modMatrix={modMatrix}
        onUpdateConnection={onUpdateConnection}
        onDeleteConnection={onDeleteConnection}
        onAddConnection={onAddConnection}
      />
    </aside>
  );
}
