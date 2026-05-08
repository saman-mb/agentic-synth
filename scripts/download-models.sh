#!/usr/bin/env bash
# Model packaging: download flow for Phi-4 Q4 and Llama 3.2 8B Q4
#
# Usage:
#   ./scripts/download-models.sh              # Download both models
#   ./scripts/download-models.sh phi4          # Download Phi-4 only
#   ./scripts/download-models.sh llama         # Download Llama 3.2 only

set -euo pipefail

MODELS_DIR="${MODELS_DIR:-models}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/agentic-synth/models}"

PHI4_URL="https://huggingface.co/microsoft/Phi-4-mini-instruct-gguf/resolve/main/Phi-4-mini-instruct-q4_k_m.gguf"
PHI4_FILE="phi-4-q4_k_m.gguf"
PHI4_SHA256="0000000000000000000000000000000000000000000000000000000000000000"  # TODO: add real hash

LLAMA_URL="https://huggingface.co/bartowski/Llama-3.2-8B-Instruct-GGUF/resolve/main/Llama-3.2-8B-Instruct-Q4_K_M.gguf"
LLAMA_FILE="llama-3.2-8b-q4_k_m.gguf"
LLAMA_SHA256="0000000000000000000000000000000000000000000000000000000000000000"  # TODO: add real hash

mkdir -p "$DOWNLOAD_DIR"

download_model() {
    local url="$1"
    local file="$2"
    local sha256="$3"
    local name="$4"

    if [[ -f "$DOWNLOAD_DIR/$file" ]]; then
        echo "✓ $name already downloaded at $DOWNLOAD_DIR/$file"
        return 0
    fi

    echo "↓ Downloading $name ($url)..."
    curl -L -o "$DOWNLOAD_DIR/$file" "$url" --progress-bar

    if command -v sha256sum &>/dev/null && [[ "$sha256" != "0000"* ]]; then
        echo "✓ Verifying checksum..."
        echo "$sha256  $DOWNLOAD_DIR/$file" | sha256sum -c -
    else
        echo "⚠ Checksum verification skipped (no hash configured)"
    fi

    echo "✓ $name downloaded to $DOWNLOAD_DIR/$file"
    echo "  Size: $(du -h "$DOWNLOAD_DIR/$file" | cut -f1)"
}

# Parse args
download_phi4=false
download_llama=false
if [[ $# -eq 0 ]]; then
    download_phi4=true
    download_llama=true
else
    for arg in "$@"; do
        case "$arg" in
            phi4|phi-4) download_phi4=true ;;
            llama|llama3.2|8b) download_llama=true ;;
            *) echo "Unknown model: $arg (use: phi4, llama)"; exit 1 ;;
        esac
    done
fi

if $download_phi4; then
    echo "=== Phi-4 Mini Q4 ==="
    download_model "$PHI4_URL" "$PHI4_FILE" "$PHI4_SHA256" "Phi-4 Mini Q4"
fi

if $download_llama; then
    echo "=== Llama 3.2 8B Q4 ==="
    download_model "$LLAMA_URL" "$LLAMA_FILE" "$LLAMA_SHA256" "Llama 3.2 8B Q4"
fi

echo ""
echo "Done! Models are in: $DOWNLOAD_DIR"
echo "Set MODELS_DIR=$DOWNLOAD_DIR in your environment or pass to the app."
