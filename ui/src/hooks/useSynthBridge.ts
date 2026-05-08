import { useCallback, useEffect, useRef, useState } from 'react';
import type { WireIncoming, WireOutgoing } from '../types/chat';

export type BridgeStatus = 'connecting' | 'open' | 'closed' | 'error';

const WS_URL = 'ws://localhost:8765';
const RECONNECT_DELAY_MS = 3000;

interface UseSynthBridgeReturn {
  status: BridgeStatus;
  send: (msg: WireOutgoing) => void;
  lastMessage: WireIncoming | null;
}

export function useSynthBridge(): UseSynthBridgeReturn {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [status, setStatus] = useState<BridgeStatus>('connecting');
  const [lastMessage, setLastMessage] = useState<WireIncoming | null>(null);

  const connect = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return;

    setStatus('connecting');
    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => setStatus('open');

    ws.onmessage = (ev: MessageEvent) => {
      try {
        const parsed = JSON.parse(ev.data as string) as WireIncoming;
        setLastMessage(parsed);
      } catch {
        // ignore malformed frames
      }
    };

    ws.onclose = () => {
      setStatus('closed');
      reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
    };

    ws.onerror = () => {
      setStatus('error');
      ws.close();
    };
  }, []);

  useEffect(() => {
    connect();
    return () => {
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connect]);

  const send = useCallback((msg: WireOutgoing) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg));
    }
  }, []);

  return { status, send, lastMessage };
}
