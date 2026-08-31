import { useCallback, useEffect, useState } from 'react';
import { markOnboardingCompleted } from '../data/quickStartPreset';
import './OnboardingOverlay.css';

// Phase C onboarding (#256) — 3-step guided tour for first-time users.
//
// Brand voice (Phase 30 Brand Guardian):
//   - Musician register only. No "module", "config", "settings",
//     "advanced", "tutorial", "walkthrough", "help", "onboarding" in
//     user-visible text.
//   - "Skip" is acceptable; "Skip for now" is banned (implies it returns).
//   - "Open the hood" never taught here — discovered later.
//
// Step ordering — each step pins to a DOM target via querySelector so we
// don't need to invasively wire refs out of deeply-nested components:
//   1. chat input         — ".prompt-input"
//   2. variation grid     — ".ab-grid"             (only after first patch)
//   3. macros             — ".play-macros"         (on the play surface)
//
// Step 2 unlock waits for the parent App/ChatInterface to flip
// `patchLanded` true (the first time a `patch` wire event arrives), so
// the tour doesn't taunt the user with a tile-grid that doesn't exist
// yet. Step 3 unlocks on step-2 dismiss.

export type OnboardingStep = 1 | 2 | 3;

interface OnboardingOverlayProps {
  // True iff the first assistant patch has landed (variation grid is
  // rendered). Until then step 2 is held back.
  patchLanded: boolean;
  // Fires when the user finishes (or skips) the tour. Parent records the
  // completion in localStorage; we also write the flag here as a belt-
  // and-braces guard so the overlay never reappears even if the parent
  // ignores the callback.
  onComplete: () => void;
}

interface StepCopy {
  title?: string;
  body: string;
  targetSelector: string;
  // Where to place the pointer arrow relative to the target. The CSS uses
  // this hint to position the card + arrow tip.
  placement: 'above' | 'below' | 'right';
}

const STEP_COPY: Record<OnboardingStep, StepCopy> = {
  1: {
    body: "Describe a sound. Try 'warm cinematic pad'.",
    targetSelector: '.prompt-input',
    placement: 'above',
  },
  2: {
    body: 'Hear what else the agent considered.',
    targetSelector: '.ab-grid',
    placement: 'right',
  },
  3: {
    body: 'Twist these. The agent stays out of your way.',
    targetSelector: '.play-surface-macros',
    placement: 'below',
  },
};

// Rough rectangle of the targeted DOM node so we can pin the spotlight
// + arrow tip to it without forcing a layout-aware ref into every child.
interface AnchorRect {
  top: number;
  left: number;
  width: number;
  height: number;
}

function readAnchor(selector: string): AnchorRect | null {
  if (typeof document === 'undefined') return null;
  const el = document.querySelector<HTMLElement>(selector);
  if (!el) return null;
  const r = el.getBoundingClientRect();
  return { top: r.top, left: r.left, width: r.width, height: r.height };
}

export function OnboardingOverlay({ patchLanded, onComplete }: OnboardingOverlayProps) {
  const [step, setStep] = useState<OnboardingStep>(1);
  const [anchor, setAnchor] = useState<AnchorRect | null>(null);
  // `dismissed` removes the overlay entirely. We keep the component
  // mounted long enough to play the 200ms fade-out before unmount.
  const [dismissed, setDismissed] = useState(false);

  const advance = useCallback(() => {
    setStep((cur) => {
      if (cur === 1) return 2;
      if (cur === 2) return 3;
      // Final step → finish.
      return cur;
    });
  }, []);

  const finish = useCallback(() => {
    setDismissed(true);
    markOnboardingCompleted();
    // Defer the parent notification slightly so the fade-out keyframe
    // gets to run. Honor reduced-motion by completing immediately.
    const reduceMotion =
      typeof window !== 'undefined' &&
      window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    const delay = reduceMotion ? 0 : 220;
    window.setTimeout(() => onComplete(), delay);
  }, [onComplete]);

  // Step 2 needs the variation grid to actually exist before we point at
  // it. While we're on step 1, ignore the gating; once we land on step 2
  // we re-poll the anchor every animation frame until the grid mounts
  // (capped at ~5s of polling so a permanent failure doesn't hang the
  // tour). If the grid never lands we transparently advance to step 3.
  useEffect(() => {
    if (dismissed) return;
    const target = STEP_COPY[step].targetSelector;

    let stop = false;
    let pollCount = 0;
    const maxPolls = 300; // ~5s @ 60fps

    const tick = () => {
      if (stop) return;
      const next = readAnchor(target);
      if (next) {
        setAnchor(next);
      } else if (step === 2) {
        pollCount += 1;
        if (pollCount >= maxPolls) {
          // Grid never appeared — skip step 2.
          setStep(3);
          return;
        }
      } else {
        setAnchor(null);
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    return () => { stop = true; };
  }, [step, dismissed]);

  // Auto-advance from step 1 → 2 the moment a patch lands. The user has
  // already completed step 1's gesture (typed a prompt), so we don't
  // need them to click "Got it" — but we DO leave step 2 manual because
  // they need to actually look at the variation grid.
  useEffect(() => {
    if (!patchLanded) return;
    if (dismissed) return;
    setStep((cur) => (cur === 1 ? 2 : cur));
  }, [patchLanded, dismissed]);

  // Reposition on window resize so the spotlight tracks the target.
  useEffect(() => {
    if (dismissed) return;
    const onResize = () => {
      const target = STEP_COPY[step].targetSelector;
      const next = readAnchor(target);
      if (next) setAnchor(next);
    };
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, [step, dismissed]);

  if (dismissed) return null;

  const copy = STEP_COPY[step];

  // Spotlight rectangle — a generous halo around the target so the user
  // can see what we're pointing at without the card itself sitting on
  // top of it.
  const spotlightStyle = anchor
    ? {
        top: `${Math.max(0, anchor.top - 8)}px`,
        left: `${Math.max(0, anchor.left - 8)}px`,
        width: `${anchor.width + 16}px`,
        height: `${anchor.height + 16}px`,
      }
    : undefined;

  // Card placement — sits adjacent to the spotlight per placement hint.
  // Falls back to centred when the anchor hasn't resolved yet (eg.
  // before .ab-grid renders on step 2).
  let cardStyle: React.CSSProperties = {};
  if (anchor) {
    const cardOffset = 18;
    if (copy.placement === 'above') {
      cardStyle = {
        bottom: `calc(100vh - ${anchor.top - cardOffset}px)`,
        left: `${anchor.left}px`,
        maxWidth: `min(380px, calc(100vw - ${anchor.left}px - 16px))`,
      };
    } else if (copy.placement === 'below') {
      cardStyle = {
        top: `${anchor.top + anchor.height + cardOffset}px`,
        left: `${anchor.left}px`,
        maxWidth: `min(380px, calc(100vw - ${anchor.left}px - 16px))`,
      };
    } else {
      // right
      cardStyle = {
        top: `${anchor.top}px`,
        left: `${anchor.left + anchor.width + cardOffset}px`,
        maxWidth: '320px',
      };
    }
  }

  const isFinalStep = step === 3;

  return (
    <div
      className="onboarding-overlay"
      role="dialog"
      aria-modal="false"
      aria-label="First-launch tour"
    >
      <div className="onboarding-dim" aria-hidden="true" />
      {anchor && (
        <div className="onboarding-spotlight" style={spotlightStyle} aria-hidden="true" />
      )}
      <div
        className={`onboarding-card onboarding-card--${copy.placement}`}
        style={cardStyle}
      >
        <div className="onboarding-step-indicator" aria-hidden="true">
          {[1, 2, 3].map((n) => (
            <span
              key={n}
              className={`onboarding-step-dot${n === step ? ' is-current' : ''}${n < step ? ' is-past' : ''}`}
            />
          ))}
        </div>
        <p className="onboarding-card-body">{copy.body}</p>
        <div className="onboarding-card-actions">
          <button
            type="button"
            className="onboarding-card-skip"
            onClick={finish}
            aria-label="Skip"
          >
            Skip
          </button>
          <button
            type="button"
            className="onboarding-card-advance"
            onClick={isFinalStep ? finish : advance}
            aria-label={isFinalStep ? 'Got it' : 'Got it, next step'}
          >
            Got it
          </button>
        </div>
      </div>
    </div>
  );
}
