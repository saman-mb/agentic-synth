# Third-Party Dependencies

This directory holds vendored dependencies and git submodules.

## Submodules

- `JUCE/` — plugin, standalone app, and audio framework. Registered in `.gitmodules`. Initialize with:

  ```sh
  git submodule update --init --recursive
  ```

## Runtime Dependencies (Not Vendored)

- **llama.cpp `llama-server`** — Agentic Synth talks to a running `llama-server` over HTTP (`/completion` and `/embedding` endpoints). It is **not** linked into the build and **not** present in this directory. The user must run `llama-server` separately on the same machine or on a LAN-reachable host. See [docs/local-inference.md](../docs/local-inference.md) for setup.

The repository license covers original Agentic Synth source code only. Review and comply with each dependency's license before shipping commercial builds.
