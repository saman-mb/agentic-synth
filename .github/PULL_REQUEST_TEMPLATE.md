<!--
Thanks for contributing. Keep this short — a few honest sentences beat a
filled-in form. Delete any section that doesn't apply.
-->

## What and why

<!-- What changes, and what problem it solves. Link issues: "Fixes #123". -->

## How it was verified

<!--
Tick what you actually ran. Please don't tick something you skipped — an
honest gap is far more useful to a reviewer than a false green.
-->

- [ ] `npx nx build web` — web dist builds
- [ ] `npx nx run-many -t test lint` — Nx targets pass
- [ ] `cmake --build build --parallel` — builds clean
- [ ] `ctest --test-dir build --output-on-failure` — tests pass
- [ ] Listened to it — the audio actually sounds right
- [ ] Loaded in a DAW as VST3/AU

<!--
`ctest` currently has one known pre-existing failure:
  495 - Phase-3 wiring: osc0_enabled toggle changes audio
If that's the only one, say so. If anything else fails, describe it here.
-->

## Audio-thread safety

<!--
Delete this section if your change doesn't touch the audio path.
Otherwise confirm: no allocation, no locks, no I/O, no exceptions in
processBlock or anything it calls.
-->

- [ ] No allocation, locking, file I/O, or logging on the audio thread
- [ ] Parameter changes are smoothed or otherwise zipper-free

## Notes for the reviewer

<!--
Anything you're unsure about, deliberately left out, or would like a second
opinion on. Flagging a known weakness is welcome, not penalised.
-->
