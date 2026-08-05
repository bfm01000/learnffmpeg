#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
INPUT="/home/bfm01000/workspace/video_downloads/FRXXZ.mp4"
FRAMES="${1:-200}"
WIDTH="${2:-320}"
HEIGHT="${3:-180}"
OUTPUT="$PROJECT_DIR/artifacts/frxxz-${WIDTH}x${HEIGHT}-${FRAMES}f.i420"
CSV="$PROJECT_DIR/artifacts/frxxz-${WIDTH}x${HEIGHT}-${FRAMES}f.csv"

mkdir -p "$PROJECT_DIR/artifacts"
"$PROJECT_DIR/native/build/video_file_decode/video_file_decode" \
  --input "$INPUT" \
  --frames "$FRAMES" \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --output "$OUTPUT" \
  --csv "$CSV"

echo "I420 output: $OUTPUT"
echo "CSV output : $CSV"