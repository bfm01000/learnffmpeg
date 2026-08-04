#!/usr/bin/env bash
set -euo pipefail

THIRD_PARTY_DIR="/home/bfm01000/workspace/third_party"
DEPOT_TOOLS_DIR="$THIRD_PARTY_DIR/depot_tools"
WEBRTC_CHECKOUT_DIR="$THIRD_PARTY_DIR/webrtc-checkout"

if ! sudo -v; then
  echo "sudo authentication failed. Please run this script from an interactive WSL terminal."
  exit 1
fi

echo "== install system packages =="
sudo apt update
sudo apt install -y ninja-build clang ca-certificates curl git python3

echo "== clone depot_tools =="
mkdir -p "$THIRD_PARTY_DIR"
if [[ -d "$DEPOT_TOOLS_DIR/.git" ]]; then
  git -C "$DEPOT_TOOLS_DIR" pull --ff-only
else
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
fi

echo "== PATH hint =="
echo "export PATH=$DEPOT_TOOLS_DIR:\$PATH"

export PATH="$DEPOT_TOOLS_DIR:$PATH"

echo "== verify depot_tools =="
command -v fetch
command -v gclient

echo "== prepare checkout directory =="
mkdir -p "$WEBRTC_CHECKOUT_DIR"
echo "$WEBRTC_CHECKOUT_DIR"

echo
cat <<MSG
Next manual step, after confirming network/proxy is OK:

  cd $WEBRTC_CHECKOUT_DIR
  fetch --nohooks webrtc
  gclient sync

Do not run fetch inside the project directory. Keep the huge checkout under third_party.
MSG