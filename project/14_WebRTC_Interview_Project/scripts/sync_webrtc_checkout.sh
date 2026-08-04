#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
CHECKOUT_DIR="$PROJECT_DIR/third_party/webrtc-checkout"
LOG_DIR="/home/bfm01000/workspace/third_party"

source "$PROJECT_DIR/scripts/use_webrtc_proxy.sh"
mkdir -p "$CHECKOUT_DIR"
cd "$CHECKOUT_DIR"

if [[ ! -d src/.git ]]; then
  fetch --nohooks --no-history webrtc 2>&1 | tee "$LOG_DIR/webrtc-fetch-no-history.log"
fi

GCLIENT_SUPPRESS_GIT_VERSION_WARNING=1 gclient sync --nohooks --no-history 2>&1 | tee "$LOG_DIR/webrtc-gclient-sync-nohooks.log"
GCLIENT_SUPPRESS_GIT_VERSION_WARNING=1 gclient runhooks 2>&1 | tee "$LOG_DIR/webrtc-gclient-runhooks.log"