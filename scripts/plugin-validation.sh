#!/usr/bin/env bash
# Plugin validation script
set -euo pipefail

PLUGIN_PATH="${1:-build/AgenticSynth.vst3}"

echo "=== pluginval: $PLUGIN_PATH ==="
pluginval --strictness-level 10 --validate "$PLUGIN_PATH" || echo "pluginval: FAILED"

# macOS only
if [[ "$(uname)" == "Darwin" ]]; then
    AU_PATH="${PLUGIN_PATH%.vst3}.component"
    echo "=== auval ==="
    auval -v aumu Vst3 Agnt || echo "auval: FAILED or not found"
fi

echo "=== Done ==="
