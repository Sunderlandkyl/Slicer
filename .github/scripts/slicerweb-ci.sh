#!/usr/bin/env bash
#
# Helper script of build-web.yml: builds Slicer for the web browser with
# SlicerWeb (https://github.com/lassoan/SlicerWeb), in SlicerWeb's container
# and in its stages (scripts/stages/NN-name.sh of SlicerWeb). /build of the
# container is a folder of the runner, so that it can be archived after one
# stage and restored before the next.
#
# Layout under the root directory (SLICERWEB_ROOT, /home/runner/SW):
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

log() { echo "::group::$*"; }
endlog() { echo "::endgroup::"; }
info() { echo "[slicerweb-ci] $*"; }
die() { echo "::error::$*" >&2; exit 1; }

# Key of the prerequisites: everything that describes the stages before
# Slicer's own (00-sources to 45-sem). The revision of Slicer is left out: no
# prerequisite is built from it.
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
#   run [--slicer <checkout>] [--extension <checkout> --descriptions <dir>]
#       <stage> [<stage> ...]
# --slicer builds the commit checked out there instead of the revision
# SlicerWeb pins. --extension mounts an extension checkout at /ext, and
# --descriptions the description files that 80-extensions builds.
cmd_run() {
  local slicer="" extension="" descriptions=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --slicer) slicer="$2"; shift 2 ;;
      --extension) extension="$2"; shift 2 ;;
      --descriptions) descriptions="$2"; shift 2 ;;
      *) break ;;
    esac
  done
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
  [ -z "$extension" ] || args+=(-v "$extension:/ext:ro")
  [ -z "$descriptions" ] || args+=(-v "$descriptions:/extensions:ro" -e SW_EXTENSIONS_DIR=/extensions)
  # Which extensions of the descriptions 80-extensions builds; the others keep
  # what is in the build folder.
  [ -z "${SW_EXTENSIONS:-}" ] || args+=(-e "SW_EXTENSIONS=$SW_EXTENSIONS")
  docker run "${args[@]}" "$SLICERWEB_IMAGE" bash /work/scripts/build.sh "$@"
}

# Build an extension from its checkout against the restored build folder, with
# SlicerWeb's 80-extensions stage, and gather its wheel.
#   extension <checkout> <name> <output> [<build subdirectory>]
# <name> is the CMake project name. Its description is the one of SlicerWeb's
# catalog (extensions/<name>.json) when there is one; otherwise one is made,
# with <build subdirectory> ("inner-build" with a superbuild, "." without).
# The extensions it depends on are provided as on the desktop platforms
# (slicer-extension-deps.py): restored from a published build tree, or built
# from source. Only the extension's own wheel is gathered.
# <output> receives the wheel, the icon, extension-web.json (the entry of
# SlicerWeb's extension index) and extension-built-with.json; with
# SLICER_EXT_TREE=true also the build tree of the extension
# (<name>-build-web.tar.zst and extension-tree.json).
cmd_extension() {
  local src="${1:?extension: missing checkout}" name="${2:?extension: missing name}"
  local output="${3:?extension: missing output}" sub="${4:-}"
  local descriptions="$SLICERWEB_ROOT/extension-descriptions"
  local work="$SLICERWEB_ROOT/extension-deps"
  local here; here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  rm -rf "$descriptions" "$work"; mkdir -p "$descriptions" "$output" "$work/download"
  local rev; rev="$(git -C "$src" rev-parse HEAD)"
  [ -n "${SLICER_COMMIT:-}" ] || die "extension: SLICER_COMMIT (the commit of the restored Slicer) is not set"

  # Its own description, and its dependencies: the catalog's list may be
  # shorter than EXTENSION_DEPENDS (what it needs in the browser).
  python3 - "$SLICERWEB_DIR/extensions" "$descriptions" "$name" "$rev" "$sub" "$src" "$work/depends.txt" <<'PY'
import json, os, re, sys
catalog, out, name, rev, sub, src, depends_file = sys.argv[1:8]

def cmake_depends(source):
    try:
        text = open(os.path.join(source, "CMakeLists.txt"), encoding="utf8", errors="replace").read()
    except OSError:
        return []
    match = re.search(r"set\s*\(\s*EXTENSION_DEPENDS\s+([^)]*)\)", text, re.I)
    names = [n.strip('"') for n in re.split(r"[\s;]+", match.group(1))] if match else []
    return [n for n in names if n and n != "NA"]

path = os.path.join(catalog, name + ".json")
if os.path.isfile(path):
    own = json.load(open(path, encoding="utf8"))
    print(f"{name}: the description of SlicerWeb's catalog")
else:
    print(f"{name} is not in SlicerWeb's catalog: building it with build subdirectory {sub or '.'!r}")
    own = {"build_dependencies": cmake_depends(src)}
if sub:
    own["build_subdirectory"] = sub
own.setdefault("build_subdirectory", ".")
# The checkout, mounted at /ext, at its commit.
own["scm_url"], own["scm_revision"] = "/ext", rev
json.dump(own, open(os.path.join(out, name + ".json"), "w"), indent=1)
open(depends_file, "w").write(" ".join(own.get("build_dependencies") or []))
PY
  local rc=0
  python3 "$here/slicer-extension-deps.py" --source "$src" --platform web \
      --slicer-commit "$SLICER_COMMIT" --registry "${SLICER_EXT_REGISTRY:-}" \
      --depends "$(cat "$work/depends.txt")" --out "$work/plan.json" || rc=$?
  if [ "$rc" -eq 3 ] && [ "${SLICER_EXT_SKIP_UNREADY:-false}" = "true" ]; then
    # As on the desktop platforms: nothing to build against yet, which is not
    # a fault of this commit.
    echo "::notice::web skipped: an extension it depends on is not built for this Slicer yet (see above). It is built when that one is."
    [ -z "${GITHUB_OUTPUT:-}" ] || echo "ready=false" >> "$GITHUB_OUTPUT"
    return 0
  fi
  [ "$rc" -eq 0 ] || die "extension: could not resolve the dependencies"

  # The description of each dependency, by which 80-extensions finds its build
  # tree, and the list of what there is to restore.
  python3 - "$SLICERWEB_DIR/extensions" "$descriptions" "$work/plan.json" "$output/extension-built-with.json" \
      > "$work/plan.txt" <<'PY'
import json, os, sys
catalog, out, plan_file, built_with_file = sys.argv[1:5]
plan = json.load(open(plan_file, encoding="utf8"))
json.dump({"depends": plan["depends"], "built_with": plan["built_with"]}, open(built_with_file, "w"), indent=1)
for d in plan["dependencies"]:
    path = os.path.join(catalog, d["name"] + ".json")
    description = json.load(open(path, encoding="utf8")) if os.path.isfile(path) else dict(d.get("description") or {})
    if d["kind"] == "tree":
        tree = d["tree"]
        description["build_subdirectory"] = tree.get("subdirectory", ".")
        description["scm_url"] = f"https://github.com/{d['repository']}.git"
        description["scm_revision"] = d["revision"]
        files = " ".join(f"{f}={tree.get('sha256', {}).get(f, '')}" for f in tree["files"])
        print("\t".join(["tree", d["name"], tree["base"], files]))
    else:
        description.setdefault("build_subdirectory", ".")
        description["scm_url"], description["scm_revision"] = d["url"], d["revision"]
        print("\t".join(["source", d["name"], d["url"], d["revision"]]))
    json.dump(description, open(os.path.join(out, d["name"] + ".json"), "w"), indent=1)
PY
  cat "$work/plan.txt"
  local build=("$name") kind dep a b item file sum got
  while IFS=$'\t' read -r kind dep a b; do
    [ -n "$kind" ] || continue
    if [ "$kind" = "source" ]; then
      build+=("$dep")
      continue
    fi
    log "Dependency $dep: its build tree"
    for item in $b; do
      file="${item%%=*}"; sum="${item#*=}"
      curl -fsSL --retry 3 --retry-delay 5 -o "$work/download/$file" "$a/$file" \
        || die "could not download $a/$file"
      got="$(sha256sum < "$work/download/$file" | cut -d' ' -f1)"
      if [ -n "$sum" ] && [ "$got" != "$sum" ]; then
        # The dependency is being published again: resolved again.
        local tries="${SLICER_EXT_DEPS_ATTEMPT:-1}"
        if [ "$tries" -lt 4 ]; then
          endlog
          info "$file of $dep is not the file its description records: is it being published? Trying again in $((tries * 60)) s"
          sleep $((tries * 60))
          SLICER_EXT_DEPS_ATTEMPT=$((tries + 1)) cmd_extension "$src" "$name" "$output" "$sub"
          return
        fi
        die "$file of $dep is not the file its description records (sha256 $got, expected $sum)"
      fi
      zstd -d -q -c "$work/download/$file" | sudo tar -C "$SLICERWEB_ROOT/build" -xf -
      rm -f "$work/download/$file"
    done
    endlog
  done < "$work/plan.txt"

  SW_EXTENSIONS="${build[*]}" cmd_run --extension "$src" --descriptions "$descriptions" 80-extensions
  local ext="$SLICERWEB_ROOT/dist/extensions"
  python3 - "$ext" "$name" "$output" <<'PY'
import json, os, shutil, sys
folder, name, output = sys.argv[1:4]
index = json.load(open(os.path.join(folder, "index.json")))["extensions"]
entry = next((e for e in index if e["name"] == name), None)
if entry is None:
    sys.exit(f"::error::no wheel of {name} among {[e['name'] for e in index]}")
for key in ("wheel", "icon"):
    if entry.get(key) and os.path.isfile(os.path.join(folder, entry[key])):
        shutil.copy(os.path.join(folder, entry[key]), output)
json.dump(entry, open(os.path.join(output, "extension-web.json"), "w"), indent=1)
print(json.dumps(entry, indent=1))
PY

  if [ "${SLICER_EXT_TREE:-false}" = "true" ]; then
    # What an extension that depends on this one needs of the build folder:
    # its build tree (<Name>Config.cmake, the libraries), the source that tree
    # refers to, and what it installed, which the wheels are made of.
    local tree="$name-build-web.tar.zst"
    local subdirectory; subdirectory="$(jq -r '.build_subdirectory // "."' "$descriptions/$name.json")"
    log "Pack the build tree of $name -> $tree"
    sudo tar -C "$SLICERWEB_ROOT/build" --exclude '*.o' --exclude "src/$name/.git" -cf - \
        "ext/$name" "install/ext/$name" "src/$name" \
      | zstd -T0 -3 -q -o "$output/$tree" --force
    ls -lh "$output/$tree"
    endlog
    local dir="ext/$name"; [ "$subdirectory" = "." ] || dir="$dir/$subdirectory"
    jq -n --arg file "$tree" --arg dir "$dir" --arg sub "$subdirectory" \
      '{files: [$file], config_dir: $dir, subdirectory: $sub}' > "$output/extension-tree.json"
  fi
  ls -l "$output"
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

Commands: deps-key image run extension pack unpack

See the comment above each cmd_ function of this script.
EOF
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in ""|-h|--help|help) usage; return ;; esac
  local fn="cmd_${cmd//-/_}"
  declare -F "$fn" >/dev/null || { usage; die "unknown command: $cmd"; }
  "$fn" "$@"
}

main "$@"
