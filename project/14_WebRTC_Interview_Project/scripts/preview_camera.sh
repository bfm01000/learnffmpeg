#!/usr/bin/env bash
set -euo pipefail

DEVICE="${1:-/dev/video0}"
SIZE="${2:-640x480}"
FORMAT="${3:-mjpeg}"

if [[ ! -e "$DEVICE" ]]; then
  echo "Camera device not found: $DEVICE"
  echo "Try: ls -l /dev/video*"
  exit 1
fi

if ! command -v ffplay >/dev/null 2>&1; then
  echo "ffplay is not installed or not in PATH."
  echo "You can still capture one frame with:"
  echo "  ffmpeg -y -f v4l2 -input_format $FORMAT -video_size $SIZE -i $DEVICE -frames:v 1 /tmp/wsl-camera-test.jpg"
  exit 1
fi

echo "Opening camera preview..."
echo "Device : $DEVICE"
echo "Size   : $SIZE"
echo "Format : $FORMAT"
echo
echo "Close the preview window or press q in the ffplay window to exit."

ffplay \
  -hide_banner \
  -loglevel warning \
  -f v4l2 \
  -input_format "$FORMAT" \
  -video_size "$SIZE" \
  "$DEVICE"
