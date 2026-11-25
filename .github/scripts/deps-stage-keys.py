#!/usr/bin/env python3
"""The stages the prerequisites of Slicer are built in, for slicer-ci.sh.

Usage: deps-stage-keys.py <Slicer source tree> <targets of stage 1> <targets of stage 2> ...
       (what all stages are built with is read from the standard input)

Prints one line per stage: its key, then the external projects it builds. The
last line, with - for a key, is the stage that builds everything else.

The key of a stage is the hash of the key of the stage before it and of the
files that describe the projects the stage builds: a change to a project
leaves the keys of the stages before it as they were, so the build starts
from the tree the last of those left.

A stage builds its targets and what they depend on that no stage before it
built. What a project depends on is read from its SuperBuild/External_<name>.cmake
(the <name>_DEPENDENCIES it sets, and what its steps name after DEPENDS), all
conditions taken as true: more than what is built, never less.

The first key also covers what is not one project's: the standard input,
SuperBuild.cmake, and the files of SuperBuild/ that no project can be named
for. Left out of every key are the revisions of the sources SuperBuild.cmake
adds with Slicer_Remote_Add, which the last stage downloads.
"""
import hashlib
import os
import re
import sys

source = sys.argv[1]
stages = [targets.split() for targets in sys.argv[2:]]
common = sys.stdin.buffer.read().replace(b"\r", b"")
superbuild = os.path.join(source, "SuperBuild")
last = len(stages) + 1


def read(path):
    with open(path, "rb") as f:
        return f.read().replace(b"\r", b"")


def relative(path):
    return os.path.relpath(path, source).replace(os.sep, "/")


projects = {}
for name in sorted(os.listdir(superbuild)):
    match = re.fullmatch(r"External_(.+)\.cmake", name)
    if match:
        projects[match.group(1)] = os.path.join(superbuild, name)


def dependencies(project):
    """The projects a project may depend on."""
    content = re.sub(r"#[^\n]*", "", read(projects[project]).decode("utf8", "replace"))
    named = []
    # set(${proj}_DEPENDENCIES a b) and list(APPEND ${proj}_DEPENDENCIES a b), over several lines
    for match in re.finditer(r"_DEPENDENCIES\b([^()]*)\)", content):
        named += match.group(1).split()
    # DEPENDS a b, up to the next keyword or the end of the command
    for match in re.finditer(r"\bDEPENDS\b([^()]*?)(?=\b[A-Z_]{4,}\b|\))", content, re.S):
        named += match.group(1).split()
    # ${VTK_EXTERNAL_NAME} is VTK
    named = {re.sub(r"^\$\{(\w+)_EXTERNAL_NAME\}$", r"\1", token.strip('"')) for token in named}
    return {token for token in named if token in projects and token != project}


# The stage that builds each project: the first whose targets need it.
stage_of = {}
for number, targets in enumerate(stages, 1):
    todo = [target for target in targets if target in projects]
    while todo:
        project = todo.pop()
        if project not in stage_of:
            stage_of[project] = number
            todo += dependencies(project)


def project_of(name):
    """The project a file of SuperBuild/ is of, or None: External_<project>.cmake, <project>_..."""
    match = re.fullmatch(r"External_(.+)\.cmake", name)
    if match:
        return match.group(1)
    named = [p for p in projects
             if name.lower().startswith(p.lower()) and not name[len(p):len(p) + 1].isalnum()]
    return max(named, key=len, default=None)


files = {number: [] for number in range(1, last + 1)}
for folder, _, names in os.walk(superbuild):
    for name in names:
        project = project_of(name)
        # A file that no project can be named for may be of use to any of them.
        files[stage_of.get(project, last) if project else 1].append(os.path.join(folder, name))

# SuperBuild.cmake, without the revisions of the sources added with Slicer_Remote_Add.
content = re.sub(r"\bSlicer_Remote_Add\s*\([^()]*\)",
                 lambda match: re.sub(r"\n[ \t]*GIT_TAG[^\n]*", "\n", match.group(0)),
                 read(os.path.join(source, "SuperBuild.cmake")).decode("utf8", "replace"))
key = hashlib.sha256(b"== common\n" + common + b"== SuperBuild.cmake\n" + content.encode("utf8")).hexdigest()
for number in range(1, last + 1):
    stage = hashlib.sha256(key.encode())
    for path in sorted(files[number], key=relative):
        stage.update(("== " + relative(path) + "\n").encode() + read(path))
    key = stage.hexdigest()[:16]
    built = sorted(p for p in projects if stage_of.get(p, last) == number)
    print("-" if number == last else key, *built)
