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
#   run [--slicer <checkout>] [--extension <checkout> --descriptions <dir>]
#       <stage> [<stage> ...]
#
# --slicer builds the commit checked out in <checkout> instead of the Slicer
# revision SlicerWeb pins. --extension mounts the checkout of an extension at
# /ext, and --descriptions the folder of extension description files that
# 80-extensions builds (see the extension command).
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
  docker run "${args[@]}" "$SLICERWEB_IMAGE" bash /work/scripts/build.sh "$@"
}

# Build an extension from its checkout against the restored build folder, as
# the extension repository's own build does on the desktop platforms, with
# SlicerWeb's 80-extensions stage, and gather its wheel.
#
#   extension <checkout> <name> <output> [<build subdirectory>]
#
# <name> is the CMake project name of the extension. Its description is the
# one of SlicerWeb's catalog (extensions/<name>.json: build subdirectory,
# dependencies, slicerweb options) when there is one, pointed at the checkout;
# otherwise one is made, with <build subdirectory> ("inner-build" for an
# extension with a superbuild, "." without). The extensions it depends on are
# built first, from the descriptions of the catalog, but only its own wheel is
# gathered: theirs are published by their own repositories.
#
# <output> receives the wheel, the icon if any, and extension-web.json, the
# entry of the extension index SlicerWeb's Extensions Manager reads.
cmd_extension() {
  local src="${1:?extension: missing checkout}" name="${2:?extension: missing name}"
  local output="${3:?extension: missing output}" sub="${4:-}"
  local descriptions="$SLICERWEB_ROOT/extension-descriptions"
  rm -rf "$descriptions"; mkdir -p "$descriptions" "$output"
  local rev; rev="$(git -C "$src" rev-parse HEAD)"
  python3 - "$SLICERWEB_DIR/extensions" "$descriptions" "$name" "$rev" "$sub" "$src" \
    "${SLICER_EXT_REGISTRY:-}" "${SLICER_EXT_INDEX_URL:-https://raw.githubusercontent.com/Slicer/ExtensionsIndex/main}" <<'PY'
import json, os, re, sys, time, urllib.error, urllib.request
catalog, out, name, rev, sub, src, registry_url, index_url = sys.argv[1:9]

def get_json(url, attempts=3):
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
        except Exception:
            pass
        time.sleep(5 * (attempt + 1))
    return None

def described(n):
    path = os.path.join(catalog, n + ".json")
    return json.load(open(path, encoding="utf8")) if os.path.isfile(path) else None

def cmake_depends(source):
    try:
        text = open(os.path.join(source, "CMakeLists.txt"), encoding="utf8", errors="replace").read()
    except OSError:
        return []
    match = re.search(r"set\s*\(\s*EXTENSION_DEPENDS\s+([^)]*)\)", text, re.I)
    names = [n.strip('"') for n in re.split(r"[\s;]+", match.group(1))] if match else []
    return [n for n in names if n and n != "NA"]

# The registry: name -> (repository, revision) of what each repository published.
published = {}
registry = get_json(registry_url) if registry_url else None
if registry:
    tag = registry.get("release_tag", "nightly")
    for entry in registry.get("repositories", []):
        repo = entry["repository"]
        e = get_json(f"https://github.com/{repo}/releases/download/{tag}/extension.json")
        if e and e.get("name"):
            published[e["name"]] = (repo, e.get("revision") or entry.get("default_branch"))

own = described(name)
if own is None:
    print(f"{name} is not in SlicerWeb's catalog: building it with build subdirectory {sub or '.'!r}")
    own = {"build_dependencies": cmake_depends(src)}
else:
    print(f"{name}: the description of SlicerWeb's catalog")
if sub:
    own["build_subdirectory"] = sub
own.setdefault("build_subdirectory", ".")
# The checkout, mounted at /ext, at its commit.
own["scm_url"], own["scm_revision"] = "/ext", rev
json.dump(own, open(os.path.join(out, name + ".json"), "w"), indent=1)

# Its dependencies: what SlicerWeb's catalog says of each (build subdirectory,
# browser-specific options), else the ExtensionsIndex; from the repository of
# the registry that published it, at the revision it published, as on the
# desktop platforms, else from where that description says.
pending, seen = list(own.get("build_dependencies") or []), set()
while pending:
    dep = pending.pop()
    if dep in seen:
        continue
    seen.add(dep)
    description = described(dep) or get_json(f"{index_url}/{dep}.json")
    if description is None and dep not in published:
        print(f"::warning::{name} depends on {dep}, which neither the registry, SlicerWeb's catalog nor the ExtensionsIndex describes")
        continue
    description = dict(description or {"build_subdirectory": "."})
    if dep in published:
        repo, revision = published[dep]
        description["scm_url"], description["scm_revision"] = f"https://github.com/{repo}.git", revision
    print(f"{name} depends on {dep}: built from {description['scm_url']} {description['scm_revision']}")
    json.dump(description, open(os.path.join(out, dep + ".json"), "w"), indent=1)
    pending.extend(description.get("build_dependencies") or [])
PY
  cmd_run --extension "$src" --descriptions "$descriptions" 80-extensions
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

Commands:
  deps-key                     Print the key of the prerequisites
  image                        Build the toolchain image
  run [--slicer <dir>] <stage>...  Run SlicerWeb build stages
  extension <checkout> <name> <output> [<build subdirectory>]
                               Build an extension and gather its wheel
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
    extension) cmd_extension "$@" ;;
    pack) cmd_pack "$@" ;;
    unpack) cmd_unpack "$@" ;;
    ""|-h|--help|help) usage ;;
    *) usage; die "unknown command: $cmd" ;;
  esac
}

main "$@"
