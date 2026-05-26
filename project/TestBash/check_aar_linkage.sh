#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AAR_PATH="${1:-"$SCRIPT_DIR/bmgmedia-release.aar"}"

if [[ -t 1 ]]; then
  RED=$'\033[31m'
  GREEN=$'\033[32m'
  YELLOW=$'\033[33m'
  BLUE=$'\033[34m'
  CYAN=$'\033[36m'
  BOLD=$'\033[1m'
  RESET=$'\033[0m'
else
  RED=''
  GREEN=''
  YELLOW=''
  BLUE=''
  CYAN=''
  BOLD=''
  RESET=''
fi

die() {
  printf '%sERROR:%s %s\n' "$RED" "$RESET" "$*" >&2
  exit 1
}

find_readelf() {
  local candidates=(
    llvm-readelf
    readelf
    aarch64-linux-android-readelf
    arm-linux-androideabi-readelf
    x86_64-linux-android-readelf
  )

  local tool
  for tool in "${candidates[@]}"; do
    if command -v "$tool" >/dev/null 2>&1; then
      printf '%s\n' "$tool"
      return 0
    fi
  done

  return 1
}

find_ar() {
  local candidates=(
    llvm-ar
    ar
  )

  local tool
  for tool in "${candidates[@]}"; do
    if command -v "$tool" >/dev/null 2>&1; then
      printf '%s\n' "$tool"
      return 0
    fi
  done

  return 1
}

section() {
  printf '\n%s%s%s\n' "$BOLD" "$1" "$RESET"
}

print_item() {
  local color="$1"
  local label="$2"
  local value="$3"
  printf '  %s%-12s%s %s\n' "$color" "$label" "$RESET" "$value"
}

is_packaged_so() {
  local target="$1"
  local item
  for item in "${PACKAGED_SO_KEYS[@]}"; do
    if [[ "$item" == "$target" ]]; then
      return 0
    fi
  done
  return 1
}

is_android_system_lib() {
  case "$1" in
    libandroid.so|libc.so|libdl.so|libEGL.so|libGLESv1_CM.so|libGLESv2.so|libGLESv3.so|\
libjnigraphics.so|liblog.so|libm.so|libmediandk.so|libOpenSLES.so|libz.so)
      return 0
      ;;
  esac
  return 1
}

add_unique_packaged_dynamic_dep() {
  local value="$1"
  local item
  local found=0
  set +u
  for item in "${DYNAMIC_PACKAGED_DEPS[@]}"; do
    if [[ "$item" == "$value" ]]; then
      found=1
      break
    fi
  done
  set -u
  if ((found == 0)); then
    DYNAMIC_PACKAGED_DEPS+=("$value")
  fi
}

add_unique_system_dynamic_dep() {
  local value="$1"
  local item
  local found=0
  set +u
  for item in "${DYNAMIC_SYSTEM_DEPS[@]}"; do
    if [[ "$item" == "$value" ]]; then
      found=1
      break
    fi
  done
  set -u
  if ((found == 0)); then
    DYNAMIC_SYSTEM_DEPS+=("$value")
  fi
}

add_unique_missing_dynamic_dep() {
  local value="$1"
  local item
  local found=0
  set +u
  for item in "${DYNAMIC_MISSING_DEPS[@]}"; do
    if [[ "$item" == "$value" ]]; then
      found=1
      break
    fi
  done
  set -u
  if ((found == 0)); then
    DYNAMIC_MISSING_DEPS+=("$value")
  fi
}

if [[ ! -f "$AAR_PATH" ]]; then
  die "AAR not found: $AAR_PATH"
fi

if ! command -v unzip >/dev/null 2>&1; then
  die "unzip is required"
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/aar-linkage.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

unzip -q "$AAR_PATH" -d "$TMP_DIR"

READELF_TOOL=''
if READELF_TOOL="$(find_readelf)"; then
  :
else
  printf '%sWARN:%s readelf/llvm-readelf not found; .so DT_NEEDED dependencies cannot be parsed.\n' "$YELLOW" "$RESET" >&2
fi

AR_TOOL=''
if AR_TOOL="$(find_ar)"; then
  :
else
  printf '%sWARN:%s ar/llvm-ar not found; .a archive members cannot be listed.\n' "$YELLOW" "$RESET" >&2
fi

section "AAR"
printf '  %s\n' "$AAR_PATH"

STATIC_LIBS=()
while IFS= read -r lib; do
  STATIC_LIBS+=("$lib")
done < <(find "$TMP_DIR" -type f \( -name '*.a' -o -name '*.lo' \) | sort)

SHARED_LIBS=()
while IFS= read -r lib; do
  SHARED_LIBS+=("$lib")
done < <(find "$TMP_DIR" -type f -name '*.so' | sort)

section "Static Libraries In AAR"
if ((${#STATIC_LIBS[@]} == 0)); then
  print_item "$YELLOW" "none" "No .a/.lo files were packaged in this AAR."
else
  for lib in "${STATIC_LIBS[@]}"; do
    rel="${lib#"$TMP_DIR"/}"
    print_item "$YELLOW" "static" "$rel"
  done
fi

section "Static Dependencies"
if ((${#STATIC_LIBS[@]} == 0)); then
  print_item "$YELLOW" "none" "No packaged static dependency libraries (.a/.lo) found."
else
  for lib in "${STATIC_LIBS[@]}"; do
    rel="${lib#"$TMP_DIR"/}"
    printf '\n  %s%s%s\n' "$CYAN" "$rel" "$RESET"

    if [[ "$lib" == *.a ]]; then
      if [[ -z "$AR_TOOL" ]]; then
        print_item "$YELLOW" "skipped" "Install ar/llvm-ar to list archive members."
        continue
      fi

      members=()
      while IFS= read -r member; do
        members+=("$member")
      done < <("$AR_TOOL" -t "$lib" 2>/dev/null | sort -u)

      if ((${#members[@]} == 0)); then
        print_item "$YELLOW" "member" "No archive members found."
      else
        for member in "${members[@]}"; do
          print_item "$YELLOW" "member" "$member"
        done
      fi
    else
      print_item "$YELLOW" "static" "Libtool object file; exact library dependencies cannot be parsed reliably."
    fi
  done
fi

section "Dynamic Libraries In AAR"
PACKAGED_SO_KEYS=()
if ((${#SHARED_LIBS[@]} == 0)); then
  print_item "$RED" "none" "No .so files were packaged in this AAR."
else
  for lib in "${SHARED_LIBS[@]}"; do
    rel="${lib#"$TMP_DIR"/}"
    abi="$(basename "$(dirname "$lib")")"
    soname="$(basename "$lib")"
    PACKAGED_SO_KEYS+=("$abi/$soname")
    print_item "$GREEN" "dynamic" "$rel"
  done
fi

section "Dynamic Dependencies"
DYNAMIC_PACKAGED_DEPS=()
DYNAMIC_SYSTEM_DEPS=()
DYNAMIC_MISSING_DEPS=()
if [[ -z "$READELF_TOOL" ]]; then
  print_item "$YELLOW" "skipped" "Install llvm-readelf/readelf or Android NDK binutils to parse DT_NEEDED."
elif ((${#SHARED_LIBS[@]} == 0)); then
  print_item "$YELLOW" "skipped" "No .so files to inspect."
else
  for lib in "${SHARED_LIBS[@]}"; do
    rel="${lib#"$TMP_DIR"/}"
    abi="$(basename "$(dirname "$lib")")"
    printf '\n  %s%s%s\n' "$CYAN" "$rel" "$RESET"

    needed=()
    while IFS= read -r dep; do
      needed+=("$dep")
    done < <("$READELF_TOOL" -d "$lib" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p' | sort -u)

    if ((${#needed[@]} == 0)); then
      print_item "$YELLOW" "needed" "No DT_NEEDED entries found."
      continue
    fi

    for dep in "${needed[@]}"; do
      if is_packaged_so "$abi/$dep"; then
        print_item "$BLUE" "needed" "$dep (packaged for $abi)"
        add_unique_packaged_dynamic_dep "$abi/$dep"
      elif is_android_system_lib "$dep"; then
        print_item "$GREEN" "system" "$dep (provided by Android system for $abi)"
        add_unique_system_dynamic_dep "$abi/$dep"
      else
        print_item "$RED" "missing" "$dep (not packaged in this AAR for $abi)"
        add_unique_missing_dynamic_dep "$abi/$dep"
      fi
    done
  done
fi

section "Dynamic Dependency Summary"
if [[ -z "$READELF_TOOL" ]]; then
  print_item "$YELLOW" "skipped" "Dynamic dependency summary requires readelf/llvm-readelf."
elif ((${#SHARED_LIBS[@]} == 0)); then
  print_item "$YELLOW" "skipped" "No .so files to inspect."
else
  if ((${#DYNAMIC_PACKAGED_DEPS[@]} == 0)); then
    print_item "$YELLOW" "packaged" "none"
  else
    for dep in "${DYNAMIC_PACKAGED_DEPS[@]}"; do
      print_item "$BLUE" "packaged" "$dep"
    done
  fi

  if ((${#DYNAMIC_SYSTEM_DEPS[@]} == 0)); then
    print_item "$YELLOW" "system" "none"
  else
    for dep in "${DYNAMIC_SYSTEM_DEPS[@]}"; do
      print_item "$GREEN" "system" "$dep"
    done
  fi

  if ((${#DYNAMIC_MISSING_DEPS[@]} == 0)); then
    print_item "$GREEN" "missing" "none"
  else
    for dep in "${DYNAMIC_MISSING_DEPS[@]}"; do
      print_item "$RED" "missing" "$dep"
    done
  fi
fi

section "Notes"
print_item "$GREEN" "dynamic" ".so files are dynamic libraries packaged in the AAR."
print_item "$YELLOW" "static" ".a/.lo files are static archives packaged in the AAR."
print_item "$YELLOW" "member" "Static archive members are object files, not runtime DT_NEEDED dependencies."
print_item "$BLUE" "needed" "DT_NEEDED entries are dynamic runtime dependencies of each .so."
print_item "$GREEN" "system" "Android system libraries are expected to be absent from the AAR."
print_item "$RED" "missing" "A missing dependency may be provided by Android system, app, or another AAR."
printf '  %s\n' "Already statically linked third-party code usually cannot be recovered exactly from a stripped .so."
