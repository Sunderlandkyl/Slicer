#!/usr/bin/env python3
"""Render a Markdown summary of a CTest JUnit report.

Usage: test-summary.py <test-results.xml> [<platform>]
"""

import sys
import xml.etree.ElementTree as ET


def main(argv):
    path = argv[1] if len(argv) > 1 else ""
    platform = argv[2] if len(argv) > 2 else ""
    print(f"## Tests {platform}".rstrip())
    try:
        root = ET.parse(path).getroot()
    except Exception as exc:  # noqa: BLE001 - the summary must never fail the job
        print(f"No test results could be read from `{path}`: {exc}")
        return 0

    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    total = failures = skipped = 0
    for suite in suites:
        total += int(suite.get("tests", 0))
        failures += int(suite.get("failures", 0)) + int(suite.get("errors", 0))
        skipped += int(suite.get("skipped", 0))

    print("")
    print("| Total | Passed | Failed | Skipped |")
    print("|---|---|---|---|")
    print(f"| {total} | {total - failures - skipped} | {failures} | {skipped} |")

    failed = [
        tc.get("name")
        for tc in root.iter("testcase")
        if tc.find("failure") is not None or tc.find("error") is not None
    ]
    if failed:
        print("")
        print("<details><summary>Failed tests</summary>")
        print("")
        for name in failed:
            print(f"- `{name}`")
        print("")
        print("</details>")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
