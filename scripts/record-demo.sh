#!/usr/bin/env bash
#
# Record a TIMBRE demo: screen + app audio, into a single H.264/AAC mp4.
#
#   ./record-demo.sh [output.mp4]
#
# Stop the recording with `q` (NOT ctrl-C — q lets ffmpeg finalise the
# moov atom, ctrl-C can leave an unplayable file).
#
# Prereqs (one-time, see the notes at the bottom):
#   • brew install blackhole-2ch
#   • a Multi-Output Device pairing BlackHole with your speakers
#   • Screen Recording + Microphone permission for this terminal
#
# Env overrides:
#   AUDIO_DEV   audio input name to record   (default: "BlackHole 2ch")
#   SCALE       output height, -1 for native (default: 1080)
#   FPS         capture framerate            (default: 30)
#   DURATION    auto-stop after N seconds    (default: unset = until q)
set -euo pipefail

OUT="${1:-timbre-demo.mp4}"
AUDIO_DEV="${AUDIO_DEV:-BlackHole 2ch}"
SCALE="${SCALE:-1080}"
FPS="${FPS:-30}"
DURATION="${DURATION:-}"

devices="$(ffmpeg -f avfoundation -list_devices true -i "" 2>&1 || true)"

# The device list prints video devices first, then audio devices, each
# numbered from 0 in its own section — so we must scope the search to the
# right section or "[3]" from the video list shadows audio index 3.
video_section="$(sed -n '/AVFoundation video devices/,/AVFoundation audio devices/p' <<<"$devices")"
audio_section="$(sed -n '/AVFoundation audio devices/,$p' <<<"$devices")"

screen_idx="$(grep -o '\[\([0-9]\+\)\] Capture screen 0' <<<"$video_section" | grep -o '[0-9]\+' | head -1 || true)"
audio_idx="$(grep -F "$AUDIO_DEV" <<<"$audio_section" | grep -o '\[[0-9]\+\] ' | grep -o '[0-9]\+' | head -1 || true)"

if [[ -z "$screen_idx" ]]; then
    echo "error: no 'Capture screen 0' device found." >&2
    echo "Grant this terminal Screen Recording permission, then restart it." >&2
    exit 1
fi

if [[ -z "$audio_idx" ]]; then
    echo "error: audio device '$AUDIO_DEV' not found. Available:" >&2
    grep -E '\[[0-9]+\] ' <<<"$audio_section" >&2
    echo >&2
    echo "Install it with:  brew install blackhole-2ch" >&2
    echo "Or record a different device with:  AUDIO_DEV='Some Device' $0 $OUT" >&2
    exit 1
fi

echo "screen : [$screen_idx] Capture screen 0"
echo "audio  : [$audio_idx] $AUDIO_DEV"
echo "output : $OUT"

# An explicit duration keeps a demo honest about its length and means the
# file finalises itself — no reliance on remembering to press q.
limit=()
if [[ -n "$DURATION" ]]; then
    limit=(-t "$DURATION")
    echo "length : ${DURATION}s (auto-stop)"
fi

echo
echo "Recording. Press q to stop early."
echo

# -capture_cursor 1  : include the pointer, so clicks are followable
# -pix_fmt uyvy422   : the native avfoundation screen format; avoids a
#                      conversion warning on capture
# scale=-2:$SCALE    : -2 keeps the width even, which libx264 requires
# +faststart         : moves the index to the front so the file streams
exec ffmpeg -hide_banner \
    -f avfoundation \
    -capture_cursor 1 \
    -framerate "$FPS" \
    -pix_fmt uyvy422 \
    -i "${screen_idx}:${audio_idx}" \
    "${limit[@]}" \
    -c:v libx264 -preset veryfast -crf 20 \
    -vf "scale=-2:${SCALE}" \
    -pix_fmt yuv420p \
    -c:a aac -b:a 256k \
    -movflags +faststart \
    "$OUT"
