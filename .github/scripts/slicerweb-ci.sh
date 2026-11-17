#!/usr/bin/env bash
#
# Helper script used by build-web.yml to build Slicer for the web browser with
# SlicerWeb (https://github.com/lassoan/SlicerWeb): Slicer's libraries and
# modules compiled to WebAssembly, packaged as Pyodide wheels.
#
# SlicerWeb builds in its own container (Emscripten and the Pyodide cross-build
# environment), in stages (scripts/stages/NN-name.sh of SlicerWeb) that keep
# everything they build in /build. Here /build is a folder of the runner, so
# that it can be archived after one stage and restored before the next, the way
# slicer-ci.sh carries the superbuild tree of the desktop builds.
#
# Layout under the root directory (SLICERWEB_ROOT, /home/runner/SW):
#
#   <root>/build    /build of the container: sources, build and install trees
#   <root>/dist     /dist of the container: the wheels, the logs
#   <root>/out      archives
#
# Usage: slicerweb-ci.sh <command> [args...]
#
set -euo pipefail

SLICERWEB_ROOT="${SLICERWEB_ROOT:-/home/runner/SW}"
# The SlicerWeb checkout.
SLICERWEB_DIR="${SLICERWEB_DIR:-$GITHUB_WORKSPACE/slicerweb}"
SLICERWEB_IMAGE="${SLICERWEB_IMAGE:-slicerweb-toolchain:ci}"
SLICERWEB_OUTPUT_DIR="${SLICERWEB_OUTPUT_DIR:-$SLICERWEB_ROOT/out}"

log() { echo "::group::$*"; }
endlog() { echo "::endgroup::"; }
info() { echo "[slicerweb-ci] $*"; }
die() { echo "::error::$*" >&2; exit 1; }

# Key of the prerequisites: everything that describes the stages before
# Slicer's own (00-sources to 45-sem). The revision of Slicer is left out: no
# prerequisite is built from it, and the application stage builds whichever
# revision it is given.
cmd_deps_key() {
  ( cd "$SLICERWEB_DIR"
    { echo "== slicerweb prerequisites"
      echo "== sources.env"; grep -v '^SLICER_' sources.env
      for f in $(find docker cmake/vtk cmake/itk patches scripts/env.sh scripts/fetch.sh \
                   scripts/build.sh scripts/emulator.sh -type f 2>/dev/null | LC_ALL=C sort) \
               $(ls scripts/stages/{00,10,20,30,40,45}-*.sh | LC_ALL=C sort); do
        case "$f" in patches/Slicer/*) continue ;; esac
        echo "== $f"; cat "$f"
      done
      echo "key-version=${SLICER_DEPS_KEY_VERSION:-1}"
    } | tr -d '\r' | sha256sum | cut -c1-16 )
}

# Build the toolchain image of SlicerWeb.
cmd_image() {
  log "Build the toolchain image"
  docker build -t "$SLICERWEB_IMAGE" "$SLICERWEB_DIR/docker"
  endlog
}

# Run stages of SlicerWeb in its container.
#
#   run [--slicer <checkout>] <stage> [<stage> ...]
#
# --slicer builds the commit checked out in <checkout> instead of the Slicer
# revision SlicerWeb pins.
cmd_run() {
  local slicer=""
  if [ "${1:-}" = "--slicer" ]; then slicer="$2"; shift 2; fi
  [ $# -gt 0 ] || die "run: no stage"
  mkdir -p "$SLICERWEB_ROOT/build" "$SLICERWEB_ROOT/dist"
  local args=(
    --rm
    -v "$SLICERWEB_ROOT/build:/build"
    -v "$SLICERWEB_DIR:/work:ro"
    -v "$SLICERWEB_ROOT/dist:/dist"
    -e PYTHONDONTWRITEBYTECODE=1
    -e "SW_JOBS=${SLICERWEB_JOBS:-$(nproc)}"
    # dev (-O2), SlicerWeb's default and that of its published runtime, or
    # release (-Oz).
    -e "SW_PROFILE=${SLICERWEB_PROFILE:-dev}"
    # Every stage runs once per job, on a fresh runner: a compiler cache
    # would never be read, only fill the disk.
    -e CCACHE_DISABLE=1
  )
  if [ -n "$slicer" ]; then
    local rev; rev="$(git -C "$slicer" rev-parse HEAD)"
    info "Slicer: $rev from $slicer"
    args+=(-v "$slicer:/slicer:ro" -e SW_SLICER_URL=/slicer -e "SW_SLICER_REV=$rev")
  fi
  docker run "${args[@]}" "$SLICERWEB_IMAGE" bash /work/scripts/build.sh "$@"
}

# Archive the build folder. The container writes as root, hence sudo.
#
#   pack <archive> [<tar exclude pattern> ...]
cmd_pack() {
  local archive="${1:?pack: missing archive}"; shift
  local excludes=(--exclude ./.ccache)
  for p in "$@"; do excludes+=(--exclude "$p"); done
  mkdir -p "$(dirname "$archive")"
  log "Pack $SLICERWEB_ROOT/build -> $archive"
  sudo du -sh "$SLICERWEB_ROOT/build"/* "$SLICERWEB_ROOT/build"/.[!.]* 2>/dev/null | sort -h | tail -n 20 || true
  # Object files are only needed to rebuild what is already built and
  # installed: the later stages link against the install trees.
  sudo tar -C "$SLICERWEB_ROOT/build" "${excludes[@]}" --exclude '*.o' -cf - . \
    | zstd -T0 -3 -q -o "$archive" --force
  ls -lh "$archive"
  endlog
}

# Restore an archive created with pack, possibly split, into the build folder.
cmd_unpack() {
  local archive="${1:?unpack: missing archive}"
  if [ ! -f "$archive" ] && ls "$archive".part-* >/dev/null 2>&1; then
    info "joining parts"
    cat "$archive".part-* > "$archive"
    rm -f "$archive".part-*
  fi
  [ -f "$archive" ] || die "unpack: $archive not found"
  log "Unpack $archive -> $SLICERWEB_ROOT/build"
  sudo mkdir -p "$SLICERWEB_ROOT/build"
  zstd -d -q -c "$archive" | sudo tar -C "$SLICERWEB_ROOT/build" -xf -
  rm -f "$archive"
  endlog
}

usage() {
  cat <<EOF
Usage: $0 <command> [args...]

Commands:
  deps-key                     Print the key of the prerequisites
  image                        Build the toolchain image
  run [--slicer <dir>] <stage>...  Run SlicerWeb build stages
  pack <archive> [<exclude>...]    Archive the build folder
  unpack <archive>             Restore the build folder
EOF
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in
    deps-key) cmd_deps_key "$@" ;;
    image) cmd_image "$@" ;;
    run) cmd_run "$@" ;;
    pack) cmd_pack "$@" ;;
    unpack) cmd_unpack "$@" ;;
    ""|-h|--help|help) usage ;;
    *) usage; die "unknown command: $cmd" ;;
  esac
}

main "$@"
