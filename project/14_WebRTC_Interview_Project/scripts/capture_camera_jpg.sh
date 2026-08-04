#!/usr/bin/env bash
set -euo pipefail

DEVICE="${1:-/dev/video0}"
OUTPUT="${2:-captures/camera-test.jpg}"
SIZE="${3:-640x480}"
FORMAT="${4:-mjpeg}"

if [[ ! -e "$DEVICE" ]]; then
  echo "Camera device not found: $DEVICE"
  echo "Try: ls -l /dev/video*"
  exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is not installed or not in PATH."
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

echo "Capturing one frame..."
echo "Device : $DEVICE"
echo "Output : $OUTPUT"
echo "Size   : $SIZE"
echo "Format : $FORMAT"

ffmpeg \
  -hide_banner \
  -y \
  -loglevel error \
  -f v4l2 \
  -input_format "$FORMAT" \
  -video_size "$SIZE" \
  -i "$DEVICE" \
  -frames:v 1 \
  "$OUTPUT"

echo "Saved: $OUTPUT"
