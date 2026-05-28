#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AAR_PATH="${1:-"$SCRIPT_DIR/bmgmedia-release.aar"}"
BUILD_ROOT="${2:-}"

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

find_python() {
  local candidates=(
    python3
    python
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

print_static_link_deps_from_build() {
  local abi="$1"
  local soname="$2"

  if [[ -z "$BUILD_ROOT" ]]; then
    print_item "$YELLOW" "static" "No build root available; pass it as the second argument to parse CMake link inputs."
    return 0
  fi

  if [[ -z "$PYTHON_TOOL" ]]; then
    print_item "$YELLOW" "static" "python3/python not found; cannot parse local CMake build.ninja files."
    return 0
  fi

  local output
  if ! output="$("$PYTHON_TOOL" - "$BUILD_ROOT" "$abi" "$soname" <<'PY'
from pathlib import Path
import shlex
import sys

root = Path(sys.argv[1])
abi = sys.argv[2]
soname = sys.argv[3]

system_libs = {
    "android", "atomic", "c", "dl", "EGL", "GLESv1_CM", "GLESv2", "GLESv3",
    "jnigraphics", "log", "m", "mediandk", "OpenSLES", "pthread", "z",
}

search_roots = [
    root / ".cxx",
    root / "build" / "intermediates" / "cxx",
]

ninja_files = []
for search_root in search_roots:
    if search_root.exists():
        ninja_files.extend(search_root.rglob("build.ninja"))

ninja_files = sorted(set(ninja_files), key=lambda path: path.stat().st_mtime, reverse=True)

def split_tokens(value):
    if not value:
        return []
    return shlex.split(value)

def resolve_path(build_dir, value):
    path = Path(value)
    if not path.is_absolute():
        path = build_dir / path
    return str(path.resolve())

def resolve_l_flag(build_dir, link_dirs, token):
    name = token[2:]
    if name in system_libs:
        return None

    for link_dir in link_dirs:
        for suffix in (".a", ".so"):
            candidate = link_dir / f"lib{name}{suffix}"
            if candidate.exists():
                return str(candidate.resolve())

    return f"UNRESOLVED\t{token}"

def parse_stanza(lines, index):
    variables = {}
    build_line = lines[index]
    j = index + 1
    while j < len(lines):
        line = lines[j]
        if not line:
            break
        if not line.startswith("  "):
            break
        stripped = line.strip()
        if " = " in stripped:
            key, value = stripped.split(" = ", 1)
            variables[key] = value
        j += 1
    return build_line, variables

for ninja_file in ninja_files:
    build_dir = ninja_file.parent
    try:
        lines = ninja_file.read_text(errors="ignore").splitlines()
    except OSError:
        continue

    for index, line in enumerate(lines):
        if not line.startswith("build "):
            continue
        if soname not in line:
            continue
        if f"/{abi}/" not in line and f"/{abi}:" not in line:
            continue

        build_line, variables = parse_stanza(lines, index)
        if variables.get("SONAME") not in ("", soname):
            continue

        link_dirs = []
        for token in split_tokens(variables.get("LINK_PATH", "")):
            if token.startswith("-L") and len(token) > 2:
                path = Path(token[2:])
                if not path.is_absolute():
                    path = build_dir / path
                link_dirs.append(path.resolve())

        static_libs = []
        dynamic_libs = []
        unresolved = []

        for token in split_tokens(variables.get("LINK_LIBRARIES", "")):
            if token in {"||", "-Wl,--whole-archive", "-Wl,--no-whole-archive"}:
                continue
            if token.startswith("-Wl,") or token.startswith("-pthread"):
                continue
            if token.endswith(".a"):
                static_libs.append(resolve_path(build_dir, token))
                continue
            if token.endswith(".so"):
                dynamic_libs.append(resolve_path(build_dir, token))
                continue
            if token.startswith("-l") and len(token) > 2:
                resolved = resolve_l_flag(build_dir, link_dirs, token)
                if not resolved:
                    continue
                if resolved.startswith("UNRESOLVED\t"):
                    unresolved.append(resolved.split("\t", 1)[1])
                elif resolved.endswith(".a"):
                    static_libs.append(resolved)
                elif resolved.endswith(".so"):
                    dynamic_libs.append(resolved)

        print(f"RULE\t{ninja_file}")

        seen = set()
        for value in static_libs:
            if value not in seen:
                print(f"STATIC\t{value}")
                seen.add(value)

        seen = set()
        for value in dynamic_libs:
            if value not in seen:
                print(f"LINK_DYNAMIC\t{value}")
                seen.add(value)

        seen = set()
        for value in unresolved:
            if value not in seen:
                print(f"UNRESOLVED\t{value}")
                seen.add(value)

        if not static_libs:
            print("NO_STATIC\tNo .a inputs found in the local link rule.")
        raise SystemExit(0)

print("NO_RULE\tNo matching local CMake/Ninja link rule found for this ABI and soname.")
PY
)"; then
    print_item "$YELLOW" "static" "Failed to parse local CMake/Ninja link inputs."
    return 0
  fi

  while IFS=$'\t' read -r kind value; do
    case "$kind" in
      RULE)
        print_item "$YELLOW" "link-rule" "$value"
        ;;
      STATIC)
        print_item "$YELLOW" "static" "$value"
        ;;
      LINK_DYNAMIC)
        print_item "$BLUE" "link-so" "$value"
        ;;
      UNRESOLVED)
        print_item "$YELLOW" "unresolved" "$value (could not resolve from LINK_PATH; may be toolchain/system or missing build dir)"
        ;;
      NO_STATIC|NO_RULE)
        print_item "$YELLOW" "static" "$value"
        ;;
    esac
  done <<<"$output"
}

if [[ ! -f "$AAR_PATH" ]]; then
  die "AAR not found: $AAR_PATH"
fi

AAR_ABS="$(cd "$(dirname "$AAR_PATH")" && pwd)/$(basename "$AAR_PATH")"
if [[ -z "$BUILD_ROOT" && "$AAR_ABS" == */build/outputs/aar/* ]]; then
  BUILD_ROOT="${AAR_ABS%%/build/outputs/aar/*}"
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

PYTHON_TOOL=''
if PYTHON_TOOL="$(find_python)"; then
  :
else
  printf '%sWARN:%s python3/python not found; local CMake/Ninja static link inputs cannot be parsed.\n' "$YELLOW" "$RESET" >&2
fi

section "AAR"
printf '  %s\n' "$AAR_PATH"
if [[ -n "$BUILD_ROOT" ]]; then
  print_item "$CYAN" "build-root" "$BUILD_ROOT"
fi

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

section "Per-Library Dependencies"
DYNAMIC_PACKAGED_DEPS=()
DYNAMIC_SYSTEM_DEPS=()
DYNAMIC_MISSING_DEPS=()
if ((${#SHARED_LIBS[@]} == 0 && ${#STATIC_LIBS[@]} == 0)); then
  print_item "$YELLOW" "none" "No native libraries were packaged in this AAR."
else
  for lib in "${SHARED_LIBS[@]}"; do
    rel="${lib#"$TMP_DIR"/}"
    abi="$(basename "$(dirname "$lib")")"
    printf '\n  %s%s%s\n' "$CYAN" "$rel" "$RESET"

    print_item "$BLUE" "dynamic" "DT_NEEDED entries:"
    if [[ -z "$READELF_TOOL" ]]; then
      print_item "$YELLOW" "skipped" "Install llvm-readelf/readelf or Android NDK binutils to parse DT_NEEDED."
    else
      needed=()
      while IFS= read -r dep; do
        needed+=("$dep")
      done < <("$READELF_TOOL" -d "$lib" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p' | sort -u)

      if ((${#needed[@]} == 0)); then
        print_item "$YELLOW" "needed" "No DT_NEEDED entries found."
      else
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
      fi
    fi

    print_item "$YELLOW" "static" "Static link inputs from local CMake/Ninja build:"
    print_static_link_deps_from_build "$abi" "$(basename "$lib")"
  done

  if ((${#STATIC_LIBS[@]} > 0)); then
    for lib in "${STATIC_LIBS[@]}"; do
      rel="${lib#"$TMP_DIR"/}"
      printf '\n  %s%s%s\n' "$CYAN" "$rel" "$RESET"
      print_item "$BLUE" "dynamic" "Not applicable: static archives do not carry runtime DT_NEEDED entries."

      if [[ "$lib" == *.a ]]; then
        print_item "$YELLOW" "static" "Archive members:"
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
print_item "$YELLOW" "static" "For locally built .so files, static inputs are parsed from CMake/Ninja LINK_LIBRARIES."
print_item "$YELLOW" "member" "Static archive members are object files, not source library dependency names."
print_item "$BLUE" "needed" "DT_NEEDED entries are dynamic runtime dependencies of each .so."
print_item "$GREEN" "system" "Android system libraries are expected to be absent from the AAR."
print_item "$RED" "missing" "A missing dependency may be provided by Android system, app, or another AAR."
printf '  %s\n' "Already statically linked third-party libraries usually cannot be recovered exactly from a final .so."
