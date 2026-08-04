#!/usr/bin/env bash
set -u

DEPOT_TOOLS_DIR="/home/bfm01000/workspace/third_party/depot_tools"
if [[ -d "$DEPOT_TOOLS_DIR" ]]; then
  export PATH="$DEPOT_TOOLS_DIR:$PATH"
fi

print_tool() {
  local name="$1"
  local path
  path="$(command -v "$name" 2>/dev/null || true)"
  if [[ -n "$path" ]]; then
    printf '%-12s %s\n' "$name" "$path"
  else
    printf '%-12s %s\n' "$name" "MISSING"
  fi
}

print_version() {
  local label="$1"
  shift
  printf '%-12s ' "$label"
  if command -v "$1" >/dev/null 2>&1; then
    "$@" 2>/dev/null | head -1 || true
  else
    echo "MISSING"
  fi
}

echo "== PATH additions =="
if [[ -d "$DEPOT_TOOLS_DIR" ]]; then
  echo "depot_tools: $DEPOT_TOOLS_DIR"
else
  echo "depot_tools: MISSING"
fi

echo
echo "== required/native tools =="
for tool in git python3 cmake ninja gn fetch gclient node npm clang clang++ gcc g++; do
  print_tool "$tool"
done

echo
echo "== versions =="
print_version git git --version
print_version python3 python3 --version
print_version cmake cmake --version
print_version ninja ninja --version
print_version node node --version
print_version npm npm --version
print_version clang++ clang++ --version
print_version g++ g++ --version

echo
echo "== depot_tools bootstrap files =="
if [[ -d "$DEPOT_TOOLS_DIR" ]]; then
  [[ -x "$DEPOT_TOOLS_DIR/fetch" ]] && echo "fetch script: OK" || echo "fetch script: MISSING"
  [[ -x "$DEPOT_TOOLS_DIR/gclient" ]] && echo "gclient script: OK" || echo "gclient script: MISSING"
  [[ -x "$DEPOT_TOOLS_DIR/.cipd_bin/vpython3" ]] && echo "vpython3 cipd: OK" || echo "vpython3 cipd: MISSING"
fi

echo
echo "== workspace disk =="
df -h /home/bfm01000/workspace | tail -1

echo
echo "== possible local webrtc checkout =="
find /home/bfm01000/workspace -maxdepth 5 \( -name ".gclient" -o -path "*/webrtc/src" -o -path "*/src/third_party/webrtc" \) -print 2>/dev/null | head -80