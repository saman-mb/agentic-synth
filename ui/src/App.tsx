import React, { useCallback, useEffect, useState } from 'react';
import './App.css';
import { ChatInterface } from './components/ChatInterface';
import { KnobGrid, makeDefaultPatch, PatchParams } from './components/KnobGrid';
import { SemanticDictionary } from './components/SemanticDictionary';
import { TelemetryDashboard } from './components/TelemetryDashboard';
import { useWebSocket } from './hooks/useWebSocket';
import type { PatchPreviewData } from './types/chat';

type LeftPanel = 'knobs' | 'dictionary' | 'telemetry';

const KNOB_BRIDGE_URL = 'ws://localhost:9002';

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
  const [patch, setPatch] = useState<PatchParams>(makeDefaultPatch);
  // Sticky set of params edited in the most recent agent generation.
  // Persists until the NEXT agent generation arrives (no transient flash).
  const [lastAgentEditBatch, setLastAgentEditBatch] = useState<Set<string>>(new Set());
  const [transcript, setTranscript] = useState('');
  const [leftPanel, setLeftPanel] = useState<LeftPanel>('knobs');

  const { lastMessage, sendMessage, sendBinary } = useWebSocket(KNOB_BRIDGE_URL);

  // Apply a batch of param edits as if the agent had just produced them.
  // Replaces lastAgentEditBatch so the sticky badge tracks only the latest generation.
  const applyAgentBatch = useCallback(
    (params: Record<string, number>) => {
      setPatch((prev) => {
        let next = prev;
        for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
        return next;
      });
      setLastAgentEditBatch(new Set(Object.keys(params)));
      // Forward to the audio bridge so the C++ engine sees the change too.
      for (const [p, v] of Object.entries(params)) {
        sendMessage(JSON.stringify({ type: 'knob_tweak', param: p, value: v }));
      }
    },
    [sendMessage],
  );

  useEffect(() => {
    if (!lastMessage) return;
    try {
      const msg = JSON.parse(lastMessage) as Record<string, unknown>;

      if (msg.type === 'patch_update' && msg.params && typeof msg.params === 'object') {
        const params = msg.params as Record<string, number>;
        setPatch((prev) => {
          let next = prev;
          for (const [p, v] of Object.entries(params)) next = applyParamToPatch(next, p, v);
          return next;
        });
        // Sticky: replace the batch wholesale; no timer-driven clear.
        setLastAgentEditBatch(new Set(Object.keys(params)));
      } else if (msg.type === 'transcript' && typeof msg.text === 'string') {
        setTranscript(msg.text as string);
      }
    } catch {
      // ignore malformed frames
    }
  }, [lastMessage]);

  const handleKnobChange = useCallback(
    (param: string, value: number) => {
      setPatch((prev) => applyParamToPatch(prev, param, value));
      sendMessage(JSON.stringify({ type: 'knob_tweak', param, value }));
    },
    [sendMessage],
  );

  // Wired into ChatInterface — "Use A" / "Use B" buttons hand the chosen
  // variation's preview data back here, where we treat it like a fresh agent batch.
  const handleSelectVariation = useCallback(
    (preview: PatchPreviewData) => {
      applyAgentBatch(previewToParamMap(preview));
    },
    [applyAgentBatch],
  );

  const handleAudio = useCallback(
    (buf: ArrayBuffer) => {
      sendBinary(buf);
    },
    [sendBinary],
  );

  const tabOrder: LeftPanel[] = ['knobs', 'dictionary', 'telemetry'];
  const tabLabel: Record<LeftPanel, string> = {
    knobs: 'Knobs',
    dictionary: 'Dictionary',
    telemetry: 'Telemetry',
  };

  const onTabKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    const idx = tabOrder.indexOf(leftPanel);
    let nextIdx: number | null = null;
    switch (e.key) {
      case 'ArrowRight':
        nextIdx = (idx + 1) % tabOrder.length;
        break;
      case 'ArrowLeft':
        nextIdx = (idx - 1 + tabOrder.length) % tabOrder.length;
        break;
      case 'Home':
        nextIdx = 0;
        break;
      case 'End':
        nextIdx = tabOrder.length - 1;
        break;
      default:
        return;
    }
    if (nextIdx !== null) {
      e.preventDefault();
      const nextTab = tabOrder[nextIdx];
      setLeftPanel(nextTab);
      const el = document.getElementById(`panel-tab-${nextTab}`);
      el?.focus();
    }
  };

  return (
    <div className="app-layout">
      <aside className="panel-knobs">
        <div
          className="panel-tabs"
          role="tablist"
          aria-label="Left panel"
          onKeyDown={onTabKeyDown}
        >
          {tabOrder.map((key) => {
            const selected = leftPanel === key;
            return (
              <button
                key={key}
                id={`panel-tab-${key}`}
                role="tab"
                aria-selected={selected}
                aria-controls={`panel-tabpanel-${key}`}
                tabIndex={selected ? 0 : -1}
                className={`panel-tab${selected ? ' panel-tab-active' : ''}`}
                onClick={() => setLeftPanel(key)}
              >
                {tabLabel[key]}
              </button>
            );
          })}
        </div>
        {leftPanel === 'knobs' && (
          <div
            id="panel-tabpanel-knobs"
            role="tabpanel"
            aria-labelledby="panel-tab-knobs"
            tabIndex={0}
          >
            <KnobGrid patch={patch} agentKeys={lastAgentEditBatch} onKnobChange={handleKnobChange} />
          </div>
        )}
        {leftPanel === 'dictionary' && (
          <div
            id="panel-tabpanel-dictionary"
            role="tabpanel"
            aria-labelledby="panel-tab-dictionary"
            tabIndex={0}
          >
            <SemanticDictionary sendMessage={sendMessage} lastMessage={lastMessage} />
          </div>
        )}
        {leftPanel === 'telemetry' && (
          <div
            id="panel-tabpanel-telemetry"
            role="tabpanel"
            aria-labelledby="panel-tab-telemetry"
            tabIndex={0}
          >
            <TelemetryDashboard sendMessage={sendMessage} lastMessage={lastMessage} />
          </div>
        )}
      </aside>
      <main className="panel-chat">
        <ChatInterface
          externalTranscript={transcript}
          onAudio={handleAudio}
          onSelectVariation={handleSelectVariation}
        />
      </main>
    </div>
  );
}
