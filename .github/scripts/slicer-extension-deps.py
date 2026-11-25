#!/usr/bin/env python3
"""Resolve the extensions an extension depends on, for extension-build.yml.

  slicer-extension-deps.py --source <checkout> --platform <platform>
      --slicer-commit <commit> --registry <url> [--index <url>]
      [--depends "<Name> ..."] --out <plan.json>

Each dependency (EXTENSION_DEPENDS of the CMakeLists.txt, or --depends, and
theirs in turn) is looked up in the registry (extension-repositories.json of
the Slicer release). A registered one must have published a build tree for
this platform and this Slicer commit (kind "tree"); there is no fallback. Any
other is built from the Slicer ExtensionsIndex (kind "source").

The plan lists the dependencies, dependencies first:

  {"depends": [<direct dependencies>],
   "dependencies": [
     {"name": ..., "kind": "tree", "repository": ..., "revision": ...,
      "package": <file>, "sha256": <of the package>,
      "tree": {"base": <url>, "files": [...], "sha256": {<file>: ...},
               "config_dir": ..., "subdirectory": ...}},
     {"name": ..., "kind": "source", "url": ..., "revision": ...,
      "description": <its ExtensionsIndex entry>}],
   "built_with": {<Name>: {"repository", "revision", "package", "sha256"}
                          | {"source", "revision"}}}
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request


def log(message):
    print(message, file=sys.stderr)


def get_json(url, attempts=4):
    """The JSON document at url; None when there is none (404)."""
    error = None
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            error = e
        except Exception as e:  # noqa: BLE001 - network errors are retried
            error = e
        time.sleep(5 * (attempt + 1))
    sys.exit(f"::error::could not read {url}: {error}")


def cmake_depends(source):
    """EXTENSION_DEPENDS of the CMakeLists.txt of an extension."""
    try:
        text = open(os.path.join(source, "CMakeLists.txt"), encoding="utf8", errors="replace").read()
    except OSError:
        return []
    match = re.search(r"set\s*\(\s*EXTENSION_DEPENDS\s+([^)]*)\)", text, re.I)
    if not match:
        return []
    names = [n.strip('"') for n in re.split(r"[\s;]+", match.group(1))]
    return [n for n in names if n and n != "NA"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--slicer-commit", required=True)
    parser.add_argument("--registry", default="")
    parser.add_argument("--index", default="https://raw.githubusercontent.com/Slicer/ExtensionsIndex/main")
    parser.add_argument("--depends", default=None)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    # The registry: name -> (repository, release url). It names the extension
    # each repository builds, so that one that has published nothing for this
    # platform yet is still known to be built by a repository.
    registered = {}
    registry = get_json(args.registry) if args.registry else None
    if registry:
        tag = registry.get("release_tag", "nightly")
        for entry in registry.get("repositories", []):
            repo = entry["repository"]
            registered[entry.get("name") or repo.split("/")[-1]] = (
                repo, f"https://github.com/{repo}/releases/download/{tag}")
        log(f"registry: {', '.join(f'{n} ({r})' for n, (r, _) in sorted(registered.items())) or 'no repository'}")
    elif args.registry:
        log(f"no registry at {args.registry}")

    direct = args.depends.split() if args.depends is not None else cmake_depends(args.source)
    order, state, problems = [], {}, []
    # Problems that time solves: the dependency is not built for this Slicer
    # yet. Told apart (exit status 3) from the ones that need someone.
    unready = []

    def resolve(name):
        if name in registered:
            repo, base = registered[name]
            described = get_json(f"{base}/extension-{args.platform}.json") or {}
            package = described.get("package")
            if not package:
                unready.append(f"{name} ({repo}) has published nothing for {args.platform}")
                return None, []
            if package.get("slicer_commit") != args.slicer_commit:
                unready.append(
                    f"{name} ({repo}) is built against Slicer {str(package.get('slicer_commit'))[:10]}, "
                    f"not {args.slicer_commit[:10]}: it has to be built against this Slicer first")
                return None, []
            tree = package.get("build_tree")
            if not tree or not tree.get("files"):
                problems.append(
                    f"{name} ({repo}) does not publish its build tree: its workflow has to set publish-build-tree")
                return None, []
            return {
                "name": name, "kind": "tree", "repository": repo,
                "revision": package.get("revision") or described.get("revision"),
                "package": package.get("file"), "sha256": package.get("sha256", ""),
                "tree": dict(tree, base=base),
            }, list(package.get("depends") or [])
        described = get_json(f"{args.index}/{name}.json")
        if not described or not described.get("scm_url"):
            problems.append(f"{name} is neither in the registry nor in the ExtensionsIndex")
            return None, []
        return {
            "name": name, "kind": "source", "url": described["scm_url"],
            "revision": described.get("scm_revision") or "HEAD", "description": described,
        }, list(described.get("build_dependencies") or [])

    def visit(name, chain):
        if state.get(name) == "done":
            return
        if state.get(name) == "visiting":
            sys.exit("::error::dependency cycle: " + " -> ".join(chain + [name]))
        state[name] = "visiting"
        entry, depends = resolve(name)
        for dependency in depends:
            if dependency and dependency != "NA":
                visit(dependency, chain + [name])
        state[name] = "done"
        if entry:
            order.append(entry)

    for name in direct:
        visit(name, [])
    if problems:
        for problem in problems + unready:
            print(f"::error::{problem}")
        sys.exit(1)
    if unready:
        for problem in unready:
            print(f"::warning::{problem}")
        sys.exit(3)

    built_with = {}
    for entry in order:
        if entry["kind"] == "tree":
            built_with[entry["name"]] = {k: entry[k] for k in ("repository", "revision", "package", "sha256")}
            log(f"{entry['name']}: the build tree of {entry['repository']} at {entry['revision']}")
        else:
            built_with[entry["name"]] = {"source": entry["url"], "revision": entry["revision"]}
            log(f"{entry['name']}: built from {entry['url']} {entry['revision']} (ExtensionsIndex)")
    with open(args.out, "w", encoding="utf8") as f:
        json.dump({"depends": direct, "dependencies": order, "built_with": built_with}, f, indent=1)


if __name__ == "__main__":
    main()
