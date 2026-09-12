import React from 'react';
import { createRoot } from 'react-dom/client';

// Web demo (#280 / #285): a plain-browser deploy has no JUCE WebView host,
// so the shim is loaded before App paints. Inside the plugin the real
// backend is already present and bootstrap.ts is never fetched — the
// engine stays in a separate lazy chunk (issue #285).
import { App } from './App';

async function boot() {
  if (!(window as unknown as { __JUCE__?: unknown }).__JUCE__) {
    await import('./demo/bootstrap');
  }
  createRoot(document.getElementById('root') as HTMLElement).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>,
  );
}

void boot();
