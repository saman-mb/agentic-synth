import { useCallback, useEffect, useRef, useState } from 'react';

export type WSReadyState = 0 | 1 | 2 | 3;

export interface UseWebSocketReturn {
  lastMessage: string | null;
  sendMessage: (msg: string) => void;
  sendBinary: (data: ArrayBuffer) => void;
  readyState: WSReadyState;
}

export function useWebSocket(url: string): UseWebSocketReturn {
  const ws = useRef<WebSocket | null>(null);
  const [readyState, setReadyState] = useState<WSReadyState>(WebSocket.CONNECTING);
  const [lastMessage, setLastMessage] = useState<string | null>(null);

  useEffect(() => {
    const socket = new WebSocket(url);
    ws.current = socket;

    socket.onopen = () => setReadyState(WebSocket.OPEN as WSReadyState);
    socket.onclose = () => setReadyState(WebSocket.CLOSED as WSReadyState);
    socket.onerror = () => setReadyState(WebSocket.CLOSED as WSReadyState);
    socket.onmessage = (e: MessageEvent) => {
      if (typeof e.data === 'string') setLastMessage(e.data);
    };

    return () => {
      socket.close();
    };
  }, [url]);

  const sendMessage = useCallback((msg: string) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(msg);
    }
  }, []);

  const sendBinary = useCallback((data: ArrayBuffer) => {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(data);
    }
  }, []);

  return { lastMessage, sendMessage, sendBinary, readyState };
}
