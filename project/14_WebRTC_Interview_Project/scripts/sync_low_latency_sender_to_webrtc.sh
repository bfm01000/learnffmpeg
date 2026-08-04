#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
WEBRTC_SRC="$PROJECT_DIR/third_party/webrtc-checkout/src"
TARGET_DIR="$WEBRTC_SRC/examples/low_latency_sender"
EXAMPLES_BUILD="$WEBRTC_SRC/examples/BUILD.gn"
TARGET_LINE='    "low_latency_sender:low_latency_sender",'

if [[ ! -d "$WEBRTC_SRC" ]]; then
  echo "WebRTC checkout not found: $WEBRTC_SRC" >&2
  echo "Run scripts/sync_webrtc_checkout.sh first." >&2
  exit 1
fi

mkdir -p "$TARGET_DIR"
cp "$PROJECT_DIR/native/libwebrtc_sender/"*.cpp "$TARGET_DIR/"
cp "$PROJECT_DIR/native/libwebrtc_sender/"*.h "$TARGET_DIR/"
cp "$PROJECT_DIR/native/libwebrtc_sender/BUILD.gn" "$TARGET_DIR/BUILD.gn"

if ! grep -Fq 'low_latency_sender:low_latency_sender' "$EXAMPLES_BUILD"; then
  python3 - "$EXAMPLES_BUILD" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
anchor = '    "peerconnection/server:peerconnection_server",\n'
insert = '    "low_latency_sender:low_latency_sender",\n'
if anchor not in text:
    raise SystemExit("Cannot find peerconnection_server anchor in examples/BUILD.gn")
path.write_text(text.replace(anchor, anchor + insert, 1))
PY
fi

echo "Synced low_latency_sender into $TARGET_DIR"