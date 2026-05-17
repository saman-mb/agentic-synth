import {
  FormEvent,
  KeyboardEvent,
  useCallback,
  useEffect,
  useId,
  useMemo,
  useRef,
  useState,
} from 'react';

import { useSynthBridge } from '../hooks/useSynthBridge';
import type {
  ChatMessage,
  FeedbackKind,
  AgentModulationPlan,
  PatchPreviewData,
  PatchVariation,
  ProactiveSuggestion,
  WireIncoming,
  WireOutgoing,
} from '../types/chat';
import { AuditionKeyboard } from './AuditionKeyboard';
import { ConfidenceChips } from './ConfidenceChips';
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
  // Optional fragment of the prompt that produced these variations —
  // used as a fallback label when sensory descriptors aren't supplied.
  promptFragment?: string;
  onSelectVariation?: (patch: PatchPreviewData, modulation?: AgentModulationPlan) => void;
  // Phase B simple-view (#249) — fires the `morph_request` wire frame so
  // the C++ side can generate 5 fresh tiles. Caller decides whether to
  // wire it (omit on bubbles where morphing makes no sense, e.g. when the
  // bridge is offline).
  onMoreVariations?: () => void;
  // Phase B reveal animation (#263) — bumped every time the variations
  // array is replaced. We use this as a key seed so dominant + thumb
  // tiles restart their staggered fade-in keyframes on each new family
  // instead of resolving to "no change" and skipping the animation.
  revealKey?: number;
}

// Per the spec: 1 dominant tile at full message width, the remaining
// variations as thumbnails below. Click a thumbnail to promote it into
// the dominant slot. Tiles label with sensory descriptors when present
// (PatchVariation.label can be 'A' / 'B' today; future-proof for any
// string), fall back to a prompt fragment, then to letters as last
// resort. Empty variation grid renders nothing — silence is on-brand.
function ABVariationGrid({
  variations,
  promptFragment,
  onSelectVariation,
  onMoreVariations,
  revealKey,
}: ABGridProps) {
  const [dominantIndex, setDominantIndex] = useState(0);
  // Reset dominance when the variation list itself changes identity
  // (new generation streamed in). Keep within bounds defensively.
  useEffect(() => {
    setDominantIndex((cur) => (cur < variations.length ? cur : 0));
  }, [variations.length]);

  if (variations.length === 0) return null;
  const safeIndex = Math.min(dominantIndex, variations.length - 1);
  const dominant = variations[safeIndex];
  const others = variations.filter((_, i) => i !== safeIndex);

  const tileLabel = (v: PatchVariation, fallbackIndex: number): string => {
    // Hooks for future sensory labels — today PatchVariation.label is
    // 'A' | 'B'. When the backend ships descriptors like "warmer" or
    // "brighter" they'll come through here verbatim.
    const raw = String(v.label ?? '').trim();
    if (raw && raw !== 'A' && raw !== 'B') return raw;
    if (promptFragment && promptFragment.length > 0) {
      const trimmed = promptFragment.length > 28
        ? `${promptFragment.slice(0, 26)}…`
        : promptFragment;
      return raw ? `${trimmed} · ${raw}` : trimmed;
    }
    return raw || String.fromCharCode(65 + fallbackIndex);
  };

  // Phase B reveal (#263) — key the dominant tile + thumb wrapper by revealKey
  // so the staggered fade-in restarts each time a fresh family of variations
  // lands. We rely on CSS keyframes (defined in ChatInterface.css) keyed on
  // `.ab-grid-revealing` for the animation; the key prop forces React to
  // remount so the animation actually replays instead of being skipped as
  // "already applied".
  const revealClass = revealKey !== undefined ? ' ab-grid-revealing' : '';

  return (
    <div className="ab-grid ab-grid--inline" role="region" aria-label="Patch variations">
      <div
        key={`dominant-${revealKey ?? 0}`}
        className={`ab-grid-dominant${revealClass}`}
      >
        <PatchPreview patch={dominant.patch} label={tileLabel(dominant, safeIndex)} />
        <div className="ab-grid-dominant-actions">
          {onSelectVariation && (
            <button
              type="button"
              className="ab-commit-btn"
              onClick={() => onSelectVariation(dominant.patch, dominant.modulation)}
              aria-label={`Use ${tileLabel(dominant, safeIndex)}`}
            >
              Use {tileLabel(dominant, safeIndex)}
            </button>
          )}
          {onMoreVariations && (
            <button
              type="button"
              className="ab-more-btn"
              onClick={onMoreVariations}
              aria-label="Generate more variations"
            >
              More variations
            </button>
          )}
        </div>
      </div>
      {others.length > 0 && (
        <div
          key={`thumbs-${revealKey ?? 0}`}
          className={`ab-grid-thumbs${revealClass}`}
          role="group"
          aria-label="Other variations"
        >
          {variations.map((v, i) => {
            if (i === safeIndex) return null;
            // Thumb index sequence (0..N-1) skipping the dominant slot so the
            // stagger reads left-to-right regardless of which tile is currently
            // promoted. CSS reads `--thumb-index` for the per-tile delay.
            const thumbIndex = i < safeIndex ? i : i - 1;
            return (
              <button
                key={`${v.label}-${i}-${revealKey ?? 0}`}
                type="button"
                className="ab-grid-thumb"
                style={{ ['--thumb-index' as string]: thumbIndex }}
                onClick={() => setDominantIndex(i)}
                aria-label={`Promote ${tileLabel(v, i)} to dominant`}
              >
                <PatchPreview patch={v.patch} label={tileLabel(v, i)} />
              </button>
            );
          })}
        </div>
      )}
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
  // The user prompt that produced this assistant reply (if any). Used
  // as fallback label text on variation tiles when the backend hasn't
  // emitted sensory descriptors yet.
  promptFragment?: string;
  onFeedback: (messageId: string, kind: FeedbackKind) => void;
  onSelectVariation?: (patch: PatchPreviewData, modulation?: AgentModulationPlan) => void;
  // When provided, an AuditionKeyboard renders next to single-patch
  // previews so the producer can audition the assistant's patch without
  // a DAW attached. Wired only to the single-patch path on purpose:
  // the A/B grid is Engineer B's territory and adds its own audition UI.
  auditionSend?: (frame: WireOutgoing) => void;
  auditionReady?: boolean;
  // When true, the bubble text shows the cyan reading-sweep underline.
  submittedFlash?: boolean;
  // Phase B simple-view (#249) — wired through to the variation grid when
  // the bubble's message owns the active variation set. ChatInterface
  // resolves which message can fire morph_request (most recent assistant
  // with variations) before forwarding.
  onMoreVariations?: () => void;
  // Phase B reveal (#263) — bumped on every variations_ready landing for
  // the message so the grid restarts its staggered fade-in keyframes.
  revealKey?: number;
}

function MessageBubble({
  message,
  promptFragment,
  onFeedback,
  onSelectVariation,
  auditionSend,
  auditionReady,
  submittedFlash,
  onMoreVariations,
  revealKey,
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

      {message.augmenterActions && message.augmenterActions.length > 0 && !message.streaming && (
        <aside className="augmenter-banner" role="status" aria-label="Patch adjustments">
          <span className="augmenter-banner-label">Patch adjusted</span>
          <ul className="augmenter-banner-list">
            {message.augmenterActions.map((a, i) => (
              <li key={`aug-${i}`}>{a}</li>
            ))}
          </ul>
        </aside>
      )}

      {message.variations && message.variations.length > 0 && (
        <ABVariationGrid
          variations={message.variations}
          promptFragment={promptFragment}
          onSelectVariation={onSelectVariation}
          onMoreVariations={onMoreVariations}
          revealKey={revealKey}
        />
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
// Reasoning ticker (telegraph) — driven by the 2-step LLM enhancement brief
// ---------------------------------------------------------------------------
//
// Two phases, brand-voice labels:
//   HEARING IT OUT  — the ENHANCER stage is running, no brief yet.
//                     We show neutral sensory-language placeholders that
//                     match the §2 vocabulary of the translator prompt
//                     (weight / temperature / texture / motion / light)
//                     so the ticker reads like TIMBRE listening, not like
//                     loading dots.
//   SHAPING         — the ENHANCER returned a brief. We split it on its
//                     fixed-section labels and scroll two lines at a time
//                     so the user reads the *real* translation while the
//                     generator works.
//
// The brief stays visible until the `done` event fires; the parent ChatInterface
// then folds the brief into the assistant message's `rationale` so it survives
// in the <details>Why this patch?</details> block.
//
// Accessibility: ticker container is aria-live="polite" + aria-atomic="false"
// so partial updates don't re-announce the whole list. Throttled to one
// announcement per ~1.4s while in SHAPING (lineIndex tick rate) to avoid
// screen-reader spam.

const HEARING_PLACEHOLDERS = [
  'weight…',
  'temperature…',
  'texture…',
  'motion…',
  'light…',
  'space…',
];

// Split an enhancer brief into compact display lines. The translator emits
// nine fixed sections (SONIC CHARACTER, FREQUENCY FOCUS, …, ONE-LINE SUMMARY).
// We display each section as its own scrolling line; long content is left
// intact (CSS `text-overflow: ellipsis` truncates if needed).
function splitBrief(brief: string): string[] {
  const trimmed = brief.trim();
  if (!trimmed) return [];
  // Prefer section split — each section is `LABEL: <content>` on its own line.
  const lines = trimmed
    .split(/\n+/)
    .map((l) => l.trim())
    .filter((l) => l.length > 0);
  return lines.length > 0 ? lines : [trimmed];
}

interface ReasoningTickerProps {
  active: boolean;
  brief: string;
}

function ReasoningTicker({ active, brief }: ReasoningTickerProps) {
  // Phase: 'hearing' while ENHANCER runs (no brief), 'shaping' once we have one.
  const phase: 'hearing' | 'shaping' = brief.length > 0 ? 'shaping' : 'hearing';
  const briefLines = useMemo(() => splitBrief(brief), [brief]);

  // Scrolling cursor — bumps every 1.4s. Window of 2 visible lines.
  const [cursor, setCursor] = useState(0);

  useEffect(() => {
    if (!active) return;
    setCursor(0);
    const sourceLen = phase === 'shaping' ? Math.max(1, briefLines.length) : HEARING_PLACEHOLDERS.length;
    const id = window.setInterval(() => {
      setCursor((c) => (c + 1) % sourceLen);
    }, phase === 'shaping' ? 1400 : 600);
    return () => window.clearInterval(id);
  }, [active, phase, briefLines.length]);

  const source = phase === 'shaping' ? briefLines : HEARING_PLACEHOLDERS;
  const visible = [0, 1].map((offset) => {
    if (source.length === 0) return null;
    const idx = (cursor + offset) % source.length;
    return (
      <span
        key={`${phase}-${cursor}-${offset}`}
        className="reasoning-line"
        style={{ animationDelay: `${offset * 400}ms` }}
      >
        {source[idx]}
      </span>
    );
  });

  const label = phase === 'shaping' ? 'SHAPING' : 'HEARING IT OUT';

  return (
    <div
      className={`reasoning-ticker${active ? ' is-active' : ''}`}
      aria-live="polite"
      aria-atomic="false"
    >
      {active && (
        <>
          <span className="reasoning-label" aria-hidden="false">
            {label}
          </span>
          {visible}
        </>
      )}
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
  onSelectVariation?: (patch: PatchPreviewData, modulation?: AgentModulationPlan) => void;
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
  // Network failure UI (Phase 9 §18b + Phase 13 retry wiring).
  // When set, the chat shows a calm grey countdown line below the
  // message list with a "Retry now" link. Auto-retries after 5s;
  // preserves the original prompt for resubmit. After
  // MAX_RETRY_ATTEMPTS consecutive failures we stop retrying and show
  // a final "Try again later" line — no infinite loop on a flapping
  // backend.
  const MAX_RETRY_ATTEMPTS = 3;
  const [networkFailure, setNetworkFailure] = useState<{
    prompt: string;
    countdown: number;
    attempt: number;     // 1-based: 1 = first retry, 2 = second, ...
    exhausted: boolean;  // true after MAX_RETRY_ATTEMPTS — no auto retry
  } | null>(null);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  // Focus state on the prompt input — drives the shimmer/solid border
  // distinction (idle = shimmer, focused = solid violet).
  const [inputFocused, setInputFocused] = useState(false);
  const listEndRef = useRef<HTMLDivElement>(null);
  const streamingIdRef = useRef<string | null>(null);
  // Two-step LLM flow: live brief emitted by the ENHANCER stage. While the
  // ticker is visible we show this verbatim (split by section); on `done`
  // we fold it into the assistant message's rationale and clear it so the
  // next generate starts clean.
  const [currentBrief, setCurrentBrief] = useState<string>('');
  const currentBriefRef = useRef<string>('');
  // Phase B simple-view (#249) — track which assistant message owns the
  // "more variations" replacement target. Whichever message most recently
  // produced a variation grid is the receiver for the next
  // variations_ready event. Bumped on every replacement so the reveal
  // animation actually restarts (revealKey is wired to ABVariationGrid).
  const morphTargetIdRef = useRef<string | null>(null);
  const [revealCount, setRevealCount] = useState<Record<string, number>>({});

  const { status, send, lastMessage, subscribe } = useSynthBridge();

  // Populate input from external transcript (Whisper STT result)
  useEffect(() => {
    if (externalTranscript) setInputValue(externalTranscript);
  }, [externalTranscript]);

  // Scroll to bottom on new messages
  useEffect(() => {
    listEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  // Handle incoming WebSocket frames.
  // Subscribe to the synchronous bridge stream so multi-event bursts
  // (notifyPatch → notifyToken → notifyRationale → notifyDone in one
  // C++ tick) all reach handleWireMessage. React state path (lastMessage)
  // would batch them and only the last frame would propagate.
  useEffect(() => {
    const unsub = subscribe((msg) => handleWireMessage(msg));
    return unsub;
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [subscribe]);

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
                const typedVariation: PatchVariation = {
                  label: msg.variation,
                  patch: msg.data,
                  modulation: msg.modulation,
                };
                // Phase B (#249) — this bubble now owns variations; it
                // becomes the receiver for subsequent variations_ready
                // replacements.
                morphTargetIdRef.current = sid;
                return { ...m, variations: [...existing, typedVariation] };
              }
              return {
                ...m,
                patch: msg.data,
                modulation: msg.modulation,
                augmenterActions: msg.augmenter_actions && msg.augmenter_actions.length > 0
                  ? msg.augmenter_actions
                  : m.augmenterActions,
              };
            }),
          );
          break;
        }
        case 'done': {
          const sid = streamingIdRef.current;
          if (!sid) return;
          // Two-step LLM flow: fold the live brief into the assistant
          // message's rationale block. If a generator-emitted rationale
          // already arrived ('rationale' frame), we prepend the brief so
          // both are visible in <details>Why this patch?</details>. The
          // brief itself fades from the ticker on the next tick.
          const briefSnapshot = currentBriefRef.current;
          setMessages((prev) =>
            prev.map((m) => {
              if (m.id !== sid) return m;
              const merged =
                briefSnapshot.length > 0
                  ? (m.rationale && m.rationale.length > 0
                      ? `${briefSnapshot}\n\n---\n\n${m.rationale}`
                      : briefSnapshot)
                  : m.rationale;
              return { ...m, streaming: false, rationale: merged };
            }),
          );
          streamingIdRef.current = null;
          setIsGenerating(false);
          currentBriefRef.current = '';
          setCurrentBrief('');
          break;
        }
        case 'enhancement': {
          // ENHANCER brief — drive the SHAPING phase of the ticker and
          // stash for the eventual <details> fold-in on `done`.
          currentBriefRef.current = msg.brief;
          setCurrentBrief(msg.brief);
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
          // read it for the failure card. If we're already in a retry
          // cycle for the same prompt, increment the attempt counter
          // and either schedule the next retry or surface the
          // exhausted final state.
          window.setTimeout(() => {
            setNetworkFailure((prev) => {
              if (prev && prev.prompt === lastPrompt) {
                const nextAttempt = prev.attempt + 1;
                if (nextAttempt > MAX_RETRY_ATTEMPTS) {
                  return { ...prev, attempt: nextAttempt, exhausted: true, countdown: 0 };
                }
                return { ...prev, attempt: nextAttempt, countdown: 5 };
              }
              return {
                prompt: lastPrompt,
                countdown: 5,
                attempt: 1,
                exhausted: false,
              };
            });
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
        case 'variations_ready': {
          // Phase B simple-view (#249) — replace the current message's
          // variations with the 5-tile morph result. Bump the per-message
          // reveal counter so ABVariationGrid restarts its staggered
          // fade-in keyframes via `revealKey`.
          const tid = morphTargetIdRef.current;
          if (!tid) return;
          setMessages((prev) =>
            prev.map((m) => (m.id === tid ? { ...m, variations: msg.variations } : m)),
          );
          setRevealCount((prev) => ({ ...prev, [tid]: (prev[tid] ?? 0) + 1 }));
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
    // Two-step LLM flow: clear any stale brief from a previous generate so
    // the ticker starts in HEARING IT OUT mode for this new prompt.
    currentBriefRef.current = '';
    setCurrentBrief('');
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
        setNetworkFailure({ prompt, countdown: 5, attempt: 1, exhausted: false });
      }, 600);
    }
  }, [inputValue, isGenerating, send, status, onRtfmEasterEgg]);

  // ── Network failure retry (Phase 9 §18b + Phase 13) ───────────────
  // Real retry: re-call the bridge with the persisted prompt and a
  // fresh streaming assistant bubble. We do NOT push a new user
  // message — the user's original prompt is still visible in the
  // conversation; we're just retrying the response.
  //
  // Success path: tokens arrive → the streaming bubble fills →
  // 'done' arrives → handleWireMessage sets streaming:false. A
  // separate effect watching streamingIdRef + isGenerating clears
  // networkFailure on the first incoming 'token' frame.
  //
  // Failure path: 'error' arrives (or `status !== 'open'` again) →
  // error-frame handler increments attempt, re-arms countdown, OR
  // sets exhausted:true after MAX_RETRY_ATTEMPTS.
  const retryNow = useCallback(() => {
    setNetworkFailure((cur) => {
      if (!cur) return cur;
      if (cur.exhausted) return cur; // no further retries
      // If the bridge is still closed, surface the failure again on
      // the next tick (same attempt counter — we never actually got
      // to send). This keeps the retry count meaningful: it counts
      // *attempted* sends, not optimistic UI nudges.
      if (status !== 'open') {
        // Park the prompt, tick attempt as a failed send, and either
        // re-arm the countdown or mark exhausted.
        const nextAttempt = cur.attempt + 1;
        if (nextAttempt > MAX_RETRY_ATTEMPTS) {
          return { ...cur, attempt: nextAttempt, exhausted: true, countdown: 0 };
        }
        return { ...cur, attempt: nextAttempt, countdown: 5 };
      }

      // Bridge is open — fire the retry. Inject a fresh streaming
      // assistant bubble keyed to a new id and call send().
      const assistantId = nanoid();
      const assistantMsg: ChatMessage = {
        id: assistantId,
        role: 'assistant',
        content: '',
        streaming: true,
      };
      setMessages((prev) => [...prev, assistantMsg]);
      setIsGenerating(true);
      streamingIdRef.current = assistantId;
      send({ type: 'generate', prompt: cur.prompt, sessionId: SESSION_ID });

      // Pause the countdown while the retry is in flight. The failure
      // state is cleared by the success-detection effect on first
      // token arrival.
      return { ...cur, countdown: 0 };
    });
  }, [send, status]);

  // Tick the countdown each second while networkFailure is active
  // and the retry hasn't been fired yet. countdown=0 with exhausted
  // is a sticky terminal state (no further auto-retry). countdown=0
  // with !exhausted means a retry is in flight — also no tick. So we
  // only schedule the timer when countdown > 0.
  useEffect(() => {
    if (!networkFailure) return;
    if (networkFailure.exhausted) return;
    if (networkFailure.countdown <= 0) return;
    if (networkFailure.countdown === 1) {
      retryTimerRef.current = setTimeout(() => {
        retryNow();
      }, 1000);
    } else {
      retryTimerRef.current = setTimeout(() => {
        setNetworkFailure((cur) =>
          cur ? { ...cur, countdown: cur.countdown - 1 } : cur,
        );
      }, 1000);
    }
    return () => {
      if (retryTimerRef.current) clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
    };
  }, [networkFailure, retryNow]);

  // Success detection: if we're in a retry cycle and a token arrives
  // (which the stream handler writes into the matching message), the
  // bridge has answered — clear the failure state so the countdown UI
  // disappears.
  useEffect(() => {
    if (!networkFailure) return;
    if (networkFailure.exhausted) return;
    const sid = streamingIdRef.current;
    if (!sid) return;
    const m = messages.find((mm) => mm.id === sid);
    if (m && m.role === 'assistant' && m.content.length > 0) {
      setNetworkFailure(null);
    }
  }, [messages, networkFailure]);

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
        {messages.map((msg, idx) => {
          // Look up the most-recent preceding user message — its content
          // becomes the fallback label fragment for any variations on
          // this assistant bubble.
          let promptFragment: string | undefined;
          if (msg.role === 'assistant') {
            for (let i = idx - 1; i >= 0; i--) {
              if (messages[i].role === 'user') {
                promptFragment = messages[i].content;
                break;
              }
            }
          }
          // Phase B simple-view (#249) — only the assistant bubble that
          // currently owns the variation grid gets the "More variations"
          // button. Targeting is one-bubble-at-a-time: clicking the button
          // updates morphTargetIdRef so the C++ reply lands on the same
          // bubble. Hidden when offline so we never queue a request the
          // bridge can't deliver.
          const isMorphTarget =
            msg.role === 'assistant' && (msg.variations?.length ?? 0) > 0;
          const handleMoreVariations = isMorphTarget && status === 'open'
            ? () => {
                morphTargetIdRef.current = msg.id;
                send({ type: 'morph_request' });
              }
            : undefined;
          return (
            <MessageBubble
              key={msg.id}
              message={msg}
              promptFragment={promptFragment}
              onFeedback={handleFeedback}
              onSelectVariation={onSelectVariation}
              auditionSend={send}
              auditionReady={status === 'open'}
              submittedFlash={submittedFlashId === msg.id}
              onMoreVariations={handleMoreVariations}
              revealKey={revealCount[msg.id]}
            />
          );
        })}
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
          {networkFailure.exhausted ? (
            <>
              <span className="chat-network-failure-text">
                Still can't reach the agent. Please try again later.
              </span>
              <button
                type="button"
                className="chat-network-failure-retry"
                onClick={() => setNetworkFailure(null)}
              >
                Dismiss
              </button>
            </>
          ) : networkFailure.countdown > 0 ? (
            <>
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
            </>
          ) : (
            <span className="chat-network-failure-text">
              Retrying… (attempt {networkFailure.attempt} of {MAX_RETRY_ATTEMPTS})
            </span>
          )}
        </div>
      )}

      <ReasoningTicker active={isGenerating} brief={currentBrief} />

      <form className="input-bar" onSubmit={handleSubmit} aria-label="Prompt input">
        <label htmlFor={inputId} className="visually-hidden">
          Describe a sound
        </label>
        <div className="prompt-input-col">
          <div
            className={`prompt-input-wrap${inputFocused ? ' is-focused' : ''}${isGenerating ? ' is-generating' : ''}`}
            title="Enter to send, Shift+Enter for newline"
          >
            <textarea
              id={inputId}
              className="prompt-input"
              value={inputValue}
              onChange={(e) => setInputValue(e.target.value)}
              onKeyDown={handleKeyDown}
              onFocus={() => setInputFocused(true)}
              onBlur={() => setInputFocused(false)}
              placeholder="Describe a sound… (⌘K)"
              disabled={isGenerating}
              rows={3}
            />
          </div>
          {!isGenerating && <ConfidenceChips prompt={inputValue} />}
          {/* Tiny helper line — never wraps, mutes to tertiary so it
              reads as ambient guidance, not chrome. */}
          <span className="prompt-input-hint" aria-hidden="true">
            Enter to send · Shift+Enter for newline
          </span>
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
