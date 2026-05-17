import { useEffect, useRef, useState } from 'react';
import './FailureBanner.css';

// Phase C failure-state UX (#269) — first-class banner for the four
// surfaced failure modes. Calm muted-violet, never red, never apologetic.
// One concrete next action per kind. Auto-dismisses when a fresh
// successful patch arrives (caller clears the failure state).
//
// Brand voice contract (Phase 30 UX panel):
//   - No "we're sorry" / "oops" / "error".
//   - No raw error codes, no Gemini jargon, no stack traces.
//   - One line; one action; calm color.
//   - aria-live="polite" so screen readers announce without interrupting.

export type FailureKind = 'llm_offline' | 'prompt_unclear' | 'safety_block' | 'mic_denied';

export interface FailureBannerProps {
  kind: FailureKind;
  // Optional engineering detail surfaced behind a "Why?" disclosure. Kept
  // opaque to the user by default — the disclosure is the escape hatch for
  // power users who want to see what was swapped or what the LLM diagnostic
  // looked like.
  detail?: string;
  onRetry?: () => void;
  onDismiss: () => void;
}

interface CopyFor {
  message: string;
  retryLabel?: string;
}

function copyForKind(kind: FailureKind): CopyFor {
  switch (kind) {
    case 'llm_offline':
      return {
        message: 'TIMBRE is using a backup recipe — the agent is offline right now.',
        retryLabel: 'Retry',
      };
    case 'prompt_unclear':
      return {
        message:
          "I didn't quite catch what you're hearing — mention a sound type (bass, pad, lead, bell)?",
      };
    case 'safety_block':
      return {
        message:
          'That phrasing tripped a content filter — TIMBRE swapped a few words. The shipped sound is from the rephrased version.',
      };
    case 'mic_denied':
      // Handled inline by PushToTalk's pre-press probe (Phase 28). This
      // case is kept exhaustive so future routing changes show up as a
      // compile error rather than a silent fall-through.
      return {
        message: 'Mic access is off — the push-to-talk button is disabled until permission is granted.',
      };
  }
}

export function FailureBanner({ kind, detail, onRetry, onDismiss }: FailureBannerProps) {
  // Mount-in fade. `enter` flips true on first render after mount so the
  // CSS keyframe runs; respects prefers-reduced-motion via CSS, not JS.
  const [enter, setEnter] = useState(false);
  const mountedRef = useRef(false);
  useEffect(() => {
    if (mountedRef.current) return;
    mountedRef.current = true;
    // Schedule on a microtask so the initial render is "before enter"
    // and the keyframe transition runs from opacity 0 → 1.
    requestAnimationFrame(() => setEnter(true));
  }, []);

  const { message, retryLabel } = copyForKind(kind);
  const showRetry = !!onRetry && !!retryLabel;
  const showDetail = !!detail && detail.length > 0;

  return (
    <div
      className={`failure-banner failure-${kind}${enter ? ' is-entered' : ''}`}
      role="status"
      aria-live="polite"
      aria-atomic="true"
    >
      <span className="failure-banner-text">{message}</span>
      {showDetail && (
        <details className="failure-banner-detail">
          <summary>Why?</summary>
          <p>{detail}</p>
        </details>
      )}
      <div className="failure-banner-actions">
        {showRetry && (
          <button
            type="button"
            className="failure-banner-action failure-banner-retry"
            onClick={onRetry}
            aria-label={retryLabel}
          >
            {retryLabel}
          </button>
        )}
        <button
          type="button"
          className="failure-banner-action failure-banner-dismiss"
          onClick={onDismiss}
          aria-label="Dismiss"
        >
          Dismiss
        </button>
      </div>
    </div>
  );
}
