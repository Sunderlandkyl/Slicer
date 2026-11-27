#!/usr/bin/env bash
#
# Publish what one platform built to a release, as soon as it is built, without
# waiting for the other platforms and without touching what is theirs.
#
#   publish-release.sh <tag> <title> <stale regex> <manifest> <file>...
#
# <manifest> says what the files were built from: it is what a later run reads
# to know which commit the release holds. It is
# removed first and uploaded last, so that it never describes files that are
# not all there: while the files are being replaced there is no manifest, and
# a build that starts then finds nothing to build against rather than a mix.
#
# <stale regex> matches the names of the assets that are this platform's: the
# ones among them that this run did not upload (yesterday's package, whose name
# carries the date; the parts of an archive that is no longer split) are
# removed.
#
# RELEASE_NOTES, when set, is the text the notes of the release end with. They
# are then written again here, each time: what the release holds of each
# platform (its manifests), followed by that text. They are worked out from
# the release rather than edited, so platforms publishing at the same time
# cannot damage them: at worst a line is of the previous build until the next
# platform publishes.
#
# Uses gh and jq, with GH_TOKEN and GH_REPO.
set -euo pipefail

tag="${1:?missing tag}"; title="${2:?missing title}"; stale="${3:?missing stale regex}"
manifest="${4:?missing manifest}"; shift 4
[ $# -gt 0 ] || { echo "::error::publish-release: no file"; exit 1; }
for f in "$manifest" "$@"; do
  [ -f "$f" ] || { echo "::error::publish-release: $f does not exist"; exit 1; }
done

retry() {
  local attempt
  for attempt in 1 2 3 4; do
    "$@" && return 0
    echo "attempt $attempt failed: $*" >&2
    sleep $((attempt * 20))
  done
  return 1
}

# The other platforms may be creating the release at the same time.
if ! gh release view "$tag" >/dev/null 2>&1; then
  gh release create "$tag" --title "$title" --notes "$title." --prerelease --latest=false || true
  retry gh release view "$tag" >/dev/null
fi

gh release delete-asset "$tag" "$(basename "$manifest")" --yes >/dev/null 2>&1 || true
retry gh release upload "$tag" "$@" --clobber
retry gh release upload "$tag" "$manifest" --clobber

kept="$(mktemp)"
for f in "$manifest" "$@"; do basename "$f"; done | LC_ALL=C sort -u > "$kept"
gh release view "$tag" --json assets --jq '.assets[].name' | tr -d '\r' | LC_ALL=C sort -u \
  | { grep -E "$stale" || true; } | LC_ALL=C comm -23 - "$kept" | while read -r asset; do
  [ -n "$asset" ] || continue
  echo "Removing the superseded asset $asset"
  gh release delete-asset "$tag" "$asset" --yes || true
done
rm -f "$kept"

if [ -n "${RELEASE_NOTES:-}" ]; then
  dir="$(mktemp -d)"
  gh release download "$tag" -D "$dir" --clobber -p '*manifest*.json' >/dev/null 2>&1 || true
  {
    echo "Each platform built from:"
    echo ""
    for m in "$dir"/*.json; do
      [ -f "$m" ] || continue
      jq -r 'select(.platform and .commit) | "- \(.platform): `\(.commit)`, \((.date // "")[0:10])"' "$m"
    done
    echo ""
    echo "$RELEASE_NOTES"
  } > "$dir/notes.md"
  (cd "$dir" && gh release edit "$tag" --notes-file notes.md --latest=false --prerelease) \
    || echo "::warning::Could not write the notes of the release $tag"
  rm -rf "$dir"
fi
echo "Published $(($# + 1)) files to the release $tag."
