import React, { useEffect, useState } from 'react';
import './BootSplash.css';

// Splash plays the branded hero animation (/timbre-hero.gif — 3.6s seamless
// loop) for a guaranteed MIN_DISPLAY_MS before the app reveals. Reduced-motion
// users get the static poster frame instead (a GIF cannot be paused, so we
// swap the source rather than pretend).

const MIN_DISPLAY_MS = 3000;
const FADE_MS = 400;

const HERO_GIF = '/timbre-hero.gif';
const HERO_POSTER = '/og-image.png';

interface BootSplashProps {
  onDone?: () => void;
}

type Phase = 'show' | 'fade';

export const BootSplash: React.FC<BootSplashProps> = ({ onDone }) => {
  const [phase, setPhase] = useState<Phase>('show');
  const [reducedMotion, setReducedMotion] = useState<boolean>(() => {
    if (typeof window === 'undefined' || !window.matchMedia) return false;
    return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  });

  // Single useEffect so timers cleanly cancel on unmount.
  useEffect(() => {
    const timers: number[] = [];

    // Phase 1 (0-3000ms): the hero animation holds the screen. The GIF loop
    // is 3.6s, so a 3s floor lands mid-loop — the reveal never looks like a
    // hard "end of animation", it just crossfades away underneath.
    // Phase 2 (3000-3400ms): fade-out class, then unmount via onDone.
    timers.push(window.setTimeout(() => setPhase('fade'), MIN_DISPLAY_MS));
    timers.push(
      window.setTimeout(() => onDone?.(), MIN_DISPLAY_MS + FADE_MS),
    );

    // Honour a live change of the reduced-motion preference mid-splash.
    if (window.matchMedia) {
      const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
      const onChange = (e: MediaQueryListEvent) => setReducedMotion(e.matches);
      mq.addEventListener('change', onChange);
      return () => {
        mq.removeEventListener('change', onChange);
        timers.forEach((t) => window.clearTimeout(t));
      };
    }

    return () => {
      timers.forEach((t) => window.clearTimeout(t));
    };
  }, [onDone]);

  return (
    <div
      className={`boot-splash boot-splash-${phase}`}
      role="presentation"
      aria-hidden="true"
    >
      <div className="boot-splash-stage">
        <img
          className="boot-splash-hero"
          src={reducedMotion ? HERO_POSTER : HERO_GIF}
          alt=""
          draggable={false}
        />
      </div>
    </div>
  );
};
