#!/usr/bin/env bash
set -u

PROJECT_DIR="/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project"
source "$PROJECT_DIR/scripts/use_webrtc_proxy.sh"

echo
echo "== direct / proxied network checks =="
for url in \
  https://chromium.googlesource.com/chromium/tools/depot_tools.git \
  https://chrome-infra-packages.appspot.com \
  https://commondatastorage.googleapis.com; do
  echo
  echo "-- $url"
  curl -I --max-time 20 "$url" || true
done

echo
echo "== depot_tools commands =="
command -v fetch || true
command -v gclient || true
command -v gn || true