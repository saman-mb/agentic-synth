// ── Minimal ambient types for the Netlify functions ─────────────────
//
// netlify/tsconfig.json compiles with "types": [] (the functions bundle
// has zero npm dependencies), so the few Node globals used by the
// functions are declared here. Fetch/Request/Response/URLSearchParams
// and friends come from the DOM lib via the tsconfig "lib" array —
// prefer widening "lib" over growing this file.

// Node runtime env access (handlers read GEMINI_KEY + RATE_LIMIT_*).
declare const process: {
  env: Record<string, string | undefined>;
};

// apps/web/src/components/Knob.tsx is pulled into the typecheck via the codec
// import chain and does `import "./Knob.css"`.
declare module "*.css";
