import { useCallback, useEffect, useRef, useState } from 'react';
import './App.css';
import { ChatInterface } from './components/ChatInterface';
import { KnobGrid, makeDefaultPatch, PatchParams } from './components/KnobGrid';
import { useWebSocket } from './hooks/useWebSocket';

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

export function App() {
  const [patch, setPatch] = useState<PatchParams>(makeDefaultPatch);
  const [agentKeys, setAgentKeys] = useState<Set<string>>(new Set());
  const [transcript, setTranscript] = useState('');
  const agentKeyTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const { lastMessage, sendMessage, sendBinary } = useWebSocket(KNOB_BRIDGE_URL);

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
        setAgentKeys(new Set(Object.keys(params)));
        if (agentKeyTimer.current) clearTimeout(agentKeyTimer.current);
        agentKeyTimer.current = setTimeout(() => setAgentKeys(new Set()), 500);
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

  const handleAudio = useCallback(
    (buf: ArrayBuffer) => {
      sendBinary(buf);
    },
    [sendBinary],
  );

  return (
    <div className="app-layout">
      <aside className="panel-knobs">
        <KnobGrid patch={patch} agentKeys={agentKeys} onKnobChange={handleKnobChange} />
      </aside>
      <main className="panel-chat">
        <ChatInterface externalTranscript={transcript} onAudio={handleAudio} />
      </main>
    </div>
  );
}
