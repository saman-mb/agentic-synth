import React from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { installWebDemoShim } from './demo/juceShim';

// Web demo (#280): a plain-browser deploy has no JUCE WebView host, so
// install the shim before React mounts and the hooks see window.__JUCE__
// from the first render. Inside the plugin the real backend is already
// present and this is a no-op.
if (!(window as unknown as { __JUCE__?: unknown }).__JUCE__) installWebDemoShim();

createRoot(document.getElementById('root') as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
