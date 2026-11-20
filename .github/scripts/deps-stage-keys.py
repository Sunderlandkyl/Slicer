#!/usr/bin/env python3
"""Keys of the stages the prerequisites of Slicer are built in, for slicer-ci.sh.

Usage: deps-stage-keys.py <Slicer source tree> <targets of stage 1> <targets of stage 2> ...
       (what all stages are built with is read from the standard input)

Prints one key per stage named. The key of a stage is the hash of the key of
the stage before it and of the files that describe the external projects the
stage builds, so that a change to an external project leaves the keys of the
stages before it as they were: the superbuild tree of the last of those is
reused, and only the stages after it are built again.

A stage builds its targets and what they depend on that no stage before it
built. What a project depends on is read from its SuperBuild/External_<name>.cmake
(the <name>_DEPENDENCIES it sets, and what its steps name after DEPENDS), all
conditions taken as true: more than what is built, never less.

The first key also covers everything that is not one project's: what comes on
the standard input (the configure arguments, the toolchain, the modules that
add external projects), SuperBuild.cmake, and the files of SuperBuild/ that no
project can be named for. A change to any of those rebuilds everything.

Two things are left out of every key, and so are built by the stage after the
last one named, which the key of the whole set covers (slicer-ci.sh deps-key):
the projects no named stage builds, and the revisions of the sources that
SuperBuild.cmake adds with Slicer_Remote_Add, which that last stage downloads.
"""
import hashlib
import os
import re
import sys

source = sys.argv[1]
stages = [targets.split() for targets in sys.argv[2:]]
common = sys.stdin.buffer.read().replace(b"\r", b"")
superbuild = os.path.join(source, "SuperBuild")


def read(path):
    with open(path, "rb") as f:
        return f.read().replace(b"\r", b"")


def text(path):
    return read(path).decode("utf8", "replace")


# The external projects, by name.
projects = {}
for name in sorted(os.listdir(superbuild)):
    match = re.fullmatch(r"External_(.+)\.cmake", name)
    if match:
        projects[match.group(1)] = os.path.join(superbuild, name)


def dependencies(project):
    """The projects a project may depend on."""
    content = re.sub(r"#[^\n]*", "", text(projects[project]))
    named = []
    # set(${proj}_DEPENDENCIES a b) and list(APPEND ${proj}_DEPENDENCIES a b), over several lines
    for match in re.finditer(r"_DEPENDENCIES\b([^()]*)\)", content):
        named += match.group(1).split()
    # DEPENDS a b, up to the next keyword or the end of the command
    for match in re.finditer(r"\bDEPENDS\b([^()]*?)(?=\b[A-Z_]{4,}\b|\))", content, re.S):
        named += match.group(1).split()
    found = set()
    for token in named:
        token = token.strip('"')
        # ${VTK_EXTERNAL_NAME} is VTK
        token = re.sub(r"^\$\{(\w+)_EXTERNAL_NAME\}$", r"\1", token)
        if token in projects and token != project:
            found.add(token)
    return found


def closure(targets):
    seen, todo = set(), [t for t in targets if t in projects]
    while todo:
        project = todo.pop()
        if project in seen:
            continue
        seen.add(project)
        todo += dependencies(project)
    return seen


# The stage that builds each project: the first whose targets need it.
stage_of = {}
for number, targets in enumerate(stages, 1):
    for project in closure(targets):
        stage_of.setdefault(project, number)
last = len(stages) + 1


def project_of(path):
    """The project a file of SuperBuild/ that is not an External_*.cmake is of, or None."""
    name = os.path.basename(path).lower()
    best = None
    for project in projects:
        prefix = project.lower()
        if name.startswith(prefix) and not name[len(prefix):len(prefix) + 1].isalnum():
            if best is None or len(project) > len(best):
                best = project
    return best


files = {number: [] for number in range(1, last + 1)}
for folder, _, names in os.walk(superbuild):
    for name in names:
        path = os.path.join(folder, name)
        match = re.fullmatch(r"External_(.+)\.cmake", name) if folder == superbuild else None
        project = match.group(1) if match else project_of(path)
        # A file that no project can be named for may be of use to any of them.
        files[stage_of.get(project, last) if project else 1].append(path)


def remote_revisions_removed(content):
    """SuperBuild.cmake without the revisions of the sources added with Slicer_Remote_Add."""
    def without(match):
        return re.sub(r"\n[ \t]*GIT_TAG[^\n]*", "\n", match.group(0))
    return re.sub(r"\bSlicer_Remote_Add\s*\([^()]*\)", without, content)


key = hashlib.sha256()
key.update(b"== common\n" + common)
key.update(b"== SuperBuild.cmake (without the revisions of the remote sources)\n")
key.update(remote_revisions_removed(text(os.path.join(source, "SuperBuild.cmake"))).encode("utf8"))
previous = ""
for number in range(1, last):
    stage = hashlib.sha256()
    stage.update(previous.encode() if previous else key.digest())
    for path in sorted(files[number], key=lambda p: os.path.relpath(p, source).replace(os.sep, "/")):
        stage.update(("== " + os.path.relpath(path, source).replace(os.sep, "/") + "\n").encode())
        stage.update(read(path))
    previous = stage.hexdigest()[:16]
    print(previous)

if os.environ.get("SLICER_STAGE_KEYS_VERBOSE"):
    for number in range(1, last + 1):
        built = sorted(p for p in projects if stage_of.get(p, last) == number)
        print(f"stage {number}: {' '.join(built)}", file=sys.stderr)
        for path in sorted(files[number]):
            print(f"  {os.path.relpath(path, source)}", file=sys.stderr)
