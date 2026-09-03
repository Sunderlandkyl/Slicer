#!/usr/bin/env bash
#
# Helper script used by the GitHub Actions workflows building Slicer.
#
# All paths are kept *fixed* per platform (see `default_root`) because the
# CMake configuration files exported by a Slicer build tree (SlicerConfig.cmake,
# SlicerTargets.cmake, VTKConfig.cmake, ...) embed absolute paths. Restoring a
# build tree at the same location on another machine keeps it usable, both for
# the later stages of the Slicer build and for building extensions against it.
#
# Layout under the root directory:
#
#   <root>/Slicer        Slicer source tree
#   <root>/SR            Slicer superbuild tree (SR = "Slicer Release")
#   <root>/SR/Slicer-build   Inner build tree (value of Slicer_DIR for extensions)
#   <root>/Qt            Qt installation
#
# The script is written so that it runs unchanged on Linux, macOS and Windows
# (Git Bash).
#
# Usage: slicer-ci.sh <command> [args...]
#
set -euo pipefail

#------------------------------------------------------------------------------
# Platform / paths
#------------------------------------------------------------------------------

detect_platform() {
  case "$(uname -s)" in
    Linux*) echo "linux" ;;
    Darwin*) echo "macos" ;;
    MINGW*|MSYS*|CYGWIN*|Windows*) echo "windows" ;;
    *) echo "unknown" ;;
  esac
}

default_root() {
  case "$1" in
    linux) echo "/home/runner/S" ;;
    macos) echo "/Users/runner/S" ;;
    windows) echo "${SLICER_WINDOWS_ROOT:-C:/S}" ;;
    *) echo "$HOME/S" ;;
  esac
}

SLICER_PLATFORM="${SLICER_PLATFORM:-$(detect_platform)}"
SLICER_ROOT="${SLICER_ROOT:-$(default_root "$SLICER_PLATFORM")}"
SLICER_SOURCE_DIR="${SLICER_SOURCE_DIR:-$SLICER_ROOT/Slicer}"
SLICER_SUPERBUILD_DIR="${SLICER_SUPERBUILD_DIR:-$SLICER_ROOT/SR}"
SLICER_BUILD_DIR="${SLICER_BUILD_DIR:-$SLICER_SUPERBUILD_DIR/Slicer-build}"
SLICER_BUILD_TYPE="${SLICER_BUILD_TYPE:-Release}"
SLICER_OUTPUT_DIR="${SLICER_OUTPUT_DIR:-$SLICER_ROOT/out}"
SLICER_CI_STRIP_OBJECTS="${SLICER_CI_STRIP_OBJECTS:-true}"
nproc_count() {
  if [ -n "${NUMBER_OF_PROCESSORS:-}" ]; then echo "$NUMBER_OF_PROCESSORS"
  elif command -v nproc >/dev/null 2>&1; then nproc
  elif command -v sysctl >/dev/null 2>&1; then sysctl -n hw.logicalcpu 2>/dev/null || echo 4
  else getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4
  fi
}
SLICER_PARALLEL="${SLICER_PARALLEL:-$(nproc_count)}"

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum
  else shasum -a 256
  fi
}

log() { echo "::group::$*"; }
endlog() { echo "::endgroup::"; }
info() { echo "[slicer-ci] $*"; }
die() { echo "::error::$*" >&2; exit 1; }

# Native path (for CMake/Windows tools) -> bash path (for cp/tar in Git Bash)
bash_path() {
  if [ "$SLICER_PLATFORM" = "windows" ] && command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$1"
  else
    echo "$1"
  fi
}

# Run cmake --build on the superbuild tree with the right config/parallelism.
cmake_build() {
  local dir="$1"; shift
  local args=(--build "$dir" --parallel "$SLICER_PARALLEL")
  if [ "$SLICER_PLATFORM" = "windows" ]; then
    args+=(--config "$SLICER_BUILD_TYPE")
  fi
  cmake "${args[@]}" "$@"
}

#------------------------------------------------------------------------------
# Commands
#------------------------------------------------------------------------------

cmd_env() {
  # Print (and export to $GITHUB_ENV when available) the variables used by the
  # other commands, so that workflow steps can use them directly.
  local vars=(SLICER_PLATFORM SLICER_ROOT SLICER_SOURCE_DIR SLICER_SUPERBUILD_DIR SLICER_BUILD_DIR SLICER_BUILD_TYPE SLICER_OUTPUT_DIR)
  for v in "${vars[@]}"; do
    echo "$v=${!v}"
    if [ -n "${GITHUB_ENV:-}" ]; then
      echo "$v=${!v}" >> "$GITHUB_ENV"
    fi
  done
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    {
      echo "platform=$SLICER_PLATFORM"
      echo "root=$SLICER_ROOT"
      echo "source-dir=$SLICER_SOURCE_DIR"
      echo "superbuild-dir=$SLICER_SUPERBUILD_DIR"
      echo "slicer-dir=$SLICER_BUILD_DIR"
      echo "output-dir=$SLICER_OUTPUT_DIR"
    } >> "$GITHUB_OUTPUT"
  fi
  mkdir -p "$(bash_path "$SLICER_ROOT")" "$(bash_path "$SLICER_OUTPUT_DIR")"
}

# Compute a key identifying the set of prerequisites. Any change to the files
# describing the external projects (or to the toolchain) yields a new key and
# therefore a fresh build of the prerequisites.
cmd_deps_key() {
  local src; src="$(bash_path "$SLICER_SOURCE_DIR")"
  [ -d "$src/SuperBuild" ] || die "deps-key: $src does not look like a Slicer source tree"
  (
    cd "$src"
    {
      # External project descriptions
      find SuperBuild -type f | LC_ALL=C sort | while read -r f; do echo "== $f"; cat "$f"; done
      echo "== SuperBuild.cmake"; cat SuperBuild.cmake
      for f in CMake/ExternalProject*.cmake; do echo "== $f"; cat "$f"; done
      # Options and version requirements defined in the top-level CMakeLists.txt
      echo "== CMakeLists.txt (subset)"
      grep -E 'option\(|Slicer_REQUIRED_|_VERSION|CMAKE_CXX_STANDARD|Slicer_VTK_|Slicer_ITK_|Slicer_USE_|Slicer_BUILD_' CMakeLists.txt || true
      # This script (it defines the configure options)
      echo "== slicer-ci.sh"; cat .github/scripts/slicer-ci.sh
      # Toolchain
      echo "platform=$SLICER_PLATFORM"
      echo "runner=${SLICER_RUNNER_IMAGE:-}"
      echo "qt=${SLICER_QT_VERSION:-}"
      echo "build-type=$SLICER_BUILD_TYPE"
      echo "key-version=${SLICER_DEPS_KEY_VERSION:-1}"
    } | tr -d '\r' | sha256 | cut -c1-16
  )
}

# Configure the superbuild tree.
cmd_configure() {
  local sb; sb="$(bash_path "$SLICER_SUPERBUILD_DIR")"
  mkdir -p "$sb"
  local qt_dir="${SLICER_QT_DIR:?SLICER_QT_DIR must be set to the Qt prefix (e.g. .../Qt/5.15.2/gcc_64)}"
  local qt_major="${SLICER_QT_VERSION%%.*}"
  [ -n "$qt_major" ] || qt_major=5

  local args=(
    -S "$SLICER_SOURCE_DIR"
    -B "$SLICER_SUPERBUILD_DIR"
    "-DQt${qt_major}_DIR:PATH=$qt_dir/lib/cmake/Qt${qt_major}"
    "-DCMAKE_PREFIX_PATH:PATH=$qt_dir"
    -DBUILD_TESTING:BOOL=ON
    -DSlicer_BUILD_DOCUMENTATION:BOOL=OFF
    -DSlicer_EP_UPDATE_DISCONNECTED:BOOL=ON
    -DSlicer_USE_GIT_PROTOCOL:BOOL=OFF
  )
  case "$SLICER_PLATFORM" in
    windows)
      args+=(-G "Visual Studio 17 2022" -A x64)
      ;;
    macos)
      args+=(-G Ninja "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE"
             -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++
             "-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${SLICER_MACOS_DEPLOYMENT_TARGET:-14.0}")
      ;;
    linux)
      args+=(-G Ninja "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE"
             -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++)
      ;;
  esac
  if [ -n "${SLICER_COMPILER_LAUNCHER:-}" ]; then
    args+=("-DCMAKE_C_COMPILER_LAUNCHER:STRING=$SLICER_COMPILER_LAUNCHER"
           "-DCMAKE_CXX_COMPILER_LAUNCHER:STRING=$SLICER_COMPILER_LAUNCHER")
  fi
  # Extra options provided by the workflow (space separated -D options)
  if [ -n "${SLICER_EXTRA_CMAKE_OPTIONS:-}" ]; then
    # shellcheck disable=SC2206
    args+=(${SLICER_EXTRA_CMAKE_OPTIONS})
  fi
  log "Configure superbuild"
  info "cmake ${args[*]}"
  cmake "${args[@]}"
  endlog
}

# Build a target of the superbuild tree (e.g. VTK, ITK, Slicer-dependencies, Slicer).
cmd_build() {
  local target="${1:?build: missing target}"
  log "Build target $target"
  cmake_build "$SLICER_SUPERBUILD_DIR" --target "$target"
  endlog
  if command -v sccache >/dev/null 2>&1 && [ -n "${SCCACHE_GHA_ENABLED:-}" ]; then
    sccache --show-stats || true
  fi
}

# Build the package (installer) of the inner build. Prints the package path.
cmd_package() {
  local build; build="$(bash_path "$SLICER_BUILD_DIR")"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  mkdir -p "$out"
  log "Package"
  local logfile="$out/package.log"
  # The package target is provided by the inner build tree. Run it through
  # cmake --build so the multi-config generator picks the right configuration.
  cmake_build "$SLICER_BUILD_DIR" --target package 2>&1 | tee "$logfile"
  endlog
  local pkg
  pkg="$(grep -oE 'CPack: - package: (.*) generated\.' "$logfile" | tail -n1 | sed -E 's/CPack: - package: (.*) generated\./\1/')"
  [ -n "$pkg" ] || die "package: could not find generated package in $logfile"
  info "package: $pkg"
  cp "$(bash_path "$pkg")" "$out/"
  local name; name="$(basename "$pkg")"
  echo "$name" > "$out/PACKAGE_FILE.txt"
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "package=$name" >> "$GITHUB_OUTPUT"
  fi
}

# Remove intermediate files that are not needed to *use* a build tree
# (objects, precompiled headers, debug databases, git metadata of the external
# project sources). This dramatically reduces the size of the archives and is
# safe as long as the corresponding projects are not rebuilt.
cmd_strip() {
  local dir="${1:-$SLICER_SUPERBUILD_DIR}"
  dir="$(bash_path "$dir")"
  [ -d "$dir" ] || die "strip: $dir does not exist"
  if [ "$SLICER_CI_STRIP_OBJECTS" != "true" ]; then
    info "strip: skipped (SLICER_CI_STRIP_OBJECTS=$SLICER_CI_STRIP_OBJECTS)"
    return 0
  fi
  log "Strip intermediate files in $dir"
  local before; before="$(du -sh "$dir" 2>/dev/null | cut -f1 || true)"
  find "$dir" -type f \( \
      -name '*.o' -o -name '*.obj' -o -name '*.ilk' -o -name '*.pdb' -o -name '*.idb' \
      -o -name '*.pch' -o -name '*.gch' -o -name '*.ipch' -o -name '*.tmp' \
    \) -delete
  # Git metadata of the external projects (sources are pinned by tag; the
  # update step is disconnected).
  find "$dir" -mindepth 2 -maxdepth 3 -type d -name '.git' -prune -exec rm -rf {} + 2>/dev/null || true
  local after; after="$(du -sh "$dir" 2>/dev/null | cut -f1 || true)"
  info "strip: $before -> $after"
  endlog
}

# Create a zstd-compressed tar archive of paths relative to SLICER_ROOT.
#   pack <archive> [--exclude PATTERN]... <relative path>...
cmd_pack() {
  local archive="${1:?pack: missing archive}"; shift
  local excludes=()
  local paths=()
  while [ $# -gt 0 ]; do
    case "$1" in
      --exclude) excludes+=(--exclude "$2"); shift 2 ;;
      *) paths+=("$1"); shift ;;
    esac
  done
  [ ${#paths[@]} -gt 0 ] || die "pack: no paths"
  local root; root="$(bash_path "$SLICER_ROOT")"
  mkdir -p "$(dirname "$(bash_path "$archive")")"
  log "Pack ${paths[*]} -> $archive"
  # Note: mtimes are preserved by tar, which is required for the superbuild
  # stamps to remain valid after restore.
  tar -C "$root" "${excludes[@]}" -cf - "${paths[@]}" | zstd -T0 -3 -q -o "$(bash_path "$archive")" --force
  ls -lh "$(bash_path "$archive")"
  endlog
}

# Extract an archive (created with pack, possibly split) into SLICER_ROOT.
cmd_unpack() {
  local archive="${1:?unpack: missing archive}"
  archive="$(bash_path "$archive")"
  local root; root="$(bash_path "$SLICER_ROOT")"
  mkdir -p "$root"
  log "Unpack $archive -> $root"
  if [ ! -f "$archive" ] && ls "$archive".part-* >/dev/null 2>&1; then
    info "joining parts"
    cat "$archive".part-* > "$archive"
    rm -f "$archive".part-*
  fi
  [ -f "$archive" ] || die "unpack: $archive not found"
  zstd -d -q -c "$archive" | tar -C "$root" -xf -
  endlog
}

# Split archives larger than the GitHub release asset limit (2 GiB) into parts.
cmd_split() {
  local archive="${1:?split: missing archive}"
  archive="$(bash_path "$archive")"
  local limit=$((1900 * 1024 * 1024))
  local size
  size="$(wc -c < "$archive")"
  if [ "$size" -gt "$limit" ]; then
    info "splitting $archive ($size bytes)"
    split -b "${limit}" -d -a 2 "$archive" "$archive.part-"
    rm -f "$archive"
  fi
}

# Run the test suite of the inner build.
cmd_test() {
  local build; build="$(bash_path "$SLICER_BUILD_DIR")"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  mkdir -p "$out"
  local args=(
    --test-dir "$SLICER_BUILD_DIR"
    --parallel "${SLICER_CTEST_PARALLEL:-$SLICER_PARALLEL}"
    --timeout "${SLICER_CTEST_TIMEOUT:-900}"
    --output-on-failure
    --no-tests=error
    --output-junit "$SLICER_OUTPUT_DIR/test-results.xml"
  )
  if [ "$SLICER_PLATFORM" = "windows" ]; then
    args+=(--build-config "$SLICER_BUILD_TYPE")
  fi
  if [ -n "${SLICER_CTEST_EXCLUDE:-}" ]; then
    args+=(--exclude-regex "$SLICER_CTEST_EXCLUDE")
  fi
  if [ -n "${SLICER_CTEST_INCLUDE:-}" ]; then
    args+=(--tests-regex "$SLICER_CTEST_INCLUDE")
  fi
  local rc=0
  log "Run tests"
  if [ "$SLICER_PLATFORM" = "linux" ]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    export QTWEBENGINE_DISABLE_SANDBOX=1
    xvfb-run -a -s "-screen 0 1920x1080x24" ctest "${args[@]}" || rc=$?
  else
    ctest "${args[@]}" || rc=$?
  fi
  endlog
  cp -f "$build/Testing/Temporary/LastTest.log" "$out/" 2>/dev/null || true
  cp -f "$build/Testing/Temporary/LastTestsFailed.log" "$out/" 2>/dev/null || true
  if [ -f "$out/LastTestsFailed.log" ]; then
    echo "::warning::Failed tests:"; cat "$out/LastTestsFailed.log"
  fi
  return $rc
}

# Write the manifest describing a platform build (consumed by the
# setup-slicer-build action).
#   manifest <file> key=value...
cmd_manifest() {
  local file="${1:?manifest: missing file}"; shift
  file="$(bash_path "$file")"
  mkdir -p "$(dirname "$file")"
  local cache="$(bash_path "$SLICER_BUILD_DIR")/CMakeCache.txt"
  local version="" revision="" qt=""
  if [ -f "$cache" ]; then
    version="$(grep -E '^Slicer_VERSION_FULL:' "$cache" | cut -d= -f2- || true)"
    revision="$(grep -E '^Slicer_REVISION:' "$cache" | cut -d= -f2- || true)"
  fi
  if [ -z "$version" ] && [ -f "$(bash_path "$SLICER_BUILD_DIR")/SlicerConfigVersion.cmake" ]; then
    version="$(grep -oE 'PACKAGE_VERSION "[^"]+"' "$(bash_path "$SLICER_BUILD_DIR")/SlicerConfigVersion.cmake" | head -n1 | cut -d'"' -f2 || true)"
  fi
  {
    echo "{"
    echo "  \"platform\": \"$SLICER_PLATFORM\","
    echo "  \"root\": \"$SLICER_ROOT\","
    echo "  \"source_dir\": \"$SLICER_SOURCE_DIR\","
    echo "  \"superbuild_dir\": \"$SLICER_SUPERBUILD_DIR\","
    echo "  \"slicer_dir\": \"$SLICER_BUILD_DIR\","
    echo "  \"build_type\": \"$SLICER_BUILD_TYPE\","
    echo "  \"slicer_version\": \"$version\","
    echo "  \"slicer_revision\": \"$revision\","
    echo "  \"date\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    for kv in "$@"; do
      echo "  \"${kv%%=*}\": \"${kv#*=}\","
    done
    echo "  \"manifest_version\": 1"
    echo "}"
  } > "$file"
  cat "$file"
}

# Read a value from a manifest file (no jq dependency).
cmd_manifest_get() {
  local file="${1:?manifest-get: missing file}"; local key="${2:?manifest-get: missing key}"
  grep -E "^  \"$key\": " "$(bash_path "$file")" | sed -E 's/^  "[^"]+": "?([^"]*)"?,?$/\1/'
}

# Free disk space on GitHub-hosted Linux runners.
cmd_free_disk_space() {
  [ "$SLICER_PLATFORM" = "linux" ] || return 0
  log "Free disk space"
  df -h / || true
  sudo rm -rf /usr/share/dotnet /usr/local/lib/android /opt/ghc /usr/local/.ghcup \
    /opt/hostedtoolcache/CodeQL /usr/local/share/powershell /usr/share/swift \
    /usr/local/share/chromium /usr/local/lib/node_modules 2>/dev/null || true
  sudo docker image prune --all --force >/dev/null 2>&1 || true
  sudo apt-get clean >/dev/null 2>&1 || true
  df -h / || true
  endlog
}

# Install the system packages required to build and test Slicer.
cmd_install_system_packages() {
  case "$SLICER_PLATFORM" in
    linux)
      log "Install system packages"
      sudo apt-get update -qq
      sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        build-essential ninja-build patch zstd xvfb mesa-utils \
        libxt-dev libglu1-mesa-dev libgl1-mesa-dev libgl1-mesa-dri libegl1 libopengl0 \
        libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-xfixes0 \
        libnss3 libxss1 libxtst6 libxcomposite1 libxdamage1 libxrandr2 libgbm1 libasound2t64 \
        libfontconfig1 libdbus-1-3 libxcursor1 libxi6 libpulse0 libsm6 libice6 \
        libavcodec-dev libavformat-dev libswscale-dev libgstreamer-plugins-base1.0-0
      endlog
      ;;
    macos)
      log "Install system packages"
      brew install ninja zstd >/dev/null || brew upgrade ninja zstd || true
      endlog
      ;;
    windows)
      log "Install system packages"
      choco install -y --no-progress zstandard >/dev/null
      endlog
      ;;
  esac
}

# Print a disk usage report of the root directory.
cmd_report() {
  local root; root="$(bash_path "$SLICER_ROOT")"
  echo "--- disk ---"
  df -h 2>/dev/null | head -n 10 || true
  echo "--- $root ---"
  du -sh "$root"/* 2>/dev/null || true
  echo "--- largest external projects ---"
  du -sh "$root"/SR/* 2>/dev/null | sort -h | tail -n 20 || true
}

#------------------------------------------------------------------------------
usage() {
  cat <<EOF
Usage: $0 <command> [args]

Commands:
  env                          Print/export the SLICER_* variables (and create the root dir)
  deps-key                     Print the key identifying the prerequisites
  free-disk-space              Remove unneeded tooling from Linux runners
  install-system-packages      Install platform packages
  configure                    Configure the superbuild tree
  build <target>               Build a superbuild target (VTK, ITK, Slicer-dependencies, Slicer)
  package                      Build the package of the inner build
  strip [dir]                  Remove intermediate files from a build tree
  pack <archive> [--exclude P]... <relpath>...
  unpack <archive>
  split <archive>
  test                         Run ctest on the inner build
  manifest <file> [k=v...]     Write a build manifest
  manifest-get <file> <key>    Read a manifest value
  report                       Disk usage report
EOF
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in
    env) cmd_env "$@" ;;
    deps-key) cmd_deps_key "$@" ;;
    free-disk-space) cmd_free_disk_space "$@" ;;
    install-system-packages) cmd_install_system_packages "$@" ;;
    configure) cmd_configure "$@" ;;
    build) cmd_build "$@" ;;
    package) cmd_package "$@" ;;
    strip) cmd_strip "$@" ;;
    pack) cmd_pack "$@" ;;
    unpack) cmd_unpack "$@" ;;
    split) cmd_split "$@" ;;
    test) cmd_test "$@" ;;
    manifest) cmd_manifest "$@" ;;
    manifest-get) cmd_manifest_get "$@" ;;
    report) cmd_report "$@" ;;
    ""|-h|--help|help) usage ;;
    *) die "unknown command: $cmd" ;;
  esac
}

main "$@"
