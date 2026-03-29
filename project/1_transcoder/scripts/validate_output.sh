#!/usr/bin/env bash
set -euo pipefail

INPUT="${1:-}"
OUTPUT="${2:-}"
MAX_DIFF_SEC="${3:-0.08}"

if [[ -z "$INPUT" || -z "$OUTPUT" ]]; then
  echo "Usage: $0 <input> <output> [max_audio_video_duration_diff_sec]"
  exit 2
fi

if ! command -v ffprobe >/dev/null 2>&1; then
  echo "ffprobe not found"
  exit 2
fi

echo "[1/4] checking stream types..."
STREAM_TYPES="$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$OUTPUT" | tr '\n' ' ')"
echo "output stream types: $STREAM_TYPES"

VIDEO_COUNT="$(ffprobe -v error -select_streams v -show_entries stream=index -of csv=p=0 "$OUTPUT" | wc -l | tr -d ' ')"
AUDIO_COUNT="$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$OUTPUT" | wc -l | tr -d ' ')"
SUB_COUNT="$(ffprobe -v error -select_streams s -show_entries stream=index -of csv=p=0 "$OUTPUT" | wc -l | tr -d ' ')"

if [[ "$VIDEO_COUNT" -gt 1 || "$AUDIO_COUNT" -gt 1 ]]; then
  echo "FAIL: expected at most 1 video + 1 audio stream"
  exit 1
fi
if [[ "$SUB_COUNT" -ne 0 ]]; then
  echo "FAIL: expected no subtitle streams"
  exit 1
fi

echo "[2/4] checking container..."
FORMAT_NAME="$(ffprobe -v error -show_entries format=format_name -of default=nk=1:nw=1 "$OUTPUT")"
echo "output format: $FORMAT_NAME"
if [[ "$FORMAT_NAME" != *"mp4"* ]]; then
  echo "FAIL: output is not mp4"
  exit 1
fi

echo "[3/4] checking duration sync..."
V_DUR="$(ffprobe -v error -select_streams v:0 -show_entries stream=duration -of default=nk=1:nw=1 "$OUTPUT" || true)"
A_DUR="$(ffprobe -v error -select_streams a:0 -show_entries stream=duration -of default=nk=1:nw=1 "$OUTPUT" || true)"
echo "video duration: ${V_DUR:-N/A}"
echo "audio duration: ${A_DUR:-N/A}"

if [[ -n "${V_DUR}" && -n "${A_DUR}" ]]; then
  python3 - "$V_DUR" "$A_DUR" "$MAX_DIFF_SEC" <<'PY'
import sys
v = float(sys.argv[1])
a = float(sys.argv[2])
limit = float(sys.argv[3])
d = abs(v - a)
print(f"duration diff: {d:.6f}s (limit={limit}s)")
if d > limit:
    print("FAIL: audio/video duration diff too large")
    sys.exit(1)
PY
fi

echo "[4/4] checking file exists and non-empty..."
if [[ ! -s "$OUTPUT" ]]; then
  echo "FAIL: output file missing or empty"
  exit 1
fi

echo "PASS: basic validation passed."
