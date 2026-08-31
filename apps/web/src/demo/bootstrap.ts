// ── Web-demo bootstrap (issue #280 / #285) ──────────────────────────
//
// Lazy-imported from main.tsx when window.__JUCE__ is absent — before
// App paints (await-before-render). Module-scope code that captures
// bridge presence runs at import time of this module, so the shim must
// exist by then; a body-level install call was too late for every such
// capture. useSynthBridge / useWebSocket / Visualizer keep per-call
// usingJuce() — do not revert those to module-scope captures.
//
// Self-guarding: inside the plugin WebView the real backend is already
// present, installWebDemoShim() no-ops, and this module is never
// fetched — the plugin build stays pixel-identical aside from the
// unused lazy chunk still GLOB'd into juce_add_binary_data.
import { installWebDemoShim } from './juceShim';

if (!(window as unknown as { __JUCE__?: unknown }).__JUCE__) installWebDemoShim();
