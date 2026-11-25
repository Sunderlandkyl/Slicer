#!/usr/bin/env python3
"""Write what an extension says of itself, as JSON, for extension-build.yml.

Usage: extension-description.py <build folder> <output file>

Reads the <Name>.s4ext the build wrote (lines of "key value"), for the
extensions catalog of the Slicer release. Writes nothing without such a file.
"""
import glob
import json
import os
import sys

# What a list of extensions shows; the rest is of use to the build only.
KEYS = ("homepage", "contributors", "category", "iconurl", "status", "description", "screenshoturls",
        "scmurl", "recommends", "enabled", "keywords")

folder, output = sys.argv[1:3]
found = sorted(glob.glob(os.path.join(folder, "*.s4ext")))
if not found:
    print(f"extension-description: no .s4ext in {folder}", file=sys.stderr)
    sys.exit(0)
described = {}
with open(found[0], encoding="utf8", errors="replace") as lines:
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition(" ")
        value = value.strip()
        if key in KEYS and value:
            described[key] = value
with open(output, "w", encoding="utf8") as out:
    json.dump(described, out, indent=1)
print(json.dumps(described, indent=1))
