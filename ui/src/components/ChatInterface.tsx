import {
  FormEvent,
  KeyboardEvent,
  useCallback,
  useEffect,
  useId,
  useRef,
  useState,
} from 'react';

import { useSynthBridge } from '../hooks/useSynthBridge';
import type {
  ChatMessage,
  FeedbackKind,
  PatchPreviewData,
  PatchVariation,
  ProactiveSuggestion,
  WireIncoming,
  WireOutgoing,
} from '../types/chat';
import { AuditionKeyboard } from './AuditionKeyboard';
import { PatchPreview } from './PatchPreview';
import { PushToTalk } from './PushToTalk';
import './ChatInterface.css';

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

function nanoid(): string {
  return Math.random().toString(36).slice(2, 11);
}

// ---------------------------------------------------------------------------
// A/B Variation Grid
// ---------------------------------------------------------------------------

interface ABGridProps {
  variations: PatchVariation[];
  onSelectVariation?: (patch: PatchPreviewData) => void;
}

function ABVariationGrid({ variations, onSelectVariation }: ABGridProps) {
  return (
    <div className="ab-grid" role="region" aria-label="A/B patch variations">
      {variations.map((v) => (
        <div key={v.label} className="ab-variation">
          <PatchPreview patch={v.patch} label={`Variation ${v.label}`} />
          {onSelectVariation && (
            <button
              type="button"
              className="ab-commit-btn"
              onClick={() => onSelectVariation(v.patch)}
              aria-label={`Use variation ${v.label}`}
            >
              Use {v.label}
            </button>
          )}
        </div>
      ))}
    </div>
  );
}

// ---------------------------------------------------------------------------
// Feedback bar
// ---------------------------------------------------------------------------

interface FeedbackBarProps {
  messageId: string;
  feedback?: FeedbackKind;
  onFeedback: (messageId: string, kind: FeedbackKind) => void;
}

function FeedbackBar({ messageId, feedback, onFeedback }: FeedbackBarProps) {
  return (
    <div className="feedback-bar" role="group" aria-label="Rate this patch">
      <button
        className={`feedback-btn like ${feedback === 'like' ? 'active' : ''}`}
        aria-pressed={feedback === 'like'}
        aria-label="Like this patch"
        disabled={!!feedback}
        onClick={() => onFeedback(messageId, 'like')}
      >
        ♥ Like
      </button>
      <button
        className={`feedback-btn dislike ${feedback === 'dislike' ? 'active' : ''}`}
        aria-pressed={feedback === 'dislike'}
        aria-label="Dislike this patch"
        disabled={!!feedback}
        onClick={() => onFeedback(messageId, 'dislike')}
      >
        ✕ Pass
      </button>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Message bubble
// ---------------------------------------------------------------------------

interface BubbleProps {
  message: ChatMessage;
  onFeedback: (messageId: string, kind: FeedbackKind) => void;
  onSelectVariation?: (patch: PatchPreviewData) => void;
  // When provided, an AuditionKeyboard renders next to single-patch
  // previews so the producer can audition the assistant's patch without
  // a DAW attached. Wired only to the single-patch path on purpose:
  // the A/B grid is Engineer B's territory and adds its own audition UI.
  auditionSend?: (frame: WireOutgoing) => void;
  auditionReady?: boolean;
  // When true, the bubble text shows the cyan reading-sweep underline.
  submittedFlash?: boolean;
}

function MessageBubble({
  message,
  onFeedback,
  onSelectVariation,
  auditionSend,
  auditionReady,
  submittedFlash,
}: BubbleProps) {
  const hasContent = message.role === 'assistant';
  const bubbleTextClass = `bubble-text${submittedFlash ? ' prompt-submitted' : ''}`;
  return (
    <article
      className={`message-bubble ${message.role}`}
      aria-label={`${message.role} message`}
    >
      <div className="bubble-role">{message.role === 'user' ? 'You' : 'Agent'}</div>
      <p className={bubbleTextClass}>
        {message.content}
        {message.streaming && <span className="cursor" aria-hidden="true" />}
      </p>

      {message.rationale && !message.streaming && (
        <details className="rationale-details">
          <summary className="rationale-summary">Why this patch?</summary>
          <p className="rationale-text">{message.rationale}</p>
        </details>
      )}

      {message.variations && message.variations.length > 0 && (
        <ABVariationGrid variations={message.variations} onSelectVariation={onSelectVariation} />
      )}

      {!message.variations && message.patch && (
        <>
          <PatchPreview patch={message.patch} label="Generated patch" />
          {auditionSend && (
            <AuditionKeyboard
              sendRaw={(json) => {
                try {
                  auditionSend(JSON.parse(json) as WireOutgoing);
                } catch {
                  // malformed payload — drop silently
                }
              }}
              ready={!!auditionReady}
            />
          )}
        </>
      )}

      {hasContent && !message.streaming && (
        <FeedbackBar
          messageId={message.id}
          feedback={message.feedback}
          onFeedback={onFeedback}
        />
      )}
    </article>
  );
}

// ---------------------------------------------------------------------------
// Proactive variation suggestion strip
// ---------------------------------------------------------------------------

interface ProactiveSuggestionsProps {
  suggestions: ProactiveSuggestion[];
  onDismiss: () => void;
}

function ProactiveSuggestionStrip({ suggestions, onDismiss }: ProactiveSuggestionsProps) {
  return (
    <div className="proactive-suggestions" role="region" aria-label="Suggested variations">
      <div className="proactive-header">
        <span className="proactive-title">Suggested variations</span>
        <button className="proactive-dismiss" onClick={onDismiss} aria-label="Dismiss suggestions">✕</button>
      </div>
      <div className="proactive-list">
        {suggestions.map((s) => (
          <div key={s.label} className="proactive-item">
            <strong className="proactive-label">{s.label}</strong>
            <span className="proactive-desc">{s.description}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Reasoning ticker (telegraph)
// ---------------------------------------------------------------------------
//
// While the agent is generating, a small monospace stack at the bottom of the
// chat ghost-fades through short reasoning fragments. Mock content for now;
// real reasoning streaming is a separate concern. Each line fades in 300ms,
// holds 1.5s, fades out 300ms; lines are staggered 400ms apart and the ticker
// loops while `active` is true.

const MOCK_TICKER_LINES = [
  'low fundamental… 55Hz…',
  'soft saturation…',
  'long release…',
  'opening filter…',
  'shaping body…',
];

interface ReasoningTickerProps {
  active: boolean;
}

function ReasoningTicker({ active }: ReasoningTickerProps) {
  // Index of which mock line currently leads the stack. Bumps on a cadence
  // while active. When `active` flips off, the whole ticker container fades
  // out via the CSS opacity transition and unmounts on the next tick.
  const [cursor, setCursor] = useState(0);

  useEffect(() => {
    if (!active) return;
    setCursor(0);
    const id = window.setInterval(() => {
      setCursor((c) => (c + 1) % MOCK_TICKER_LINES.length);
    }, 400);
    return () => window.clearInterval(id);
  }, [active]);

  // Show three visible lines at any moment, each with its own fade phase
  // offset. CSS animation on `.reasoning-line` drives the 300/1500/300 cycle.
  // We use the cursor + offset to pick text, keying on cursor so React
  // remounts the spans and restarts each animation on every cadence tick.
  const lines = [0, 1, 2].map((offset) => {
    const idx = (cursor + offset) % MOCK_TICKER_LINES.length;
    return (
      <span
        key={`${cursor}-${offset}`}
        className="reasoning-line"
        style={{ animationDelay: `${offset * 400}ms` }}
      >
        {MOCK_TICKER_LINES[idx]}
      </span>
    );
  });

  return (
    <div
      className={`reasoning-ticker${active ? ' is-active' : ''}`}
      aria-hidden="true"
    >
      {active && lines}
    </div>
  );
}

// ---------------------------------------------------------------------------
// Status indicator
// ---------------------------------------------------------------------------

function StatusDot({ status }: { status: string }) {
  const label =
    status === 'open'
      ? 'Connected'
      : status === 'connecting'
        ? 'Connecting…'
        : 'Offline — retrying';
  return (
    <span className={`status-dot ${status}`} title={label} aria-label={label} />
  );
}

// ---------------------------------------------------------------------------
// Main chat interface
// ---------------------------------------------------------------------------

const SESSION_ID = nanoid();

interface ChatInterfaceProps {
  externalTranscript?: string;
  onAudio?: (buf: ArrayBuffer) => void;
  onSelectVariation?: (patch: PatchPreviewData) => void;
}

export function ChatInterface({ externalTranscript, onAudio, onSelectVariation }: ChatInterfaceProps = {}) {
  const inputId = useId();
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [inputValue, setInputValue] = useState('');
  const [isGenerating, setIsGenerating] = useState(false);
  const [proactiveSuggestions, setProactiveSuggestions] = useState<ProactiveSuggestion[]>([]);
  // ID of the just-submitted user message — the bubble shows the cyan
  // reading-sweep underline while this matches, then clears after 800ms.
  const [submittedFlashId, setSubmittedFlashId] = useState<string | null>(null);
  // Brief scale-down pulse on the send button (100ms). CSS owns the
  // animation; we just toggle the class.
  const [sendPulse, setSendPulse] = useState(false);
  // Focus state on the prompt input — drives the shimmer/solid border
  // distinction (idle = shimmer, focused = solid violet).
  const [inputFocused, setInputFocused] = useState(false);
  const listEndRef = useRef<HTMLDivElement>(null);
  const streamingIdRef = useRef<string | null>(null);

  const { status, send, lastMessage } = useSynthBridge();

  // Populate input from external transcript (Whisper STT result)
  useEffect(() => {
    if (externalTranscript) setInputValue(externalTranscript);
  }, [externalTranscript]);

  // Scroll to bottom on new messages
  useEffect(() => {
    listEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  // Handle incoming WebSocket frames
  useEffect(() => {
    if (!lastMessage) return;
    handleWireMessage(lastMessage);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [lastMessage]);

  const handleWireMessage = useCallback(
    (msg: WireIncoming) => {
      switch (msg.type) {
        case 'token': {
          const sid = streamingIdRef.current;
          if (!sid) return;
          setMessages((prev) =>
            prev.map((m) =>
              m.id === sid ? { ...m, content: m.content + msg.content } : m,
            ),
          );
          break;
        }
        case 'patch': {
          const sid = streamingIdRef.current;
          if (!sid) return;
          setMessages((prev) =>
            prev.map((m) => {
              if (m.id !== sid) return m;
              if (msg.variation === 'A' || msg.variation === 'B') {
                const existing = m.variations ?? [];
                const typedVariation: PatchVariation = { label: msg.variation, patch: msg.data };
                return { ...m, variations: [...existing, typedVariation] };
              }
              return { ...m, patch: msg.data };
            }),
          );
          break;
        }
        case 'done': {
          const sid = streamingIdRef.current;
          if (!sid) return;
          setMessages((prev) =>
            prev.map((m) => (m.id === sid ? { ...m, streaming: false } : m)),
          );
          streamingIdRef.current = null;
          setIsGenerating(false);
          break;
        }
        case 'error': {
          const sid = streamingIdRef.current;
          if (sid) {
            setMessages((prev) =>
              prev.map((m) =>
                m.id === sid
                  ? { ...m, content: m.content + `\n⚠ ${msg.message}`, streaming: false }
                  : m,
              ),
            );
          }
          streamingIdRef.current = null;
          setIsGenerating(false);
          break;
        }
        case 'rationale': {
          const sid = streamingIdRef.current;
          if (!sid) return;
          setMessages((prev) =>
            prev.map((m) => (m.id === sid ? { ...m, rationale: msg.text } : m)),
          );
          break;
        }
        case 'suggest_variations': {
          setProactiveSuggestions(msg.variations);
          break;
        }
      }
    },
    [],
  );

  const handleFeedback = useCallback(
    (messageId: string, kind: FeedbackKind) => {
      setMessages((prev) =>
        prev.map((m) => (m.id === messageId ? { ...m, feedback: kind } : m)),
      );
      const msg = messages.find((m) => m.id === messageId);
      send({
        type: 'feedback',
        messageId,
        kind,
        patch: msg?.patch ?? msg?.variations?.[0]?.patch,
      });
    },
    [messages, send],
  );

  const submit = useCallback(() => {
    const prompt = inputValue.trim();
    if (!prompt || isGenerating) return;

    const userMsg: ChatMessage = {
      id: nanoid(),
      role: 'user',
      content: prompt,
    };

    const assistantId = nanoid();
    const assistantMsg: ChatMessage = {
      id: assistantId,
      role: 'assistant',
      content: '',
      streaming: true,
    };

    setMessages((prev) => [...prev, userMsg, assistantMsg]);
    setInputValue('');
    setIsGenerating(true);
    streamingIdRef.current = assistantId;
    // Theatrical reading-sweep on the just-submitted bubble — 800ms.
    setSubmittedFlashId(userMsg.id);
    window.setTimeout(() => {
      setSubmittedFlashId((cur) => (cur === userMsg.id ? null : cur));
    }, 800);
    // Send-button micro-pulse — CSS handles the 0.92 scale via the class.
    setSendPulse(true);
    window.setTimeout(() => setSendPulse(false), 100);

    send({ type: 'generate', prompt, sessionId: SESSION_ID });

    // If bridge is offline, simulate a graceful offline state after a tick
    if (status !== 'open') {
      setTimeout(() => {
        setMessages((prev) =>
          prev.map((m) =>
            m.id === assistantId
              ? {
                  ...m,
                  content: '⚡ Agent bridge is offline. Start the bridge server on ws://localhost:8765 to enable live generation.',
                  streaming: false,
                }
              : m,
          ),
        );
        streamingIdRef.current = null;
        setIsGenerating(false);
      }, 600);
    }
  }, [inputValue, isGenerating, send, status]);

  const handleKeyDown = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      submit();
    }
  };

  const handleSubmit = (e: FormEvent) => {
    e.preventDefault();
    submit();
  };

  return (
    <div className="chat-shell">
      <header className="chat-header">
        <div className="chat-wordmark">
          <span className="chat-title">TIMBRE</span>
          <span className="chat-tagline">Say it. Hear it.</span>
        </div>
        <StatusDot status={status} />
      </header>

      <section className="message-list" aria-live="polite" aria-label="Conversation">
        {messages.length === 0 && (
          <div className="empty-state">
            <p>Describe a sound and the agent will generate a patch.</p>
            <p className="empty-examples">Try: <em>"dark sub bass"</em>, <em>"bright evolving pad"</em>, <em>"plucky acid lead"</em></p>
          </div>
        )}
        {messages.map((msg) => (
          <MessageBubble
            key={msg.id}
            message={msg}
            onFeedback={handleFeedback}
            onSelectVariation={onSelectVariation}
            auditionSend={send}
            auditionReady={status === 'open'}
            submittedFlash={submittedFlashId === msg.id}
          />
        ))}
        {proactiveSuggestions.length > 0 && (
          <ProactiveSuggestionStrip
            suggestions={proactiveSuggestions}
            onDismiss={() => setProactiveSuggestions([])}
          />
        )}
        <div ref={listEndRef} />
      </section>

      <ReasoningTicker active={isGenerating} />

      <form className="input-bar" onSubmit={handleSubmit} aria-label="Prompt input">
        <label htmlFor={inputId} className="visually-hidden">
          Describe a sound
        </label>
        <div
          className={`prompt-input-wrap${inputFocused ? ' is-focused' : ''}${isGenerating ? ' is-generating' : ''}`}
        >
          <textarea
            id={inputId}
            className="prompt-input"
            value={inputValue}
            onChange={(e) => setInputValue(e.target.value)}
            onKeyDown={handleKeyDown}
            onFocus={() => setInputFocused(true)}
            onBlur={() => setInputFocused(false)}
            placeholder="Describe a sound… (⌘K — Enter to send, Shift+Enter for newline)"
            disabled={isGenerating}
            rows={2}
          />
        </div>
        {onAudio && (
          <PushToTalk
            onData={onAudio}
            wsReady={status === 'open'}
          />
        )}
        <button
          type="submit"
          className={`send-btn${sendPulse ? ' send-pulse' : ''}${isGenerating ? ' is-generating' : ''}`}
          disabled={!inputValue.trim() || isGenerating}
          aria-label="Send prompt"
        >
          {isGenerating ? (
            <span className="send-dots" aria-hidden="true">
              <span />
              <span />
              <span />
            </span>
          ) : (
            <span aria-hidden="true">→</span>
          )}
        </button>
      </form>
    </div>
  );
}
