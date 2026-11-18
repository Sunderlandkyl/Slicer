#!/usr/bin/env python3
"""Publish the package a platform built of an extension, for extension-build.yml.

  extension-publish.py <folder>

<folder> holds slicer-package-<platform>.json, which describes the package,
and the files it names (the package, the icon, the build tree). They go to the
release TAG of the repository (GH_REPO), with extension-<platform>.json, which
says what the package was built from and against.

It is run by the job that built the platform, as soon as it is built: a
platform does not wait for the others, and the builds that depend on it are
started as soon as it is there. One description per platform, so that the
jobs do not overwrite each other.

Environment: GH_TOKEN, GH_REPO, TAG, NAME (the extension), REF (the branch),
GITHUB_REPOSITORY, GITHUB_SHA.
"""
import datetime, glob, hashlib, json, os, subprocess, sys, tempfile, time

folder = sys.argv[1]
tag, name = os.environ["TAG"], os.environ["NAME"]
repository, commit = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"]

def gh(*args, check=True):
    return subprocess.run(["gh", *args], check=check, capture_output=True, text=True)

def newer_is_published(old):
    """Whether the package a platform has is of a commit after this one.

    Not whether the branch has moved on since this build started: what it built
    is still newer than a package of an earlier commit, or than none, and is
    published; the build of the head replaces it in turn."""
    revision = (old or {}).get("revision")
    if not revision or revision == commit:
        return False
    # "ahead": what is published has commits this build does not.
    status = gh("api", f"repos/{repository}/compare/{commit}...{revision}", "--jq", ".status", check=False).stdout.strip()
    return status == "ahead"

def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()

def files_of(entry):
    """The assets of a package."""
    tree = (entry or {}).get("build_tree") or {}
    return {f for f in [(entry or {}).get("file"), (entry or {}).get("icon"), *tree.get("files", [])] if f}

descriptions = sorted(glob.glob(os.path.join(folder, "slicer-package-*.json")))
if not descriptions:
    print("::warning::no platform produced a package; nothing published")
    sys.exit(0)

# The runs of the other platforms may be creating the release too.
if gh("release", "view", tag, check=False).returncode != 0:
    gh("release", "create", tag, "--title", f"{name} {tag}", "--prerelease", "--latest=false",
       "--notes", f"Packages of {name}.", check=False)
    for _ in range(5):
        if gh("release", "view", tag, check=False).returncode == 0:
            break
        time.sleep(5)
# What the release holds: the description of each platform. Only
# those of the platforms this run built are written.
release = tempfile.mkdtemp()
gh("release", "download", tag, "-p", "extension-*.json", "-D", release, "--clobber", check=False)

date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S+00:00")
done = []
for description in descriptions:
    package = json.load(open(description, encoding="utf8"))
    platform = package["platform"]
    files = package["packages"]
    entry = {
        "file": files[0],
        "sha256": sha256(os.path.join(folder, files[0])),
        "revision": commit,
        "date": date,
        "slicer_commit": package["slicer_commit"],
    }
    if package.get("slicer_revision"):
        entry["slicer_revision"] = package["slicer_revision"]
    if package.get("tests"):
        entry["tests"] = package["tests"]
    if len(files) > 1:
        entry["icon"] = files[1]
    entry["depends"] = package.get("depends") or []
    entry["built_with"] = package.get("built_with") or {}
    tree = package.get("build_tree")
    if tree:
        entry["build_tree"] = dict(tree, sha256={f: sha256(os.path.join(folder, f)) for f in tree["files"]})

    path = os.path.join(release, f"extension-{platform}.json")
    described = json.load(open(path, encoding="utf8")) if os.path.isfile(path) else {}
    old, previous = described.get("package"), described.get("previous")
    if newer_is_published(old):
        print(f"::notice::{platform}: the release holds the package of a newer commit "
              f"({str(old.get('revision'))[:10]}, this is {commit[:10]}); not published")
        continue
    done.append(platform)
    # The package the platform had. Built against another Slicer, it
    # is kept as the previous one, for the Slicer builds that are
    # still offered it, in place of the one before it; without its
    # build tree, which nothing is built against any more.
    if old and old.get("slicer_commit") != entry["slicer_commit"]:
        superseded = files_of(previous) | set((old.get("build_tree") or {}).get("files", []))
        previous = {k: v for k, v in old.items() if k != "build_tree"}
    else:
        superseded = files_of(old)
    uploads = sorted(files_of(entry))
    # A file name that does not change from one build to the next (a
    # wheel's) now holds the new package: there is no previous one.
    if files_of(previous) & set(uploads):
        previous = None
    described = {"name": name, "repository": repository, "platform": platform, "date": date,
                 "package": entry, "previous": previous}
    if platform == "web":
        described["web"] = package["web"]
    with open(path, "w", encoding="utf8") as f:
        json.dump(described, f, indent=1)
    print(json.dumps(described, indent=1))
    # The package first, its description, which names it, last.
    gh("release", "upload", tag, *[os.path.join(folder, f) for f in uploads], "--clobber")
    gh("release", "upload", tag, path, "--clobber")
    for stale in sorted(superseded - set(uploads) - files_of(previous)):
        print(f"Removing the superseded asset {stale}")
        gh("release", "delete-asset", tag, stale, "--yes", check=False)

# The description of all the platforms in one file, of before there
# was one per platform.
gh("release", "delete-asset", tag, "extension.json", "--yes", check=False)

# The notes say what each package was built against.
lines = []
gh("release", "download", tag, "-p", "extension-*.json", "-D", release, "--clobber", check=False)
for path in sorted(glob.glob(os.path.join(release, "extension-*.json"))):
    described = json.load(open(path, encoding="utf8"))
    entry = described.get("package") or {}
    line = f"- {described.get('platform')}: `{str(entry.get('revision'))[:10]}`, Slicer `{entry.get('slicer_commit')}`"
    if entry.get("slicer_revision"):
        line += f" (r{entry['slicer_revision']})"
    for dependency, used in sorted((entry.get("built_with") or {}).items()):
        line += f", {dependency} `{str(used.get('revision'))[:10]}`"
    if entry.get("tests") == "failed":
        line += ", tests failed"
    lines.append(f"{line}: {entry.get('file')}")
notes = (f"Packages of {name}: what each was built from, and against which Slicer and which of the"
         " extensions it depends on.\n\n" + "\n".join(lines) + "\n\n"
         "extension-<platform>.json says the same, for the catalog of the Slicer release, which\n"
         "the Extensions Managers read.")
gh("release", "edit", tag, "--notes", notes, "--latest=false", "--prerelease", check=False)
with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
    f.write(f"Published {', '.join(done) or 'nothing'} to the release {tag}.\n")
