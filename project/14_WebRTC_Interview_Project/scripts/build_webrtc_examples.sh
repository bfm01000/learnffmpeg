#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
WEBRTC_SRC="$PROJECT_DIR/third_party/webrtc-checkout/src"
LOG_DIR="/home/bfm01000/workspace/third_party"

source "$PROJECT_DIR/scripts/use_webrtc_proxy.sh"
cd "$WEBRTC_SRC"

gn gen out/Default --args='is_debug=true rtc_include_tests=false treat_warnings_as_errors=false rtc_include_pulse_audio=false' 2>&1 | tee "$LOG_DIR/webrtc-gn-gen-default.log"
autoninja -C out/Default peerconnection_server 2>&1 | tee "$LOG_DIR/webrtc-build-peerconnection-server.log"
autoninja -C out/Default peerconnection_client 2>&1 | tee "$LOG_DIR/webrtc-build-peerconnection-client.log"

ls -lh out/Default/peerconnection_server out/Default/peerconnection_client