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

  // Phase 10 §16 — `sudo make me a sound` easter egg renders as a
  // green-on-black CRT terminal block. Bypasses feedback / patch / cursor.
  if (message.terminal) {
    return (
      <article
        className={`message-bubble ${message.role} terminal-bubble`}
        aria-label="RTFM terminal output"
      >
        <div className="bubble-role">Agent</div>
        <pre className="terminal-message" aria-live="polite">
          {(message.terminalLines ?? []).map((line, i) => (
            <span key={i} className="terminal-line">{line}</span>
          ))}
        </pre>
      </article>
    );
  }

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
  // Phase 10 §16 — `sudo make me a sound` easter egg. The chat detects
  // the prompt locally (no bridge round-trip) and asks App to load the
  // hardcoded RTFM patch. Optional so existing callers stay unchanged.
  onRtfmEasterEgg?: () => void;
}

export function ChatInterface({ externalTranscript, onAudio, onSelectVariation, onRtfmEasterEgg }: ChatInterfaceProps = {}) {
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
  // Wordless head-shake on the send arrow when user submits an empty
  // prompt (Phase 9 §18c). 180ms class-toggle; auto-clears on timer.
  const [shake, setShake] = useState(false);
  // Network failure UI (Phase 9 §18b). When set, the chat shows a calm
  // grey countdown line below the message list with a "Retry now" link.
  // Auto-retries after 5s; preserves the original prompt for resubmit.
  const [networkFailure, setNetworkFailure] = useState<{
    prompt: string;
    countdown: number;
  } | null>(null);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
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
          // Brand-defined failure (Phase 9 §18b): drop technical detail,
          // preserve the user's most recent prompt for one-touch retry,
          // and surface the calm countdown under the message list.
          // We can recover the prompt by walking back through `messages`
          // (set via React state read through closure — fine here, the
          // newest user bubble is always the trigger for the current
          // streaming reply).
          const sid = streamingIdRef.current;
          let lastPrompt = '';
          setMessages((prev) => {
            // Drop the failed assistant bubble entirely.
            const filtered = prev.filter((m) => m.id !== sid);
            const lastUser = [...filtered].reverse().find((m) => m.role === 'user');
            lastPrompt = lastUser?.content ?? '';
            return filtered;
          });
          streamingIdRef.current = null;
          setIsGenerating(false);
          // Log original engineering detail for devs; never to UI.
          // eslint-disable-next-line no-console
          console.debug('[TIMBRE] LLM error:', msg.message);
          // Schedule on a microtask so lastPrompt is set by the time we
          // read it for the failure card.
          window.setTimeout(() => {
            setNetworkFailure({ prompt: lastPrompt, countdown: 5 });
          }, 0);
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
    if (isGenerating) return;
    if (!prompt) {
      // Wordless head-shake on the send arrow (Phase 9 §18c).
      // No error toast, no message. Animation is 180ms; class clears
      // after 200ms so a rapid second Enter can replay the shake.
      setShake(true);
      window.setTimeout(() => setShake(false), 200);
      return;
    }

    // Phase 10 §16 — `sudo make me a sound` easter egg. Match
    // case-insensitively against the trimmed prompt. We do NOT round-trip
    // through the bridge: render a terminal-styled assistant block in the
    // chat and ask App to load the hardcoded RTFM FM patch.
    if (prompt.toLowerCase() === 'sudo make me a sound') {
      const userMsg: ChatMessage = { id: nanoid(), role: 'user', content: prompt };
      const terminalMsg: ChatMessage = {
        id: nanoid(),
        role: 'assistant',
        content: '',
        terminal: true,
        terminalLines: [
          '> initializing FM patch...',
          '> tuning carrier 220Hz...',
          '> sourcing modulator at ratio 3.14...',
          '> RTFM.',
        ],
      };
      setMessages((prev) => [...prev, userMsg, terminalMsg]);
      setInputValue('');
      setSubmittedFlashId(userMsg.id);
      window.setTimeout(() => {
        setSubmittedFlashId((cur) => (cur === userMsg.id ? null : cur));
      }, 800);
      setSendPulse(true);
      window.setTimeout(() => setSendPulse(false), 100);
      if (onRtfmEasterEgg) onRtfmEasterEgg();
      return;
    }

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

    // If the bridge is offline, fall into the brand-defined failure state
    // (Phase 9 §18b): we never destroy the user's typed prompt, we never
    // apologise, we never leak the underlying WebSocket reason. Instead
    // we show a calm countdown line under the conversation and auto-retry
    // after 5 seconds. The user can hit "Retry now" to fire immediately.
    if (status !== 'open') {
      setTimeout(() => {
        // Drop the empty streaming assistant bubble — the failure UI
        // lives below the conversation, not in a bubble.
        setMessages((prev) => prev.filter((m) => m.id !== assistantId));
        streamingIdRef.current = null;
        setIsGenerating(false);
        // Park the prompt for retry; render countdown below the list.
        setNetworkFailure({ prompt, countdown: 5 });
      }, 600);
    }
  }, [inputValue, isGenerating, send, status, onRtfmEasterEgg]);

  // ── Network failure countdown (Phase 9 §18b) ─────────────────────
  // While networkFailure is non-null, tick the countdown each second.
  // At 0, fire the retry: restore the prompt into the input and submit
  // automatically. Manual "Retry now" calls the same handler.
  const retryNow = useCallback(() => {
    setNetworkFailure((cur) => {
      if (!cur) return null;
      // Restore prompt text. Submit happens via a separate effect once
      // inputValue has settled (avoids racing the controlled <textarea>).
      setInputValue(cur.prompt);
      return null;
    });
    // After restoring text, fire submit on the next tick.
    window.setTimeout(() => {
      // submit reads `inputValue` via closure; the most recent render
      // will see the restored prompt. Guard with a tiny timeout so the
      // state flush has landed.
      submit();
    }, 0);
  }, [submit]);

  useEffect(() => {
    if (!networkFailure) return;
    if (networkFailure.countdown <= 0) {
      retryNow();
      return;
    }
    retryTimerRef.current = setTimeout(() => {
      setNetworkFailure((cur) =>
        cur ? { ...cur, countdown: cur.countdown - 1 } : cur,
      );
    }, 1000);
    return () => {
      if (retryTimerRef.current) clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
    };
  }, [networkFailure, retryNow]);

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

      {networkFailure && (
        <div className="chat-network-failure" role="status" aria-live="polite">
          <span className="chat-network-failure-text">
            Couldn't reach the agent. Trying again in {networkFailure.countdown}…
          </span>
          <button
            type="button"
            className="chat-network-failure-retry"
            onClick={retryNow}
          >
            Retry now
          </button>
        </div>
      )}

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
            // Phase 9 §18c — wordless head-shake on empty submit. The
            // `key` change forces remount so the keyframe restarts on
            // a rapid second Enter.
            <span
              key={shake ? 'shake-on' : 'shake-off'}
              className={shake ? 'head-shake' : ''}
              aria-hidden="true"
            >
              →
            </span>
          )}
        </button>
      </form>
    </div>
  );
}
