import React from 'react';
import { ChatInterface } from './ChatInterface';
import type { AgentModulationPlan, PatchPreviewData } from '../types/chat';
import './RightColumn.css';

// ── RightColumn — chat-only ──────────────────────────────────────────
// Visualiser + Mod Matrix moved to the centre column (LeftPanel) so the
// chat dock can fill the whole right side without competing for height.

interface RightColumnProps {
  externalTranscript: string;
  onAudio: (buf: ArrayBuffer) => void;
  onSelectVariation: (preview: PatchPreviewData, modulation?: AgentModulationPlan) => void;
  onRtfmEasterEgg?: () => void;
}

export function RightColumn({
  externalTranscript,
  onAudio,
  onSelectVariation,
  onRtfmEasterEgg,
}: RightColumnProps) {
  return (
    <aside className="right-column right-column-chat-only" aria-label="AI prompt">
      <section className="rc-card rc-prompt-dock rc-prompt-dock-full" aria-label="AI prompt">
        <ChatInterface
          externalTranscript={externalTranscript}
          onAudio={onAudio}
          onSelectVariation={onSelectVariation}
          onRtfmEasterEgg={onRtfmEasterEgg}
        />
      </section>
    </aside>
  );
}
