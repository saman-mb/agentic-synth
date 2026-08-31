import React from 'react';
import { createRoot } from 'react-dom/client';

// Web demo (#280): a plain-browser deploy has no JUCE WebView host, so
// the shim installs at module-evaluation time — BEFORE App (and every
// module-scope bridge check it triggers) is evaluated. Inside the plugin
// the real backend is already present and bootstrap.ts is a no-op.
import './demo/bootstrap';
import { App } from './App';

createRoot(document.getElementById('root') as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
