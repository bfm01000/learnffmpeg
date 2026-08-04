#!/usr/bin/env bash
# Source this file before running depot_tools commands from WSL:
#   source scripts/use_webrtc_proxy.sh

PROXY_PORT="${PROXY_PORT:-7897}"
WINDOWS_HOST_IP="${WINDOWS_HOST_IP:-$(ip route | awk '/default/ {print $3; exit}')}"

export http_proxy="http://${WINDOWS_HOST_IP}:${PROXY_PORT}"
export https_proxy="http://${WINDOWS_HOST_IP}:${PROXY_PORT}"
export HTTP_PROXY="$http_proxy"
export HTTPS_PROXY="$https_proxy"
export PATH="/home/bfm01000/workspace/third_party/depot_tools:$PATH"

echo "WSL proxy: $http_proxy"
echo "depot_tools PATH: /home/bfm01000/workspace/third_party/depot_tools"