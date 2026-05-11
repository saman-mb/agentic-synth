# ADR-0005: JUCE 8 WebView Hosting a Bundled React UI

## Status

Accepted

## Date

2026-05-11

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

The original UI was a React app in a separate browser process, coupled to the plugin via a `WebSocketBridge`. It shipped two artifacts, prompted for firewall access, did not survive DAW save/restore cleanly, and forced users to keep a browser window open alongside the DAW. Over seven phases we moved the UI inside the plugin binary using JUCE 8's `juce::WebBrowserComponent`.

## Decision Drivers

- Single artifact per plugin format (VST3 / AU / standalone).
- No firewall prompt; no separate process to manage.
- DAW save/restore should round-trip naturally.
- Preserve the existing React + TypeScript UI investment.

## Considered Options

- JUCE 8 `WebBrowserComponent` with bundled React assets (chosen)
- Native `juce::Component` rewrite
- Flutter UI embedded in the plugin window
- Two-process WebSocket coupling (prior)

## Decision Outcome

Chosen option: `JUCE 8 WebBrowserComponent with bundled React assets`

`WebUiComponent` (`src/ui/WebUiComponent.h`, `.cpp`) owns a `TelemetryAwareBrowser` subclass of `juce::WebBrowserComponent`. The React bundle is embedded via `juce_add_binary_data` and served by a static resource provider (`serveResource`). Filename mangling matches JUCE's `makeBinaryDataIdentifierName`. The native↔JS bridge uses `withNativeFunction` (UI → C++, 8 functions) and `emitEventIfBrowserIsVisible` (C++ → UI, 8 typed `AgentBridge` callbacks). `WebSocketBridge` is deleted.

Lifecycle hooks (`pageAboutToLoad`, `pageFinishedLoading`, `pageLoadHadNetworkError`) route into `AgentBridge::telemetry`; on load failure the component swaps in a `FallbackComponent` with the error and a "Copy diagnostic" button. Per-instance WebView2 user-data folders (Windows) are disambiguated by a persisted install-stable UUID so plugin re-instantiation does not collide.

## Pros and Cons of the Options

### JUCE 8 WebBrowserComponent + bundled React

- Pros: Single binary; no firewall prompt; DAW save/restore behaves naturally; reuses React investment; testable resource provider; per-instance isolation on Windows.
- Cons: WebView footprint ~40-80 MB per instance; runtime dependency on platform WebView (WKWebView / WebView2 / WebKitGTK); slower cold-start than a native panel.

### Native `juce::Component` rewrite

- Pros: Lowest memory; no WebView runtime.
- Cons: Wastes existing React work; slower UI iteration.

### Flutter embedded UI

- Pros: Modern widget toolkit.
- Cons: Embedding Flutter in a DAW plugin window is technically hostile; no proven path; large runtime.

### Two-process WebSocket coupling (prior)

- Pros: Strict process isolation.
- Cons: Two artifacts; firewall prompts; awkward state save; user must keep the companion app alive.

## Links

- [src/ui/WebUiComponent.h](../../src/ui/WebUiComponent.h)
- [src/ui/WebUiComponent.cpp](../../src/ui/WebUiComponent.cpp)
- [docs/local-inference.md](../local-inference.md)
