#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
WEBRTC_SRC="$PROJECT_DIR/third_party/webrtc-checkout/src"
ROOM="${1:-lab}"
WIDTH="${2:-320}"
HEIGHT="${3:-180}"
FPS="${4:-25}"
FRAMES="${5:-200}"
I420_FILE="$PROJECT_DIR/artifacts/frxxz-${WIDTH}x${HEIGHT}-${FRAMES}f.i420"
LOG_FILE="/tmp/lowlatency-native-sender.log"

if [[ ! -f "$I420_FILE" ]]; then
  "$PROJECT_DIR/scripts/prepare_frxxz_i420.sh" "$FRAMES" "$WIDTH" "$HEIGHT"
fi

if ! ss -ltn | grep -q ':3000'; then
  setsid node "$PROJECT_DIR/server.js" > /tmp/lowlatency-webrtc-lab.log 2>&1 < /dev/null &
  sleep 1
fi

pkill -f 'out/Default/low_latency_sender' 2>/dev/null || true
cd "$WEBRTC_SRC"
setsid ./out/Default/low_latency_sender \
  --host 127.0.0.1 \
  --port 3000 \
  --room "$ROOM" \
  --source i420 \
  --file "$I420_FILE" \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --fps "$FPS" \
  > "$LOG_FILE" 2>&1 < /dev/null &

sleep 2
pgrep -af 'low_latency_sender --host' || true
grep -E 'I420 file source|low_latency_sender joined|signaling hello|joined room|i420 frames sent|failed|Unsupported' "$LOG_FILE" || true