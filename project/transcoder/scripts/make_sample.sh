#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-sample_input.mkv}"

ffmpeg -y \
  -f lavfi -i testsrc=size=1280x720:rate=30 \
  -f lavfi -i sine=frequency=1000:sample_rate=48000 \
  -f lavfi -i color=size=320x80:rate=1:color=black \
  -vf "drawtext=text='demo':fontcolor=white:fontsize=40:x=(w-text_w)/2:y=(h-text_h)/2" \
  -map 0:v:0 -map 1:a:0 -map 2:v:0 \
  -t 8 \
  -c:v libx264 -pix_fmt yuv420p \
  -c:a aac -b:a 128k \
  -c:v:1 libx264 -pix_fmt yuv420p \
  "$OUT"

echo "Generated sample: $OUT"
