#!/usr/bin/env bash
#
# Helper script of the GitHub Actions workflows that build Slicer and its
# extensions. Runs unchanged on Linux, macOS and Windows (Git Bash).
#
# Paths are fixed per platform (see `default_root`): the CMake files a Slicer
# build tree exports (SlicerConfig.cmake, ...) embed absolute paths, so a tree
# restored at the same location on another machine stays usable.
#
#   <root>/Slicer            Slicer source tree
#   <root>/SR                Slicer superbuild tree
#   <root>/SR/Slicer-build   Inner build tree (Slicer_DIR for extensions)
#   <root>/Qt                Qt installation
#
# Usage: slicer-ci.sh <command> [args...]
#
set -euo pipefail

detect_platform() {
  case "$(uname -s)" in
    Linux*) echo "linux" ;;
    # Apple silicon is a platform of its own: nothing built for one
    # architecture is usable by the other.
    Darwin*) if [ "$(uname -m)" = "arm64" ]; then echo "macos-arm64"; else echo "macos"; fi ;;
    MINGW*|MSYS*|CYGWIN*|Windows*) echo "windows" ;;
    *) echo "unknown" ;;
  esac
}

default_root() {
  case "$1" in
    linux) echo "/home/runner/S" ;;
    macos|macos-arm64) echo "/Users/runner/S" ;;
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
# Where ExternalData keeps the test data it downloads. The top-level
# CMakeLists.txt reads it from the environment at the first configure.
export ExternalData_OBJECT_STORES="${ExternalData_OBJECT_STORES:-$SLICER_ROOT/ExternalData}"
# On Apple silicon CMake searches Homebrew (/opt/homebrew) by default, so a Qt
# installed there would provide the Qt tools (qmake, moc, ...). Set in the
# environment so that it also applies to every external project, as the Slicer
# factory does: https://github.com/Slicer/Slicer/issues/9484
if [ "$SLICER_PLATFORM" = "macos-arm64" ] && [ -n "${SLICER_QT_DIR:-}" ]; then
  export CMAKE_PREFIX_PATH="$SLICER_QT_DIR"
fi
# The Apple silicon runner has 7 GB of memory for 3 cores: Ninja compiling five
# of the largest files of VTK at once makes it swap. cmake --build, which
# builds each external project, takes its number of jobs from this.
if [ "$SLICER_PLATFORM" = "macos-arm64" ]; then
  export CMAKE_BUILD_PARALLEL_LEVEL="${SLICER_EP_PARALLEL:-2}"
fi
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

# The git a native Windows program can run, as a Windows path. Inside Git Bash
# `git` is Git's internal build, whose location changes between Git releases.
# SlicerConfig.cmake hands the recorded path to every extension, which fails on
# a newer runner image. The launcher in Git's cmd directory stays where it is.
native_git() {
  local candidate
  candidate="$(cygpath -m "${PROGRAMFILES:-C:\\Program Files}" 2>/dev/null)/Git/cmd/git.exe"
  if [ -f "$candidate" ]; then
    echo "$candidate"
    return 0
  fi
  where.exe git 2>/dev/null | tr -d '\r' | grep -i -v -E '\\(ucrt64|mingw64|usr)\\' \
    | head -n 1 | sed 's#\\#/#g'
}

# cmake --build <dir> with the configuration and the number of jobs.
cmake_build() {
  local dir="$1"; shift
  local args=(--build "$dir" --parallel "$SLICER_PARALLEL")
  if [ "$SLICER_PLATFORM" = "windows" ]; then
    args+=(--config "$SLICER_BUILD_TYPE")
  fi
  cmake "${args[@]}" "$@"
}

# Show what CMake recorded about a failed configure, which is where the reason
# usually is rather than in the output above.
dump_cmake_logs() {
  local dir; dir="$(bash_path "$1")"
  local f
  for f in "$dir/CMakeFiles/CMakeConfigureLog.yaml" "$dir/CMakeFiles/CMakeError.log" \
    "$dir/CMakeFiles/CMakeOutput.log"; do
    if [ -f "$f" ]; then
      log "$(basename "$f")"
      tail -n 200 "$f"
      endlog
    fi
  done
}

# Run a program that needs a display and OpenGL. The runners have neither a
# display on Linux nor a GPU anywhere, and Chromium's sandbox keeps
# QtWebEngine's helper processes from reading their resources.
run_headless() {
  export QTWEBENGINE_DISABLE_SANDBOX=1
  case "$SLICER_PLATFORM" in
    linux)
      export LIBGL_ALWAYS_SOFTWARE=1
      xvfb-run -a -s "-screen 0 1920x1080x24" "$@"
      ;;
    windows)
      # Mesa's software rasterizer, rather than a driver that needs a GPU.
      export GALLIUM_DRIVER=llvmpipe
      "$@"
      ;;
    *)
      "$@"
      ;;
  esac
}

# Run ctest in <dir> with the junit report in <output>/<junit>; the remaining
# arguments are passed on.
#   run_tests <title> <dir> <junit> [<ctest argument>...]
run_tests() {
  local title="$1" dir="$2" junit="$3"; shift 3
  mkdir -p "$(bash_path "$SLICER_OUTPUT_DIR")"
  local args=(
    --test-dir "$dir"
    --parallel "${SLICER_CTEST_PARALLEL:-$SLICER_PARALLEL}"
    --timeout "${SLICER_CTEST_TIMEOUT:-900}"
    --output-on-failure
    --output-junit "$SLICER_OUTPUT_DIR/$junit"
    "$@"
  )
  if [ "$SLICER_PLATFORM" = "windows" ]; then
    args+=(--build-config "$SLICER_BUILD_TYPE")
  fi
  if [ -n "${SLICER_CTEST_EXCLUDE:-}" ]; then
    args+=(--exclude-regex "$SLICER_CTEST_EXCLUDE")
  fi
  local rc=0
  log "$title"
  run_headless ctest "${args[@]}" || rc=$?
  endlog
  return $rc
}

# Configure with cmake, inside a log group; on failure, close the group, show
# the CMake logs of <dir> and stop with <message>.
#   cmake_configure <dir> <message> <cmake argument>...
cmake_configure() {
  local dir="$1" message="$2"; shift 2
  info "cmake $*"
  local rc=0
  if [ "$SLICER_PLATFORM" = "windows" ]; then
    # Git Bash rewrites any argument that looks like an absolute POSIX path,
    # which turns a compiler option such as /MP into C:/Program Files/Git/MP.
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' cmake "$@" || rc=$?
  else
    cmake "$@" || rc=$?
  fi
  [ $rc -eq 0 ] || { endlog; dump_cmake_logs "$dir"; die "$message"; }
}

# /MP is requested through the options the build system provides rather than
# through the CL environment variable, which breaks the compiler identification
# of the Visual Studio generator.
MSVC_PARALLEL_ARGS=(
  -DCMAKE_CXX_MP_FLAG:BOOL=ON
  -DADDITIONAL_C_FLAGS:STRING=/MP
  -DADDITIONAL_CXX_FLAGS:STRING=/MP
)

# Print, one per line, the arguments that enable SLICER_COMPILER_LAUNCHER.
compiler_launcher_args() {
  [ -n "${SLICER_COMPILER_LAUNCHER:-}" ] || return 0
  printf '%s\n' "-DCMAKE_C_COMPILER_LAUNCHER:STRING=$SLICER_COMPILER_LAUNCHER" \
    "-DCMAKE_CXX_COMPILER_LAUNCHER:STRING=$SLICER_COMPILER_LAUNCHER"
}

cmd_env() {
  # Print the variables used by the other commands, and export them to
  # $GITHUB_ENV for the workflow steps.
  local vars=(SLICER_PLATFORM SLICER_ROOT SLICER_SOURCE_DIR SLICER_SUPERBUILD_DIR SLICER_BUILD_DIR SLICER_BUILD_TYPE SLICER_OUTPUT_DIR ExternalData_OBJECT_STORES)
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
  mkdir -p "$(bash_path "$SLICER_ROOT")" "$(bash_path "$SLICER_OUTPUT_DIR")" \
    "$(bash_path "$ExternalData_OBJECT_STORES")"
}

# Print the key identifying the set of prerequisites: a change to the files
# describing the external projects, or to the toolchain, yields a new key and a
# fresh build of the prerequisites. The source tree is Slicer itself or a
# custom application built on it (as in the SlicerCustomAppTemplate).
cmd_deps_key() {
  local src; src="$(bash_path "$SLICER_SOURCE_DIR")"
  if [ -f "$src/SuperBuild.cmake" ]; then
    _deps_key_slicer "$src"
  elif [ -f "$src/CMakeLists.txt" ]; then
    _deps_key_application "$src"
  else
    die "deps-key: $src is neither a Slicer source tree nor a custom application"
  fi
}

# Key of a custom application: its top-level CMakeLists.txt, which pins the
# Slicer revision and sets the options, hashed whole, and its SuperBuild/. A
# revision pinned by branch name is not followed: bump the key version to
# rebuild after the branch moves.
_deps_key_application() {
  local src="$1"
  (
    cd "$src"
    {
      echo "== application"
      echo "== CMakeLists.txt"; cat CMakeLists.txt
      if [ -d SuperBuild ]; then
        find SuperBuild -type f | LC_ALL=C sort | while read -r f; do echo "== $f"; cat "$f"; done
      fi
      echo "== configure arguments"
      SLICER_QT_DIR="" configure_args
      echo "platform=$SLICER_PLATFORM"
      echo "runner=${SLICER_RUNNER_IMAGE:-}"
      echo "qt=${SLICER_QT_VERSION:-}"
      echo "key-version=${SLICER_DEPS_KEY_VERSION:-1}"
    } | tr -d '\r' | sha256 | cut -c1-16
  )
}

# Key of Slicer itself. Kept byte for byte as it was when only Slicer was
# built, so that existing prerequisites keep their key.
_deps_key_slicer() {
  local src="$1"
  (
    cd "$src"
    {
      # External project descriptions
      find SuperBuild -type f | LC_ALL=C sort | while read -r f; do echo "== $f"; cat "$f"; done
      echo "== SuperBuild.cmake"; cat SuperBuild.cmake
      _deps_key_toolchain
    } | tr -d '\r' | sha256 | cut -c1-16
  )
}

# What every external project of Slicer is built with.
_deps_key_toolchain() {
  for f in CMake/ExternalProject*.cmake; do echo "== $f"; cat "$f"; done
  # Options and version requirements defined in the top-level CMakeLists.txt
  echo "== CMakeLists.txt (subset)"
  grep -E 'option\(|Slicer_REQUIRED_|_VERSION|CMAKE_CXX_STANDARD|Slicer_VTK_|Slicer_ITK_|Slicer_USE_|Slicer_BUILD_' CMakeLists.txt || true
  # The arguments the superbuild is configured with, rather than this script:
  # other edits to it do not invalidate hours of build, nor does an option
  # added for another platform.
  echo "== configure arguments"
  SLICER_QT_DIR="" configure_args
  echo "platform=$SLICER_PLATFORM"
  echo "runner=${SLICER_RUNNER_IMAGE:-}"
  echo "qt=${SLICER_QT_VERSION:-}"
  echo "key-version=${SLICER_DEPS_KEY_VERSION:-1}"
}

# The prerequisites of Slicer are built in stages: these targets in turn,
# then everything else. The tree each stage leaves is kept on the
# prerequisites release under the key of the stage, and a build starts from
# the tree of the last stage whose key has not changed: a change to an
# external project builds its stage and those after it, on a tree that never
# held them.
SLICER_STAGE_TARGETS=("python DCMTK tbb" "VTK" "ITK" "CTK")

# Print the stages, one per line: the key, then the external projects the
# stage builds (see deps-stage-keys.py). Nothing for a custom application,
# whose prerequisites are built in one piece.
cmd_deps_stages() {
  local src; src="$(bash_path "$SLICER_SOURCE_DIR")"
  [ -f "$src/SuperBuild.cmake" ] || return 0
  local script; script="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/deps-stage-keys.py"
  (cd "$src" && _deps_key_toolchain | python3 "$script" . "${SLICER_STAGE_TARGETS[@]}")
}

# Print the number of the stage (of SLICER_STAGES) that builds an external
# project: nothing if no stage is known to.
stage_of() {
  local name="$1" stage
  while :; do
    stage="$(awk -v p="$name" '{ for (i = 2; i <= NF; i++) if ($i == p) print NR }' <<< "$SLICER_STAGES")"
    [ -z "$stage" ] || { echo "$stage"; return 0; }
    # python-source is of python
    [ "$name" != "${name%-*}" ] || return 0
    name="${name%-*}"
  done
}

# Restore the tree of the last stage whose key the release has, if any.
cmd_restore_stage() {
  local tag="$SLICER_PREREQUISITES_RELEASE" assets key name="" n=0 from=0
  assets="$(gh release view "$tag" -R "$GITHUB_REPOSITORY" --json assets --jq '.assets[].name' 2>/dev/null || true)"
  while read -r key _; do
    n=$((n + 1))
    if grep -qxF "slicer-stage$n-$SLICER_PLATFORM-$key.parts" <<< "$assets"; then
      from=$n name="slicer-stage$n-$SLICER_PLATFORM-$key"
    fi
  done <<< "${SLICER_STAGES:-}"
  if [ "$from" -eq 0 ]; then
    info "the release has no stage to start from"
    return 0
  fi
  info "starting from stage $from ($name)"
  local dir; dir="$(bash_path "$SLICER_OUTPUT_DIR")/restore"
  rm -rf "$dir"; mkdir -p "$dir"
  gh release download "$tag" -R "$GITHUB_REPOSITORY" -D "$dir" --clobber -p "$name.tar.zst*"
  cmd_unpack "$dir/$name.tar.zst"
  rm -rf "$dir"
  echo "SLICER_STAGE_FROM=$from" >> "$GITHUB_ENV"
}

# Build the stages after the one restored, keeping the tree each leaves.
build_stages() {
  local key n=0 project stage archive
  archive="$(bash_path "$SLICER_OUTPUT_DIR")/stage.tar.zst"
  while read -r -u 3 key _; do
    n=$((n + 1))
    [ "$key" != "-" ] || break
    [ "$n" -gt "${SLICER_STAGE_FROM:-0}" ] || continue
    # shellcheck disable=SC2086 - the targets are deliberately word split
    cmd_build ${SLICER_STAGE_TARGETS[n-1]}
    # A project built by an earlier stage than the one whose key covers it
    # would not be built again when it changes. Those of the superbuilds of
    # the projects themselves (CTK-build/...) are not Slicer's.
    while read -r project; do
      stage="$(stage_of "$project")"
      [ -n "$stage" ] && [ "$stage" -le "$n" ] \
        || die "stage $n built $project, of stage ${stage:-none}: see SLICER_STAGE_TARGETS"
    done < <(find "$(bash_path "$SLICER_SUPERBUILD_DIR")" -type f -name "*-done" -not -path "*-build/*" \
               | sed -e 's:.*/::' -e 's:-done$::' | LC_ALL=C sort -u)
    info "stage $n: every project built is of a stage up to it"
    cmd_pack "$archive" SR
    cmd_publish_prerequisites "slicer-stage$n-$SLICER_PLATFORM-$key" "$archive" 2
  done 3<<< "$SLICER_STAGES"
}

# Store a set of prerequisites, or the tree of a stage, on the prerequisites
# release under its key, and remove the sets of the same kind and platform
# but the most recent: they are of external projects nothing builds any more.
#   publish-prerequisites <key> <archive> <number of sets to keep>
cmd_publish_prerequisites() {
  local key="${1:?publish-prerequisites: missing key}" archive="${2:?publish-prerequisites: missing archive}"
  local keep="${3:-3}" tag="$SLICER_PREREQUISITES_RELEASE"
  archive="$(bash_path "$archive")"
  # The release is created on the first run; for Slicer it is the nightly
  # release, whose notes publish-release.sh writes.
  gh release create "$tag" -R "$GITHUB_REPOSITORY" --title "${SLICER_PREREQUISITES_TITLE:-CI prerequisites}" \
    --prerelease --latest=false \
    --notes "Prebuilt prerequisites reused by the build workflow. The slicer-deps-* and slicer-stage* assets are regenerated on demand." \
    2>/dev/null || true
  local upload; upload="$(bash_path "$SLICER_OUTPUT_DIR")/prerequisites"
  rm -rf "$upload"; mkdir -p "$upload"
  mv "$archive" "$upload/$key.tar.zst"
  cmd_split "$upload/$key.tar.zst"
  gh release upload "$tag" -R "$GITHUB_REPOSITORY" "$upload"/* --clobber
  # The list of parts goes last: its presence marks the set as complete.
  (cd "$upload" && ls) > "$upload/$key.parts"
  gh release upload "$tag" -R "$GITHUB_REPOSITORY" "$upload/$key.parts" --clobber
  rm -rf "$upload"

  # A set's list is removed first, so that a half-removed set never counts as
  # built.
  local names list old asset
  names="$(gh release view "$tag" -R "$GITHUB_REPOSITORY" --json assets \
             --jq '.assets[] | "\(.createdAt) \(.name)"')"
  { grep -E " ${key%-*}-[0-9a-f]+\.parts$" <<< "$names" || true; } \
    | sort -r | tail -n +$((keep + 1)) | while read -r _ list; do
        old="${list%.parts}"
        echo "Removing the superseded $old"
        gh release delete-asset "$tag" "$list" -R "$GITHUB_REPOSITORY" --yes || true
        awk '{print $2}' <<< "$names" | { grep -F "$old.tar.zst" || true; } | while read -r asset; do
          gh release delete-asset "$tag" "$asset" -R "$GITHUB_REPOSITORY" --yes || true
        done
      done
}

# Print, one per line, the arguments the superbuild tree is configured with.
# They are part of the prerequisites key.
configure_args() {
  local qt_dir="${SLICER_QT_DIR:-@QT_DIR@}"
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
  )
  case "$SLICER_PLATFORM" in
    windows)
      args+=(-G "Visual Studio 17 2022" -A x64 "${MSVC_PARALLEL_ARGS[@]}")
      ;;
    macos)
      args+=(-G Ninja "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE"
             -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++
             "-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${SLICER_MACOS_DEPLOYMENT_TARGET:-14.0}"
             -DCMAKE_OSX_ARCHITECTURES:STRING=x86_64)
      ;;
    macos-arm64)
      args+=(-G Ninja "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE"
             -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++
             "-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${SLICER_MACOS_DEPLOYMENT_TARGET:-14.0}"
             -DCMAKE_OSX_ARCHITECTURES:STRING=arm64)
      ;;
    *)
      args+=(-G Ninja "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE"
             -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++)
      ;;
  esac
  # Extra options provided by the workflow (space separated -D options)
  if [ -n "${SLICER_EXTRA_CMAKE_OPTIONS:-}" ]; then
    # shellcheck disable=SC2206
    args+=(${SLICER_EXTRA_CMAKE_OPTIONS})
  fi
  printf '%s\n' "${args[@]}"
}

# Configure the superbuild tree.
cmd_configure() {
  mkdir -p "$(bash_path "$SLICER_SUPERBUILD_DIR")"
  : "${SLICER_QT_DIR:?SLICER_QT_DIR must be set to the Qt prefix (e.g. .../Qt/5.15.2/gcc_64)}"
  # A compiler launcher (ccache, sccache) is deliberately not part of
  # `configure_args`: it does not change what is built, so enabling or
  # disabling it must not invalidate prebuilt prerequisites.
  local args=()
  while IFS= read -r line; do args+=("$line"); done < <(configure_args)
  while IFS= read -r line; do args+=("$line"); done < <(compiler_launcher_args)
  log "Configure superbuild"
  cmake_configure "$SLICER_SUPERBUILD_DIR" "configure failed" "${args[@]}"
  endlog
}

# Build targets of the superbuild tree.
cmd_build() {
  [ $# -gt 0 ] || die "build: missing target"
  local targets=("$@")
  if [ "$*" = "Slicer-dependencies" ] && [ -n "${SLICER_STAGES:-}" ]; then
    build_stages
  fi
  # The stamp files say which external projects completed, more reliably than
  # the build output, which carries ANSI escapes and carriage returns.
  _completed_projects() {
    find "$(bash_path "$SLICER_SUPERBUILD_DIR")" -type f -name "*-done" 2>/dev/null \
      | while read -r f; do b="${f##*/}"; echo "${b%-done}"; done \
      | LC_ALL=C sort -u
  }
  local before after built count
  before="$(_completed_projects)"

  log "Build ${targets[*]}"
  # One by one: the Visual Studio generator takes a single target per
  # invocation, and a failure is easier to locate.
  local t
  for t in "${targets[@]}"; do
    echo "[slicer-ci] --- $t ---"
    cmake_build "$SLICER_SUPERBUILD_DIR" --target "$t"
  done
  endlog

  after="$(_completed_projects)"
  built="$(comm -13 <(printf '%s\n' "$before") <(printf '%s\n' "$after") | sed '/^$/d')"
  count="$(printf '%s' "$built" | grep -c . || true)"
  info "external projects completed in this step: ${count}"
  if [ "$count" -gt 0 ]; then
    printf '%s\n' "$built" | sed 's/^/  /'
  fi

  if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
      echo "### ${targets[*]} (${SLICER_PLATFORM})"
      echo ""
      if [ "$count" -gt 0 ]; then
        echo "Completed $count external projects:"
        echo ""
        printf '%s\n' "$built" | sed 's/^/- /'
      else
        echo "Nothing new: every project asked for was already built."
      fi
      echo ""
    } >> "$GITHUB_STEP_SUMMARY"
  fi

  if [ "${SLICER_COMPILER_LAUNCHER:-}" = "ccache" ] && command -v ccache >/dev/null 2>&1; then
    ccache --show-stats || true
  fi
}

# Build the package (installer) of the inner build. Prints the package path.
cmd_package() {
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  mkdir -p "$out"
  log "Package"
  local logfile="$out/package.log"
  # Through cmake --build, so that a multi-config generator picks the right
  # configuration.
  cmake_build "$SLICER_BUILD_DIR" --target package 2>&1 | tee "$logfile"
  endlog
  local pkg
  pkg="$(grep -oE 'CPack: - package: (.*) generated\.' "$logfile" | tail -n1 | sed -E 's/CPack: - package: (.*) generated\./\1/')"
  [ -n "$pkg" ] || die "package: could not find generated package in $logfile"
  info "package: $pkg"
  cp "$(bash_path "$pkg")" "$out/"
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "package=$(basename "$pkg")" >> "$GITHUB_OUTPUT"
  fi
}

# Check that the macOS package is self-contained, with the 'packageverify'
# target. A bundle whose install names were not rewritten otherwise only fails
# on a user's machine, with a dyld "Library not loaded" error.
cmd_verify_package() {
  if [ "${SLICER_PLATFORM%%-*}" != "macos" ]; then
    info "verify-package: nothing to check on $SLICER_PLATFORM"
    return 0
  fi
  log "Verify the application bundle"
  local rc=0
  cmake_build "$SLICER_BUILD_DIR" --target packageverify || rc=$?
  endlog
  if [ $rc -ne 0 ]; then
    die "the application bundle is not self-contained"
  fi
}

# Remove what is not needed to use a build tree: object files and the git
# metadata of the external project sources.
#   strip <dir>
# Only for a tree that will never be built again: a stripped tree recompiles
# every external project from scratch, and without the git metadata the
# external projects that CTK builds itself fail to update.
cmd_strip() {
  local dir; dir="$(bash_path "${1:?strip: missing directory}")"
  [ -d "$dir" ] || die "strip: $dir does not exist"
  log "Strip $dir"
  local before; before="$(du -sh "$dir" 2>/dev/null | cut -f1 || true)"
  find "$dir" -type f \( \
      -name '*.o' -o -name '*.obj' -o -name '*.ilk' -o -name '*.pdb' -o -name '*.idb' \
      -o -name '*.pch' -o -name '*.gch' -o -name '*.ipch' \
    \) -delete
  find "$dir" -type d -name '.git' -prune -exec rm -rf {} + 2>/dev/null || true
  local after; after="$(du -sh "$dir" 2>/dev/null | cut -f1 || true)"
  info "strip: $before -> $after"
  endlog
}

# Create a zstd-compressed tar archive of paths relative to SLICER_ROOT.
#   pack <archive> [--exclude PATTERN]... <relative path>...
# GNU tar and the bsdtar of macOS match exclude patterns against the whole
# stored path: "*/.git" rather than ".git", and "SR/Slicer-build*" rather than
# "SR/Slicer-build".
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
  # tar preserves mtimes, which keeps the superbuild stamps valid after restore.
  # ${a[@]+"${a[@]}"} rather than "${a[@]}": expanding an empty array under
  # `set -u` is an error in the bash 3.2 that macOS ships.
  tar -C "$root" ${excludes[@]+"${excludes[@]}"} -cf - "${paths[@]}"     | zstd -T0 -3 -q -o "$(bash_path "$archive")" --force
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

# Record in a manifest the sha256 of each file published with it, which
# restore-release checks what it downloads against.
#   manifest-files <manifest> <file>...
cmd_manifest_files() {
  local file="${1:?manifest-files: missing manifest}"; shift
  file="$(bash_path "$file")"
  [ -f "$file" ] || die "manifest-files: $file does not exist"
  local entries="" f sum
  for f in "$@"; do
    f="$(bash_path "$f")"
    [ -f "$f" ] || die "manifest-files: $f does not exist"
    sum="$(sha256 < "$f" | cut -d' ' -f1)"
    entries="$entries  \"sha256:$(basename "$f")\": \"$sum\","$'\n'
  done
  local line
  { while IFS= read -r line; do
      case "$line" in
        *'"sha256:'*) continue ;;                       # written again
        *'"manifest_version"'*) printf '%s' "$entries" ;;
      esac
      printf '%s\n' "$line"
    done < "$file"
  } > "$file.new"
  mv "$file.new" "$file"
  cat "$file"
}

# Whether the archives downloaded into <dir> are the ones <manifest> records:
# prints what is wrong, nothing when all is as recorded. A manifest that
# records no files is not checked.
restore_check() {
  local manifest="$1" dir="$2"; shift 2
  local recorded; recorded="$(sed -nE 's/^  "sha256:([^"]+)": "([0-9a-f]+)",?$/\1 \2/p' "$manifest")"
  if [ -z "$recorded" ]; then
    info "the manifest does not record its files: not checked" >&2
    return 0
  fi
  local f name sum got
  # Every archive downloaded is one the manifest records, unchanged...
  for f in "$dir"/*.tar.zst*; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    sum="$(awk -v n="$name" '$1 == n {print $2}' <<< "$recorded")"
    if [ -z "$sum" ]; then
      echo "$name is not a file of the manifest"
      return 0
    fi
    got="$(sha256 < "$f" | cut -d' ' -f1)"
    if [ "$got" != "$sum" ]; then
      echo "$name is not the file the manifest records"
      return 0
    fi
  done
  # ... and every file it records, of the archives that were asked for (the
  # remaining arguments), is there: an archive that is being replaced is, for
  # a moment, not on the release at all.
  local archive found
  for archive in "$@"; do
    found=0
    while read -r name sum; do
      case "$name" in
        "$archive".tar.zst*)
          found=1
          [ -f "$dir/$name" ] || { echo "$name, which the manifest records, was not downloaded"; return 0; } ;;
      esac
    done <<< "$recorded"
    [ "$found" -eq 1 ] || { echo "the manifest records no $archive.tar.zst"; return 0; }
  done
}

# Restore under the build root a release published by the Slicer build
# workflow: the external projects, the source tree and the inner build tree.
#   restore-release <repository> <tag>
# The manifest is left in <output>/restore for the caller.
cmd_restore_release() {
  local repo="${1:?restore-release: missing repository}"
  local tag="${2:?restore-release: missing tag}"
  local dest; dest="$(bash_path "$SLICER_OUTPUT_DIR")/restore"
  mkdir -p "$dest"

  # The external projects first: the inner build tree refers to them.
  local archives=("deps-${SLICER_PLATFORM}" "slicer-source-${SLICER_PLATFORM}" "slicer-build-${SLICER_PLATFORM}")
  local patterns=(-p "manifest-${SLICER_PLATFORM}.json") a
  for a in "${archives[@]}"; do patterns+=(-p "$a.tar.zst*"); done

  # A platform publishes while others build against it (publish-release.sh
  # removes the manifest first and uploads it last), so what is downloaded can
  # be a mix of two builds, or lack the manifest. That is seen from the sha256
  # the manifest records, waited out, and downloaded again.
  local manifest="$dest/manifest-${SLICER_PLATFORM}.json"
  local attempt problem
  for attempt in 1 2 3 4 5; do
    problem=""
    rm -rf "$dest"; mkdir -p "$dest"
    log "Download $tag from $repo"
    gh release download "$tag" -R "$repo" -D "$dest" --clobber "${patterns[@]}" \
      || problem="the download failed"
    ls -la "$dest"
    endlog
    if [ -z "$problem" ] && [ ! -f "$manifest" ]; then
      problem="the release has no manifest-${SLICER_PLATFORM}.json"
    fi
    [ -n "$problem" ] || problem="$(restore_check "$manifest" "$dest" "${archives[@]}")"
    [ -n "$problem" ] || break
    if [ "$attempt" -lt 5 ]; then
      info "$problem: is the platform being published? Trying again in $((attempt * 60)) s"
      sleep $((attempt * 60))
    fi
  done
  [ -z "$problem" ] || die "restore-release: $problem"

  local root; root="$(cmd_manifest_get "$manifest" root)"
  if [ -n "$root" ] && [ "$root" != "$SLICER_ROOT" ]; then
    die "the build was produced at '$root' but this runner uses '$SLICER_ROOT'"
  fi

  for a in "${archives[@]}"; do
    if [ -f "$dest/$a.tar.zst" ] || ls "$dest/$a.tar.zst".part-* >/dev/null 2>&1; then
      cmd_unpack "$dest/$a.tar.zst"
      rm -f "$dest/$a.tar.zst" "$dest/$a.tar.zst".part-* 2>/dev/null || true
    else
      # Never skipped: what would be configured against is not all there.
      die "restore-release: $a.tar.zst is not in the release"
    fi
  done
  cmd_report
}

# Run the test suite of the inner build.
cmd_test() {
  local build; build="$(bash_path "$SLICER_BUILD_DIR")"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  local rc=0
  # Run a failed test once more before reporting it: a few GUI tests now and
  # then fail on the shared runners (a settings file being read while another
  # test writes it, the virtual display).
  run_tests "Run tests" "$SLICER_BUILD_DIR" test-results.xml \
    --no-tests=error --repeat until-pass:2 || rc=$?
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
  fi
  # Slicer_REVISION is computed while configuring rather than cached, so it is
  # read from the configuration Slicer exports, not from CMakeCache.txt.
  revision="$(slicer_config_value Slicer_REVISION)"
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
  log "Install system packages"
  case "$SLICER_PLATFORM" in
    linux)
      sudo apt-get update -qq
      # Packages required to build Slicer.
      sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        build-essential ninja-build patch zstd xvfb \
        libxt-dev libglu1-mesa-dev libgl1-mesa-dri
      # Packages required at run time by Qt and QtWebEngine. Their names vary
      # between Ubuntu releases (libasound2 became libasound2t64,
      # libgl1-mesa-glx was dropped), so only those this release provides.
      local runtime_pkgs=(
        mesa-utils libgl1-mesa-glx libgl1 libegl1 libopengl0
        libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1
        libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-xfixes0
        libnss3 libxss1 libxtst6 libxcomposite1 libxdamage1 libxrandr2 libgbm1
        libasound2 libasound2t64
        libfontconfig1 libdbus-1-3 libxcursor1 libxi6 libpulse0 libsm6 libice6
        libgstreamer-plugins-base1.0-0 libxkbfile1 libodbc1 libpq5
      )
      local available=()
      local pkg
      for pkg in "${runtime_pkgs[@]}"; do
        if apt-cache show "$pkg" >/dev/null 2>&1; then
          available+=("$pkg")
        fi
      done
      info "installing: ${available[*]}"
      sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        ${available[@]+"${available[@]}"}
      ;;
    macos|macos-arm64)
      brew install ninja zstd >/dev/null || brew upgrade ninja zstd || true
      ;;
    windows)
      choco install -y --no-progress zstandard >/dev/null
      ;;
  esac
  endlog
}

# The OpenGL that Windows provides without a GPU is too old for VTK to create a
# render window, so Mesa is installed in its place (on Linux, Mesa already
# renders in software under xvfb). The version is pinned, and its archive
# checked, so the renderer the tests run against does not change.
MESA_WINDOWS_VERSION="26.1.7"
MESA_WINDOWS_SHA256="c6e90c3117233b66f7816df05026a5fb0f88eaf7829bd07a1724b981487ec0bb"

cmd_install_software_opengl() {
  if [ "$SLICER_PLATFORM" != "windows" ]; then
    info "no software OpenGL to install on $SLICER_PLATFORM"
    return 0
  fi
  local name="mesa3d-${MESA_WINDOWS_VERSION}-release-msvc"
  local dir; dir="$(bash_path "${RUNNER_TEMP:-$SLICER_ROOT}")/$name"
  log "Install Mesa ${MESA_WINDOWS_VERSION} software OpenGL"
  curl -fsSL --retry 5 --retry-all-errors --connect-timeout 30 -o "$dir.7z" \
    "https://github.com/pal1000/mesa-dist-win/releases/download/${MESA_WINDOWS_VERSION}/${name}.7z"
  echo "${MESA_WINDOWS_SHA256}  $dir.7z" | sha256sum -c - || die "unexpected checksum for $name.7z"
  rm -rf "$dir"
  7z x -y -o"$dir" "$dir.7z" >/dev/null
  # Choice 1 of the deployment utility installs Mesa as the desktop OpenGL
  # driver. Its other numbered choices differ between Mesa releases, so no other
  # is used.
  ( cd "$dir" && MSYS_NO_PATHCONV=1 cmd.exe /c "systemwidedeploy.cmd 1" ) | tee "$dir.log"
  grep -q "Desktop OpenGL drivers deploy complete" "$dir.log" \
    || die "Mesa did not report deploying its desktop OpenGL drivers"
  rm -rf "$dir" "$dir.7z" "$dir.log"
  endlog
}

# Extensions. An extension builds against the Slicer build tree restored from a
# release (SLICER_BUILD_DIR). It is taken from SLICER_EXT_SOURCE_DIR (default:
# the checkout) and built in SLICER_EXT_BUILD_DIR (default: <root>/ext-build,
# kept short because of the Windows path limit).
#
# With SLICER_EXT_NAME (the CMake project name) it is built in <root>/E/<Name>
# from a copy of its checkout in <root>/ES/<Name>: <Name>Config.cmake records
# absolute paths, so only a tree at fixed paths can be published
# (extension-tree) and restored by its dependents (extension-dependencies).

ext_checkout_dir() { echo "${SLICER_EXT_SOURCE_DIR:-${GITHUB_WORKSPACE:-$PWD}}"; }
# Whether the extension is built at the fixed paths: named, and a whole
# repository (a subdirectory of one has no revision of its own to copy).
ext_relocated() {
  [ -n "${SLICER_EXT_NAME:-}" ] && [ -e "$(bash_path "$(ext_checkout_dir)")/.git" ]
}
ext_source_dir() {
  if ext_relocated; then echo "$SLICER_ROOT/ES/$SLICER_EXT_NAME"; else ext_checkout_dir; fi
}
ext_build_dir() {
  if [ -n "${SLICER_EXT_BUILD_DIR:-}" ]; then echo "$SLICER_EXT_BUILD_DIR"
  elif ext_relocated; then echo "$SLICER_ROOT/E/$SLICER_EXT_NAME"
  else echo "$SLICER_ROOT/ext-build"; fi
}

# An extension that uses a superbuild builds itself in `inner-build`, which is
# where its targets, its tests and its package are; one that does not builds
# everything in the top level tree.
has_inner_build() { [ -f "$(bash_path "$1")/inner-build/CMakeCache.txt" ]; }
ext_inner_dir() {
  local b; b="$(ext_build_dir)"
  if has_inner_build "$b"; then echo "$b/inner-build"; else echo "$b"; fi
}

# A value recorded in the SlicerConfig.cmake of the restored build tree.
slicer_config_value() {
  local config; config="$(bash_path "$SLICER_BUILD_DIR")/SlicerConfig.cmake"
  [ -f "$config" ] || return 0
  sed -n "s/^set($1[[:space:]]*\"\(.*\)\")[[:space:]]*$/\1/p" "$config" | head -n 1
}

extension_configure_args() {
  local args=(
    -S "$(ext_source_dir)"
    -B "$(ext_build_dir)"
    "-DSlicer_DIR:PATH=$SLICER_BUILD_DIR"
    -DBUILD_TESTING:BOOL=ON
  )
  # Slicer refuses to be found by a project configured with a different
  # compiler than its own, and the default compiler of a runner is not
  # necessarily the one Slicer was built with. The macOS deployment target and
  # architecture need no such care: Slicer sets them itself, through the
  # ConfigurePrerequisites component.
  local compiler
  compiler="$(slicer_config_value Slicer_CMAKE_C_COMPILER)"
  [ -n "$compiler" ] && args+=("-DCMAKE_C_COMPILER:FILEPATH=$compiler")
  compiler="$(slicer_config_value Slicer_CMAKE_CXX_COMPILER)"
  [ -n "$compiler" ] && args+=("-DCMAKE_CXX_COMPILER:FILEPATH=$compiler")
  case "$SLICER_PLATFORM" in
    windows)
      # /MP, as for Slicer itself: without it each project of the extension
      # compiles its files one after the other.
      args+=(-G "Visual Studio 17 2022" -A x64 "${MSVC_PARALLEL_ARGS[@]}")
      # Rather than the git recorded in SlicerConfig.cmake, which may not
      # exist on this runner (see native_git).
      local git; git="$(native_git)"
      [ -n "$git" ] && args+=("-DGIT_EXECUTABLE:FILEPATH=$git")
      ;;
    *)
      # Ninja, as Slicer itself, unless the extension asks for another
      # generator (SLICER_EXT_GENERATOR): the extensions are built with
      # Makefiles by the Slicer extension build, and Ninja refuses what those
      # let through, such as two modules installing a file of the same name.
      args+=(-G "${SLICER_EXT_GENERATOR:-Ninja}" "-DCMAKE_BUILD_TYPE:STRING=$SLICER_BUILD_TYPE")
      ;;
  esac
  while IFS= read -r line; do args+=("$line"); done < <(compiler_launcher_args)
  # The extensions it depends on, built by extension-dependencies: <Name>_DIR
  # of each, as the Slicer extension build hands them on.
  local deps; deps="$(bash_path "$(ext_deps_dir)")/cmake-args.txt"
  if [ -f "$deps" ]; then
    while IFS= read -r line; do [ -n "$line" ] && args+=("$line"); done < "$deps"
  fi
  # Options provided by the workflow, one per line or space separated.
  if [ -n "${SLICER_EXT_CMAKE_OPTIONS:-}" ]; then
    # shellcheck disable=SC2206
    args+=(${SLICER_EXT_CMAKE_OPTIONS})
  fi
  printf '%s\n' "${args[@]}"
}

ext_deps_dir() { echo "${SLICER_EXT_DEPS_DIR:-$SLICER_ROOT/ext-deps}"; }

# Provide the extensions the extension depends on (EXTENSION_DEPENDS, and
# theirs in turn), dependencies first, as slicer-extension-deps.py resolves
# them. One that a repository of the registry (SLICER_EXT_REGISTRY) publishes
# is restored from the build tree published for this platform and this Slicer
# commit (SLICER_COMMIT), so that the extension is linked against what its
# users install; any other is built from the ExtensionsIndex. Leaves
# <output>/extension-built-with.json.
cmd_extension_dependencies() {
  # A dependency that is being published again describes, for a moment, a
  # build tree that is not the one there: everything is then resolved and
  # downloaded again, a few times. An attempt asks for another by setting
  # `again` to the reason.
  local again tries
  for tries in 1 2 3 4; do
    again=""
    extension_dependencies_once
    [ -n "$again" ] || return 0
    [ "$tries" -lt 4 ] || break
    info "$again: is it being published? Trying again in $((tries * 60)) s"
    sleep $((tries * 60))
  done
  die "$again"
}

# One attempt of extension-dependencies; sets `again`, a variable of its
# caller, to the reason when it has to be made again.
extension_dependencies_once() {
  ext_parallel_compile
  local src; src="$(ext_checkout_dir)"
  local root; root="$(ext_deps_dir)"
  local broot; broot="$(bash_path "$root")"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  local py; py="$(command -v python3 || command -v python)" || die "extension-dependencies: no python"
  local here; here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && { pwd -W 2>/dev/null || pwd; })"
  [ -n "${SLICER_COMMIT:-}" ] || die "extension-dependencies: SLICER_COMMIT (the commit of the restored Slicer) is not set"
  rm -rf "$broot"; mkdir -p "$broot/download" "$out"
  : > "$broot/cmake-args.txt"; : > "$broot/module-paths.txt"
  log "Resolve the dependencies"
  local rc=0
  "$py" "$here/slicer-extension-deps.py" --source "$src" --platform "$SLICER_PLATFORM" \
      --slicer-commit "$SLICER_COMMIT" --registry "${SLICER_EXT_REGISTRY:-}" \
      --out "$root/plan.json" || rc=$?
  if [ "$rc" -eq 3 ] && [ "${SLICER_EXT_SKIP_UNREADY:-false}" = "true" ]; then
    # What it depends on is not built for this Slicer yet: not a fault of this
    # commit. The reconciliation builds it when it is there.
    endlog
    echo "::notice::$SLICER_PLATFORM skipped: an extension it depends on is not built for this Slicer yet (see above). It is built when that one is."
    [ -z "${GITHUB_OUTPUT:-}" ] || echo "ready=false" >> "$GITHUB_OUTPUT"
    return 0
  fi
  [ "$rc" -eq 0 ] || { endlog; die "extension-dependencies: could not resolve the dependencies"; }
  "$py" - "$root/plan.json" "$SLICER_OUTPUT_DIR/extension-built-with.json" > "$broot/plan.txt" <<'PY'
import json, sys
plan = json.load(open(sys.argv[1], encoding="utf8"))
json.dump({"depends": plan["depends"], "built_with": plan["built_with"]}, open(sys.argv[2], "w"), indent=1)
for d in plan["dependencies"]:
    if d["kind"] == "tree":
        tree = d["tree"]
        files = " ".join(f"{f}={tree.get('sha256', {}).get(f, '')}" for f in tree["files"])
        print("\t".join(["tree", d["name"], tree["base"], tree["config_dir"], files]))
    else:
        print("\t".join(["source", d["name"], d["url"], d["revision"], "-"]))
PY
  cat "$broot/plan.txt" >&2
  endlog
  local kind name a b c dir
  while IFS=$'\t' read -r kind name a b c; do
    [ -n "$kind" ] || continue
    c="${c%$'\r'}"
    log "Dependency $name"
    if [ "$kind" = "tree" ]; then
      # a: the release, b: where its <Name>Config.cmake is, c: file=sha256 ...
      local item file sum got first=""
      for item in $c; do
        file="${item%%=*}"; sum="${item#*=}"
        [ -n "$first" ] || first="$file"
        curl -fsSL --retry 3 --retry-delay 5 -o "$broot/download/$file" "$a/$file" \
          || { endlog; die "could not download $a/$file"; }
        got="$(sha256 < "$broot/download/$file" | cut -d' ' -f1)"
        if [ -n "$sum" ] && [ "$got" != "$sum" ]; then
          endlog
          # What it describes and what is there are of two builds.
          again="$file of $name is not the file its description records (sha256 $got, expected $sum)"
          return 0
        fi
      done
      cmd_unpack "$root/download/$first"
      rm -f "$broot/download/$first"
      dir="$SLICER_ROOT/$b"
      ls "$(bash_path "$dir")"/*Config.cmake >/dev/null 2>&1 \
        || { endlog; die "the build tree of $name has no CMake package configuration in $dir"; }
    else
      # a: the repository, b: the revision.
      local path="$root/src/$name" build="$root/build/$name"
      mkdir -p "$(bash_path "$path")"
      git -C "$(bash_path "$path")" init -q
      git -C "$(bash_path "$path")" remote add origin "$a"
      git -C "$(bash_path "$path")" fetch -q --depth 1 origin "$b" \
        || { endlog; die "could not fetch $b of $a"; }
      git -C "$(bash_path "$path")" checkout -q FETCH_HEAD
      local args=()
      # The extension's configure arguments, with the dependencies provided so
      # far, for this one, without its tests.
      while IFS= read -r line; do args+=("$line"); done < <(
        SLICER_EXT_NAME="" SLICER_EXT_SOURCE_DIR="$path" SLICER_EXT_BUILD_DIR="$build" \
          SLICER_EXT_CMAKE_OPTIONS="" extension_configure_args)
      args+=(-DBUILD_TESTING:BOOL=OFF)
      cmake_configure "$build" "configuring the dependency $name failed" "${args[@]}"
      cmake_build "$build"
      # Where the extension itself is: inner-build for one with a superbuild.
      dir="$build"
      has_inner_build "$build" && dir="$build/inner-build"
    fi
    echo "-D${name}_DIR:PATH=$dir" >> "$broot/cmake-args.txt"
    echo "$dir" >> "$broot/module-paths.txt"
    endlog
  done < "$broot/plan.txt"
  info "dependencies: $(tr '\n' ' ' < "$broot/cmake-args.txt")"
}

# Archive the build tree of the extension, for the extensions that depend on
# it: <output>/<Name>-build-<platform>.tar.zst, of <root>/E/<Name> and the
# source it was built from, <root>/ES/<Name>, and <output>/extension-tree.json,
# which says where its <Name>Config.cmake is. Object files and the staging
# area of the package are left out: a dependent links against the libraries.
cmd_extension_tree() {
  local name="${SLICER_EXT_NAME:-}"
  [ -n "$name" ] || die "extension-tree: SLICER_EXT_NAME is not set"
  ext_relocated || die "extension-tree: the build tree of an extension in a subdirectory of its repository cannot be published"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  local file="$name-build-$SLICER_PLATFORM.tar.zst"
  cmd_pack "$SLICER_OUTPUT_DIR/$file" --exclude '*/.git' --exclude '*.o' --exclude '*.obj' \
    --exclude '*/_CPack_Packages' "E/$name" "ES/$name"
  local size; size="$(wc -c < "$out/$file" | tr -d ' ')"
  [ "$size" -lt 2000000000 ] || die "extension-tree: $file is $size bytes, more than a release asset holds"
  local sub="."
  has_inner_build "$SLICER_ROOT/E/$name" && sub="inner-build"
  local dir="E/$name"; [ "$sub" = "." ] || dir="$dir/$sub"
  printf '{"files": ["%s"], "config_dir": "%s", "subdirectory": "%s"}\n' "$file" "$dir" "$sub" \
    > "$out/extension-tree.json"
  cat "$out/extension-tree.json"
}

# On Windows, have MSBuild compile the files of a project in parallel whatever
# flags the project was configured with: the projects an extension's
# superbuild adds (its own inner build among them) do not all get /MP from the
# options above. MSBuild reads its properties from the environment.
ext_parallel_compile() {
  [ "$SLICER_PLATFORM" = "windows" ] || return 0
  export UseMultiToolTask=true EnforceProcessCountAcrossBuilds=true
}

cmd_extension_configure() {
  if ext_relocated; then
    # The copy the extension is built from, at its fixed path; without the
    # actions of Slicer, which the workflow checks out inside the checkout.
    local from; from="$(bash_path "$(ext_checkout_dir)")"
    local to; to="$(bash_path "$(ext_source_dir)")"
    rm -rf "$to"; mkdir -p "$to"
    tar -C "$from" --exclude ./.slicer-actions -cf - . | tar -C "$to" -xf -
  fi
  local src; src="$(bash_path "$(ext_source_dir)")"
  [ -f "$src/CMakeLists.txt" ] || die "extension-configure: no CMakeLists.txt in $(ext_source_dir)"
  [ -f "$(bash_path "$SLICER_BUILD_DIR")/SlicerConfig.cmake" ] \
    || die "extension-configure: no Slicer build tree at $SLICER_BUILD_DIR"
  mkdir -p "$(bash_path "$(ext_build_dir)")"
  local args=()
  while IFS= read -r line; do args+=("$line"); done < <(extension_configure_args)
  log "Configure extension"
  cmake_configure "$(ext_build_dir)" "extension configure failed" "${args[@]}"
  endlog
}

cmd_extension_build() {
  ext_parallel_compile
  log "Build extension"
  cmake_build "$(ext_build_dir)"
  endlog
}

cmd_extension_test() {
  # An extension is not required to have tests.
  run_tests "Test extension" "$(ext_inner_dir)" extension-test-results.xml --no-tests=ignore
}

cmd_extension_package() {
  local inner; inner="$(ext_inner_dir)"
  local out; out="$(bash_path "$SLICER_OUTPUT_DIR")"
  mkdir -p "$out"
  log "Package extension"
  local logfile="$out/extension-package.log"
  cmake_build "$inner" --target package 2>&1 | tee "$logfile"
  endlog
  local packages
  packages="$(grep -oE 'CPack: - package: (.*) generated\.' "$logfile" \
    | sed -E 's/CPack: - package: (.*) generated\./\1/')"
  [ -n "$packages" ] || die "extension-package: no package was generated, see $logfile"
  local pkg
  while IFS= read -r pkg; do
    [ -n "$pkg" ] || continue
    info "package: $pkg"
    cp "$(bash_path "$pkg")" "$out/"
  done <<< "$packages"
  # What the extension says of itself (its .s4ext), for the lists that offer
  # it before it is installed. Not having it withholds nothing.
  local py; py="$(command -v python3 || command -v python || true)"
  if [ -n "$py" ]; then
    "$py" "$(dirname "${BASH_SOURCE[0]}")/extension-description.py" \
      "$(bash_path "$inner")" "$out/extension-metadata.json" \
      || echo "::warning::extension-package: the description of the extension could not be read"
  fi
}

# Load the built extension into the restored Slicer, which is what proves the
# two were built compatibly: a binary incompatibility shows up here and not in
# the build.
cmd_extension_load() {
  local launcher="$SLICER_BUILD_DIR/Slicer"
  [ "$SLICER_PLATFORM" = "windows" ] && launcher="$SLICER_BUILD_DIR/Slicer.exe"
  local inner; inner="$(ext_inner_dir)"
  local code="import slicer, sys; n = len(slicer.app.moduleManager().modulesNames()); print('modules loaded:', n); sys.exit(slicer.util.EXIT_SUCCESS if n else slicer.util.EXIT_FAILURE)"
  local args=(
    "$(bash_path "$launcher")"
    --no-splash --no-main-window --ignore-slicerrc
    --additional-module-paths "$inner"
  )
  # The modules of the extensions it depends on (extension-dependencies).
  local deps; deps="$(bash_path "$(ext_deps_dir)")/module-paths.txt"
  if [ -f "$deps" ]; then
    while IFS= read -r dir; do [ -n "$dir" ] && args+=("$dir"); done < "$deps"
  fi
  args+=(--python-code "$code")
  log "Load the extension in Slicer"
  local rc=0
  run_headless "${args[@]}" || rc=$?
  endlog
  [ $rc -eq 0 ] || die "the extension could not be loaded (exit $rc)"
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

usage() {
  cat <<EOF
Usage: $0 <command> [args]

Commands:
  env deps-key deps-stages restore-stage publish-prerequisites free-disk-space install-system-packages install-software-opengl
  configure build package verify-package strip test
  pack unpack split restore-release manifest manifest-files manifest-get
  extension-dependencies extension-tree extension-configure extension-build
  extension-test extension-package extension-load report

See the comment above each cmd_ function of this script.
EOF
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in ""|-h|--help|help) usage; return ;; esac
  local fn="cmd_${cmd//-/_}"
  declare -F "$fn" >/dev/null || die "unknown command: $cmd"
  "$fn" "$@"
}

main "$@"
