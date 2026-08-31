// ── Web-demo bootstrap (issue #280) ─────────────────────────────────
//
// Imported FIRST in main.tsx — before App (and therefore before any
// component module) is evaluated. Module-scope code that captures bridge
// presence runs at import time, so the shim must exist by then; a
// body-level install call was too late for every such capture.
//
// Self-guarding: inside the plugin WebView the real backend is already
// present, installWebDemoShim() no-ops, and this module is dead code —
// the plugin build stays pixel-identical.
import { installWebDemoShim } from './juceShim';

if (!(window as unknown as { __JUCE__?: unknown }).__JUCE__) installWebDemoShim();
