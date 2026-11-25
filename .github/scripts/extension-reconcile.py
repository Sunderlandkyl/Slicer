#!/usr/bin/env python3
"""Bring the extensions up to date with the Slicer nightly, in dependency order.

Run by extension-reconcile.yml; what it does is described in
Docs/developer_guide/build_instructions/github_actions.md. It keeps no memory
of events: each pass works out what to do from what is published, so that
running it late, twice, or after a failure converges on the same state. For
each platform of the nightly release, it starts the build of every extension
that is not current (built against that Slicer commit, from the head of its
branch, against the packages its dependencies publish now) once the extensions
it depends on are, and publishes extensions-catalog.json when that changes.

Environment: GH_TOKEN and GITHUB_REPOSITORY (the release of this repository,
and starting itself), APP_ID and APP_PRIVATE_KEY (the app: starting the
builds), RELEASE_TAG, NIGHTLY_WORKFLOW, NIGHTLY_WEB_WORKFLOW, REFS (JSON
{repository: branch}, to build from another branch than the default one),
TRUSTED_FORKS, EXTENSIONS_INDEX, RETRY (true: start again what failed for the
same inputs), WAIT (true: stay for as long as builds are going), LOOK_SECONDS,
DEADLINE_MINUTES, SELF_WORKFLOW and SELF_REF (the run that carries on),
START_SITES, FORCE_SITES and REASON (what started the run, for its summary).
"""
import base64
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

API = "https://api.github.com"
PLATFORMS = ("linux", "macos", "macos-arm64", "windows", "web")
STARTED = time.time()


def env(name, default=""):
    return os.environ.get(name, default)


SLICER = env("GITHUB_REPOSITORY")
TAG = env("RELEASE_TAG", "nightly")
WORKFLOW = env("NIGHTLY_WORKFLOW", "slicer-nightly-build.yml")
WEB_WORKFLOW = env("NIGHTLY_WEB_WORKFLOW", "slicer-nightly-web.yml")
RETRY = env("RETRY") == "true"
# false: one step, which starts what can be started and stops; the relay of the
# app's webhook (.github/extension-webhook) then has to start the next one.
WAIT = env("WAIT") == "true"
DEADLINE = STARTED + 60 * float(env("DEADLINE_MINUTES", "300"))
NOT_PUBLISHED = "not published for this platform"
# How often a pass that is waiting for builds looks for what no build of its
# own tells it: commits to the extensions, builds they started, a new Slicer.
LOOK_SECONDS = float(env("LOOK_SECONDS", "90"))
# How many times the build of an extension is started for the same Slicer,
# commit and dependencies, when it fails.
MAX_TRIES = 2
REFS = json.loads(env("REFS") or "{}")
# One <Name>.json per reviewed extension, naming its repository (scm_url) and
# its tier (1 to 5). Any other extension is of tier 0: not reviewed.
EXTENSIONS_INDEX = env("EXTENSIONS_INDEX") or "https://raw.githubusercontent.com/Slicer/ExtensionsIndex/main"
# {"fork": "repository"}: a fork that has the tier of the repository it copies.
TRUSTED_FORKS = {k.lower(): v for k, v in json.loads(env("TRUSTED_FORKS") or "{}").items()}


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S+00:00")


def log(message):
    print(message, flush=True)


def request(url, token=None, method="GET", data=None, attempts=4, raw=False):
    """(status, body) of a request; waits and tries again when asked to slow down."""
    headers = {"User-Agent": "slicer-extension-reconcile"}
    if url.startswith(API):
        headers["Accept"] = "application/vnd.github+json"
        headers["X-GitHub-Api-Version"] = "2022-11-28"
        if token:
            headers["Authorization"] = f"Bearer {token}"
    body = json.dumps(data).encode() if data is not None else None
    status, text = 0, b""
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, data=body, headers=headers, method=method),
                                        timeout=60) as response:
                status, text = response.status, response.read()
        except urllib.error.HTTPError as e:
            status, text = e.code, e.read()
            wait = e.headers.get("Retry-After")
            if status in (403, 429) and (wait or b"secondary rate limit" in text.lower()):
                time.sleep(int(wait or 60))
                continue
            if status >= 500:
                time.sleep(5 * (attempt + 1))
                continue
        except Exception as e:  # noqa: BLE001 - network errors are retried
            status, text = 0, str(e).encode()
            time.sleep(5 * (attempt + 1))
            continue
        break
    if raw:
        return status, text
    try:
        return status, json.loads(text) if text else None
    except ValueError:
        return status, None


def repository_of(url):
    """owner/name, in lower case, of the address of a GitHub repository; "" when it is not one."""
    match = re.match(r"^(?:https?://|git://|git@)(?:www\.)?github\.com[:/]+([^/\s]+)/([^/\s]+?)(?:\.git)?/?$",
                     (url or "").strip(), re.I)
    return f"{match.group(1)}/{match.group(2)}".lower() if match else ""


def tier_of(repository, described):
    """(tier, the repository it is reviewed as) of an extension, from its description in the ExtensionsIndex.

    The tier is that of the repository, not of the name: a repository that
    takes the name of a reviewed extension does not take its tier.
    """
    reviewed = repository_of((described or {}).get("scm_url"))
    if not reviewed:
        return 0, ""
    built = repository.lower()
    stands_for = TRUSTED_FORKS.get(built, "").lower()
    if reviewed not in (built, stands_for):
        return 0, ""
    try:
        tier = int(described.get("tier", 1))
    except (TypeError, ValueError):
        tier = 1
    # What is in the index is of tier 1 at least.
    return max(tier, 1), ("" if reviewed == built else reviewed)


def release_json(repository, *patterns):
    """The JSON files of the release TAG of a repository that match the patterns: {name: document}.

    Read through the API (gh): the public download address of an asset can go
    on serving, for a while, the file it held before it was replaced."""
    folder = tempfile.mkdtemp()
    arguments = ["release", "download", TAG, "-R", repository, "-D", folder, "--clobber"]
    for pattern in patterns:
        arguments += ["-p", pattern]
    found = {}
    for attempt in range(4):
        result = subprocess.run(["gh", *arguments], capture_output=True, text=True)
        if result.returncode == 0 or "no assets" in result.stderr.lower() or "release not found" in result.stderr.lower():
            break
        time.sleep(5 * (attempt + 1))
    else:
        # It could not be read, which is not "there is none": the pass stops
        # rather than decide on what it did not read.
        raise RuntimeError(f"could not read {', '.join(patterns)} of {repository}: {result.stderr.strip()[:300]}")
    for name in os.listdir(folder):
        try:
            with open(os.path.join(folder, name), encoding="utf8") as document:
                found[name] = json.load(document)
        except ValueError:
            pass
    return found


class App:
    """The Slicer nightly GitHub App: the repositories it is installed in, and their tokens."""

    def __init__(self, app_id, private_key):
        self.app_id = app_id
        self.key = tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False)
        self.key.write(private_key.strip() + "\n")
        self.key.close()
        self.jwt, self.jwt_made = "", 0
        self.installation_of, self.tokens = {}, {}
        self.complete = True

    def _jwt(self):
        # Valid for ten minutes at most; made again after seven.
        if self.jwt and time.time() - self.jwt_made < 420:
            return self.jwt
        b64 = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=").decode()
        made = int(time.time())
        unsigned = (b64(b'{"alg":"RS256","typ":"JWT"}') + "."
                    + b64(json.dumps({"iat": made - 60, "exp": made + 540, "iss": str(self.app_id)}).encode()))
        signature = subprocess.run(["openssl", "dgst", "-sha256", "-sign", self.key.name, "-binary"],
                                   input=unsigned.encode(), capture_output=True, check=True).stdout
        self.jwt, self.jwt_made = unsigned + "." + b64(signature), made
        print(f"::add-mask::{self.jwt}")
        return self.jwt

    def _installation_token(self, installation):
        token, made = self.tokens.get(installation, ("", 0))
        # Valid for an hour; made again after forty minutes.
        if not token or time.time() - made > 2400:
            status, body = request(f"{API}/app/installations/{installation}/access_tokens", self._jwt(), "POST")
            if status != 201:
                return None
            token = body["token"]
            print(f"::add-mask::{token}")
            self.tokens[installation] = (token, time.time())
        return token

    def token(self, repository):
        return self._installation_token(self.installation_of[repository])

    def repositories(self):
        """{repository: default branch} of every repository the app is installed in."""
        found, page, installations = {}, 1, []
        while True:
            status, batch = request(f"{API}/app/installations?per_page=100&page={page}", self._jwt())
            if status != 200:
                sys.exit(f"::error::Could not list the installations of app {self.app_id} (HTTP {status}); "
                         "are SLICER_NIGHTLY_APP_ID and SLICER_NIGHTLY_APP_PRIVATE_KEY from the same app?")
            if not batch:
                break
            installations += [i["id"] for i in batch]
            page += 1
        for installation in installations:
            token = self._installation_token(installation)
            if not token:
                log(f"::warning::No token for installation {installation}")
                self.complete = False
                continue
            page = 1
            while True:
                status, batch = request(f"{API}/installation/repositories?per_page=100&page={page}", token)
                if status != 200:
                    log(f"::warning::Could not list the repositories of installation {installation}")
                    self.complete = False
                    break
                if not batch["repositories"]:
                    break
                for repository in batch["repositories"]:
                    found[repository["full_name"]] = repository["default_branch"]
                    self.installation_of[repository["full_name"]] = installation
                page += 1
        return found


class Extension:
    def __init__(self, repository, ref):
        self.repository, self.ref = repository, ref
        self.name = repository.split("/")[-1]
        self.head = None
        self.depends = []       # EXTENSION_DEPENDS, at the head
        self.packages = {}      # platform: "package" of its extension-<platform>.json
        self.web = None         # its entry of SlicerWeb's extension index
        self.tier = 0           # of the ExtensionsIndex (1 to 5); 0: not reviewed
        self.reviewed_as = ""   # the repository it is a trusted fork of, when its tier is that one's
        self.attempts = {}      # platform: {key, run, conclusion, date}, of this pass or an earlier one
        self.note = ""
        self.one_run = False    # its workflow takes no `platforms` input: one run builds every platform
        self.known = set()      # the platforms it is known to have published

    def read_source(self, app):
        """The head of its branch, its name and what it depends on."""
        status, commit = request(f"{API}/repos/{self.repository}/commits/{self.ref}", app.token(self.repository))
        if status != 200:
            status, commit = request(f"{API}/repos/{self.repository}/commits/{self.ref}", env("GH_TOKEN"))
        # Read again during a pass: a failure keeps what was read before.
        if status != 200:
            return
        if commit["sha"] == self.head:
            return
        self.head = commit["sha"]
        status, text = request(f"https://raw.githubusercontent.com/{self.repository}/{self.head}/CMakeLists.txt", raw=True)
        if status != 200:
            return
        text = text.decode("utf8", "replace")
        match = re.search(r"^\s*project\(\s*([A-Za-z0-9_]+)", text, re.M)
        if match:
            self.name = match.group(1)
        match = re.search(r"set\s*\(\s*EXTENSION_DEPENDS\s+([^)]*)\)", text, re.I)
        names = [n.strip('"') for n in re.split(r"[\s;]+", match.group(1))] if match else []
        self.depends = [n for n in names if n and n != "NA"]

    def read_tier(self, earlier):
        """Its tier. When the ExtensionsIndex cannot be read, the tier it had, in the catalog of the pass before."""
        status, described = request(f"{EXTENSIONS_INDEX}/{self.name}.json")
        if status == 200 and isinstance(described, dict):
            self.tier, self.reviewed_as = tier_of(self.repository, described)
        elif status == 404:
            self.tier, self.reviewed_as = 0, ""
        else:
            before = earlier.get(self.name) or {}
            same_repository = before.get("repository") == self.repository
            self.tier = int(before.get("tier") or 0) if same_repository else 0
            self.reviewed_as = (before.get("reviewed_as") or "") if same_repository else ""
            log(f"::warning::{self.name}: the ExtensionsIndex could not be read ({status}); tier {self.tier}, as before")

    def read_published(self, platforms):
        """What it published for the platforms."""
        names = {platform: f"extension-{platform}.json" for platform in platforms}
        files = release_json(self.repository, *names.values())
        # A description is replaced by removing it and uploading the new one:
        # in between it is not there, which does not mean that the platform
        # was never published. One that was there before is looked for again.
        for _ in range(4):
            missing = [p for p in platforms if p in self.known and names[p] not in files]
            if not missing:
                break
            time.sleep(4)
            files.update(release_json(self.repository, *[names[p] for p in missing]))
        for platform in platforms:
            described = files.get(names[platform])
            if described and isinstance(described.get("package"), dict):
                self.packages[platform] = described["package"]
                self.known.add(platform)
                if platform == "web":
                    self.web = described.get("web")
            else:
                self.packages.pop(platform, None)
                self.known.discard(platform)

    def package(self, platform):
        return self.packages.get(platform)


class State:
    def __init__(self, targets, extensions):
        self.targets = targets
        self.by_name = {e.name: e for e in extensions}
        self.extensions = extensions
        self._reason = {}

    def registered(self, extension):
        """The extensions it depends on that a repository builds."""
        return [self.by_name[d] for d in extension.depends if d in self.by_name and d != extension.name]

    def reason(self, extension, platform, chain=(), heads=True):
        """Why the extension is not current on a platform; "" when it is.

        heads=False: whether it can be offered, that is, built against the
        target Slicer and the packages its dependencies publish, whatever
        commit of each it was built from. A new commit is a reason to build
        again, not to withdraw the packages that are consistent meanwhile."""
        key = (extension.name, platform, heads)
        if key in self._reason:
            return self._reason[key]
        if extension.name in chain:
            return "dependency cycle: " + " -> ".join(chain + (extension.name,))
        package, target = extension.package(platform), self.targets.get(platform)
        reason = ""
        if not package:
            reason = NOT_PUBLISHED
        elif package.get("slicer_commit") != target:
            reason = f"built against Slicer {str(package.get('slicer_commit'))[:10]}, not {str(target)[:10]}"
        elif heads and extension.head and package.get("revision") != extension.head:
            reason = f"built from {str(package.get('revision'))[:10]}, not {extension.head[:10]}"
        else:
            for name, used in sorted((package.get("built_with") or {}).items()):
                dependency = self.by_name.get(name)
                if not dependency:
                    continue  # nothing publishes it: built from source with the extension
                theirs = dependency.package(platform)
                if not theirs or not used.get("sha256") or theirs.get("sha256") != used.get("sha256"):
                    reason = f"built against another package of {name} than the one it publishes"
                    break
                below = self.reason(dependency, platform, chain + (extension.name,), heads)
                if below:
                    reason = f"{name} is not current ({below})"
                    break
        self._reason[key] = reason
        return reason

    def key(self, extension, platform):
        """What a build of the extension for a platform is made from: started once per key."""
        inputs = {
            "slicer": self.targets.get(platform), "head": extension.head,
            "dependencies": {d.name: (d.package(platform) or {}).get("sha256") for d in self.registered(extension)},
        }
        return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:16]

    def settled(self, extension, platform):
        """Nothing more will be done about it in this pass: current, or started for what there is."""
        if not self.reason(extension, platform):
            return True
        attempt = extension.attempts.get(platform)
        if not attempt or attempt.get("key") != self.key(extension, platform):
            return False
        # Not while its build is running: what depends on it waits for it.
        if attempt.get("conclusion") == "running":
            return False
        # A build that succeeded and left it as it was (the extension does not
        # build that platform) is settled. One that failed is tried once more:
        # runners, downloads and tests fail for reasons that are not its own.
        return attempt.get("conclusion") == "success" or attempt.get("tries", 1) >= MAX_TRIES

    def attempt(self, extension, platform):
        """The record of a build about to be started: how many there have been for the same inputs."""
        key, earlier = self.key(extension, platform), extension.attempts.get(platform) or {}
        tries = earlier.get("tries", 1) + 1 if earlier.get("key") == key else 1
        return {"key": key, "conclusion": "not started", "date": now(), "tries": tries}

    def startable(self, extension, platform):
        # Only against dependencies that are current: after one that failed,
        # what depends on it is left as it is.
        return (not extension.note and not self.settled(extension, platform)
                and all(not self.reason(d, platform) for d in self.registered(extension)))


def runs_in_progress(extension, token):
    """The builds of the branch that have not ended, whatever their state: queued, waiting for
    a runner or an approval, pending behind another, or running."""
    status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                           f"?branch={extension.ref}&per_page=50", token)
    if status != 200:
        return []
    # Those that publish: the ones started here or by hand, and the one of a
    # commit to the branch, which the extension starts itself.
    return [r for r in body["workflow_runs"]
            if r["status"] != "completed" and r["event"] in ("workflow_dispatch", "push")]


def platform_jobs(extension, run_id, token):
    """[(platform, conclusion)] of the jobs of a run that build a platform, which their names say."""
    status, body = request(f"{API}/repos/{extension.repository}/actions/runs/{run_id}/jobs?per_page=100", token)
    found = []
    for job in (body or {}).get("jobs", []) if status == 200 else []:
        match = re.search(r"(?:^|/ )(linux|macos|macos-arm64|windows|web)$", job.get("name") or "")
        if match:
            found.append((match.group(1), job.get("conclusion")))
    return found


def run_platforms(extension, run_id, token):
    """The platforms a run builds; None while they are not known."""
    return {platform for platform, conclusion in platform_jobs(extension, run_id, token)
            if conclusion != "skipped"} or None


def push_failures(extension, token):
    """{platform: run url} of the platforms that failed in the build of the commit at the head of the branch."""
    status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                           f"?head_sha={extension.head}&event=push&status=completed&per_page=5", token)
    runs = [r for r in (body or {}).get("workflow_runs", []) if r.get("conclusion") == "failure"] if status == 200 else []
    if not runs:
        return {}
    return {platform: runs[0]["html_url"] for platform, conclusion in platform_jobs(extension, runs[0]["id"], token)
            if conclusion == "failure"}


def dispatch(extension, app, platforms, claimed):
    """Start the build of an extension for platforms; (run id, run url), or None.

    The workflow of the extension takes the platforms to build as its
    `platforms` input. One that does not is started without, and builds them
    all in one run."""
    token = app.token(extension.repository)
    url = f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/dispatches"
    since = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(seconds=30)
    data = {"ref": extension.ref, "return_run_details": True}
    if not extension.one_run:
        status, body = request(url, token, "POST", dict(data, inputs={"platforms": ",".join(platforms)}))
        if status == 422:
            log(f"{extension.name}: its workflow takes no platforms input; one run builds every platform")
            extension.one_run = True
    if extension.one_run:
        status, body = request(url, token, "POST", data)
    if status not in (200, 204):
        log(f"::warning::{extension.repository}: could not start {WORKFLOW} on {extension.ref} (HTTP {status}: {body})")
        return None
    if isinstance(body, dict) and body.get("workflow_run_id"):
        return body["workflow_run_id"], body.get("html_url") or f"https://github.com/{extension.repository}/actions/runs/{body['workflow_run_id']}"
    # The run is not named in the answer: the one that appears, that no other start has claimed.
    for _ in range(40):
        time.sleep(8)
        status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                               f"?branch={extension.ref}&event=workflow_dispatch&per_page=20", token)
        for run in reversed((body or {}).get("workflow_runs", []) if status == 200 else []):
            created = datetime.datetime.fromisoformat(run["created_at"].replace("Z", "+00:00"))
            if created >= since and run["id"] not in claimed:
                return run["id"], run["html_url"]
    log(f"::warning::{extension.repository}: started {WORKFLOW}, but its run did not appear")
    return None


def catalog_of(state):
    platforms, withheld = {}, {}
    for platform, target in state.targets.items():
        offered, held = {}, {}
        for extension in sorted(state.extensions, key=lambda e: e.name):
            reason = state.reason(extension, platform, heads=False)
            package = extension.package(platform)
            if reason:
                # An extension that has never built a platform is not withheld from it.
                if package or extension.attempts.get(platform):
                    held[extension.name] = reason
                continue
            base = f"https://github.com/{extension.repository}/releases/download/{TAG}"
            entry = {
                "repository": extension.repository, "revision": package.get("revision"),
                "file": package["file"], "url": f"{base}/{package['file']}", "sha256": package.get("sha256", ""),
                "depends": sorted(n for n in (package.get("built_with") or {}) if n in state.by_name),
                "tests": package.get("tests", ""),
                "tier": extension.tier,
            }
            if package.get("icon"):
                entry["icon"] = f"{base}/{package['icon']}"
            # For Slicer's Extensions Manager.
            if package.get("slicer_revision"):
                entry["slicer_revision"] = package["slicer_revision"]
            if package.get("date"):
                entry["date"] = package["date"]
            if package.get("metadata"):
                entry["metadata"] = package["metadata"]
            if platform == "web" and extension.web:
                entry["web"] = extension.web
            offered[extension.name] = entry
        platforms[platform] = {"slicer_commit": target, "extensions": offered}
        withheld[platform] = held
    return {
        "date": now(), "slicer_repository": SLICER, "release_tag": TAG,
        "description": "The extensions built against the Slicer builds of this release, by their own repositories: "
                       "per platform, those whose package is built against this Slicer commit and against the very "
                       "packages, listed here too, of the extensions it depends on. tier is that of the Slicer "
                       "ExtensionsIndex (1 to 5) when the repository is the one it names; 0: not reviewed.",
        "platforms": platforms, "withheld": withheld,
        "extensions": {e.name: {"repository": e.repository, "branch": e.ref, "head": e.head, "depends": e.depends,
                                "tier": e.tier, **({"reviewed_as": e.reviewed_as} if e.reviewed_as else {}),
                                "attempts": e.attempts, **({"note": e.note} if e.note else {})}
                       for e in sorted(state.extensions, key=lambda e: e.name)},
    }


def same(a, b):
    strip = lambda d: json.dumps({k: v for k, v in (d or {}).items() if k != "date"}, sort_keys=True)
    return strip(a) == strip(b)


def upload(name, document):
    path = os.path.join(tempfile.mkdtemp(), name)
    with open(path, "w", encoding="utf8") as f:
        json.dump(document, f, indent=1)
    subprocess.run(["gh", "release", "upload", TAG, path, "--clobber", "-R", SLICER],
                   check=True, capture_output=True, text=True)
    log(f"{name}: published")


def main():
    if not env("APP_PRIVATE_KEY"):
        log("::warning::SLICER_NIGHTLY_APP_PRIVATE_KEY is not set; nothing to reconcile.")
        return
    app = App(env("APP_ID"), env("APP_PRIVATE_KEY"))

    # The catalog of the pass before: what it offered, and what it started.
    published = release_json(SLICER, "extensions-catalog.json").get("extensions-catalog.json")
    earlier = (published or {}).get("extensions") or {}
    # The Slicer commit last seen for each platform.
    seen = {p: v.get("slicer_commit") for p, v in ((published or {}).get("platforms") or {}).items() if v.get("slicer_commit")}

    def targets_now():
        targets = {}
        files = release_json(SLICER, "manifest-*.json", "slicerweb-manifest.json")
        for platform in PLATFORMS:
            manifest = files.get("slicerweb-manifest.json" if platform == "web" else f"manifest-{platform}.json")
            if manifest and manifest.get("commit"):
                targets[platform] = seen[platform] = manifest["commit"]
            elif platform in seen:
                # A platform of Slicer takes its manifest away while it
                # replaces its files, for minutes: the platform is not gone,
                # and its extensions are not to be dropped from the catalog.
                # It stays as it was until its manifest is back.
                targets[platform] = seen[platform]
        return targets

    targets = targets_now()
    if not targets:
        log(f"::notice::The {TAG} release of {SLICER} holds no Slicer build; nothing to reconcile.")
        return
    log("Slicer: " + ", ".join(f"{p} {c[:10]}" for p, c in targets.items()))

    extensions, sites = [], []
    for repository, branch in sorted(app.repositories().items()):
        token = app.token(repository)
        if request(f"{API}/repos/{repository}/actions/workflows/{WORKFLOW}", token)[0] == 200:
            extensions.append(Extension(repository, REFS.get(repository, branch)))
        if request(f"{API}/repos/{repository}/actions/workflows/{WEB_WORKFLOW}", token)[0] == 200:
            sites.append((repository, REFS.get(repository, branch)))
    log(f"Extensions: {', '.join(e.repository for e in extensions) or 'none'}; sites: {', '.join(s for s, _ in sites) or 'none'}")
    for extension in extensions:
        extension.read_source(app)
        extension.read_tier(earlier)
        # Where the pass before found a package of it.
        extension.known = {p for p in PLATFORMS
                           if extension.name in ((published or {}).get("platforms", {}).get(p, {}).get("extensions") or {})
                           or ((published or {}).get("withheld", {}).get(p, {}).get(extension.name) or NOT_PUBLISHED) != NOT_PUBLISHED}
        extension.read_published(targets)
        status = request(f"{API}/repos/{extension.repository}/contents/.github/workflows/{WORKFLOW}?ref={extension.ref}",
                         app.token(extension.repository))[0]
        if status == 404:
            extension.note = f"no {WORKFLOW} on {extension.ref}: not built"
    if app.complete:
        registry = {
            "date": now(), "slicer_repository": SLICER, "release_tag": TAG,
            "description": "Repositories building Slicer extensions against this repository: their packages are in "
                           "the release nightly of each, with extension-<platform>.json, which says what each was "
                           "built against. extensions-catalog.json lists the ones that are consistent with each other.",
            "repositories": [{"repository": e.repository, "name": e.name, "default_branch": e.ref} for e in extensions],
        }
        if not same(registry, release_json(SLICER, "extension-repositories.json").get("extension-repositories.json")):
            upload("extension-repositories.json", registry)

    state = State(targets, extensions)
    # What an earlier pass started, for the same inputs, is not started again.
    running = {}    # run id: (extension, platforms, url)
    # What this pass did, for its summary.
    did = {"started": [], "finished": [], "left": [], "site": []}
    for extension in extensions:
        for platform, attempt in ((earlier.get(extension.name) or {}).get("attempts") or {}).items():
            if not RETRY and platform in targets and attempt.get("key") == state.key(extension, platform):
                # What an earlier step left running is taken up as if started here.
                if attempt.get("conclusion") == "not started":
                    continue  # never started: started again
                if attempt.get("conclusion") == "running":
                    run = (attempt.get("run") or "").rstrip("/").split("/")[-1]
                    if not run.isdigit():
                        continue  # never started: started again
                    running.setdefault(int(run), (extension, [], attempt["run"]))[1].append(platform)
                elif attempt.get("conclusion") == "failure" and not state.reason(extension, platform):
                    # Recorded as failed for its tests alone, before a build
                    # that publishes was told from one that does not.
                    attempt = dict(attempt, conclusion="success")
                extension.attempts[platform] = attempt
    # The build of a commit, which the extension starts itself, is the first
    # try of a platform it failed for: one more follows, not two.
    for extension in extensions:
        waiting = [p for p in targets if state.reason(extension, p) and p not in extension.attempts]
        if waiting and extension.head:
            for platform, run in push_failures(extension, app.token(extension.repository)).items():
                if platform in waiting:
                    extension.attempts[platform] = dict(state.attempt(extension, platform), conclusion="failure", run=run)
                    log(f"{extension.name} ({platform}): the build of its commit failed ({run}); one more try")
    # Builds not started here (of a commit to the extension, by hand, or by a
    # pass that was cancelled) are left to finish: nothing is started meanwhile
    # for the platforms they build, which their jobs say.
    foreign = {}    # repository: {run id: the platforms it builds, None while not known}

    def held(extension, platform):
        """Whether a build not started here is building that platform of the extension."""
        return any(platforms is None or platform in platforms
                   for platforms in foreign.get(extension.repository, {}).values())

    def look_for_foreign(extension, message):
        """Take note of the builds of the extension that are going and are not known yet."""
        known = set(running) | set(foreign.get(extension.repository, {}))
        going = [r for r in runs_in_progress(extension, app.token(extension.repository)) if r["id"] not in known]
        if going:
            foreign.setdefault(extension.repository, {}).update(
                {r["id"]: run_platforms(extension, r["id"], app.token(extension.repository)) for r in going})
            log(f"{extension.name}: {message} ({', '.join(r['html_url'] for r in going)})")
            did["left"].append(extension.name + " (" + ", ".join(f"[run]({r['html_url']})" for r in going) + ")")
    for extension in extensions:
        look_for_foreign(extension, "waiting for the builds already running")

    web_of = lambda c: json.dumps((c or {}).get("platforms", {}).get("web"), sort_keys=True)
    sites_seen = web_of(published) if env("FORCE_SITES") != "true" else ""

    def start_sites(catalog):
        nonlocal sites_seen
        if env("START_SITES", "true") != "true" or web_of(catalog) == sites_seen:
            return
        sites_seen = web_of(catalog)
        for repository, branch in sites:
            status, _ = request(f"{API}/repos/{repository}/actions/workflows/{WEB_WORKFLOW}/dispatches",
                                app.token(repository), "POST", {"ref": branch})
            log(f"{repository}: {'started' if status in (200, 204) else f'could not start (HTTP {status})'} {WEB_WORKFLOW} on {branch}")
            if status in (200, 204):
                did["site"].append(repository)

    def publish_catalog():
        """Publish the catalog of the state when it differs from the one published; (catalog, whether it did)."""
        nonlocal published
        catalog = catalog_of(state)
        differs = not same(catalog, published)
        if differs:
            upload("extensions-catalog.json", catalog)
            published = catalog
        return catalog, differs

    started, again, checked_targets = 0, False, time.time()
    unstarted = {}  # (repository, platform): how many times its build could not be started
    # What is published may already differ from the catalog: it is made at once.
    first = True
    while True:
        changed, first = first, False
        for run_id, (extension, platforms, url) in list(running.items()):
            status, body = request(f"{API}/repos/{extension.repository}/actions/runs/{run_id}", app.token(extension.repository))
            if status == 200 and body["status"] == "completed":
                extension.read_published(platforms)
                for platform in platforms:
                    # A run that published the platform and then failed for
                    # its tests is not a build that failed.
                    package = extension.package(platform) or {}
                    built = (package.get("slicer_commit") == targets.get(platform)
                             and (not extension.head or package.get("revision") == extension.head))
                    outcome = "success" if built else body["conclusion"]
                    extension.attempts[platform]["conclusion"] = outcome
                    if built and package.get("tests"):
                        extension.attempts[platform]["tests"] = package["tests"]
                    outcome = ("published" if built else body["conclusion"]) + (
                        ", tests " + package["tests"] if built and package.get("tests") else "")
                    log(f"{extension.name} ({platform}): {outcome} {url}")
                    did["finished"].append(f"[{extension.name} {platform}]({url}): {outcome}")
                del running[run_id]
                changed = True
        for extension in extensions:
            if foreign.get(extension.repository):
                # Such a build publishes its platforms one by one.
                before = json.dumps(extension.packages, sort_keys=True)
                extension.read_published(targets)
                changed = changed or json.dumps(extension.packages, sort_keys=True) != before
            for run_id in list(foreign.get(extension.repository, {})):
                status, body = request(f"{API}/repos/{extension.repository}/actions/runs/{run_id}", app.token(extension.repository))
                if status == 200 and body["status"] != "completed" and foreign[extension.repository][run_id] is None:
                    # Its jobs had not been made yet: which platforms it builds.
                    foreign[extension.repository][run_id] = run_platforms(extension, run_id, app.token(extension.repository))
                if status != 200 or body["status"] == "completed":
                    del foreign[extension.repository][run_id]
                    extension.read_published(targets)
                    changed = True
        # Nothing wakes another pass while this one is going: it looks itself
        # for commits to the extensions, the builds they start, and a new Slicer.
        if time.time() - checked_targets > LOOK_SECONDS:
            checked_targets = time.time()
            for extension in extensions:
                head = extension.head
                extension.read_source(app)
                if extension.head != head:
                    log(f"{extension.name}: new commit {extension.head[:10]} on {extension.ref}")
                    changed = True
                look_for_foreign(extension, "a build not started here is running")
            latest = targets_now()
            if latest and latest != targets:
                log("Slicer: " + ", ".join(f"{p} {c[:10]}" for p, c in latest.items()))
                targets, changed = latest, True
                for extension in extensions:
                    extension.read_published(targets)
        state = State(targets, extensions)
        busy = {(e.repository, p) for e, platforms, _ in running.values() for p in platforms}
        todo = [(e, p) for e in extensions for p in targets
                if state.startable(e, p) and (e.repository, p) not in busy and not held(e, p)
                and unstarted.get((e.repository, p), 0) < 3]
        if todo and time.time() > DEADLINE:
            again = True
            todo = []
        # One run per platform, so that no platform waits for another; one for
        # all the platforms of an extension whose workflow cannot be told which.
        groups, retry_soon = {}, False
        for extension, platform in todo:
            groups.setdefault((extension.repository, None if extension.one_run else platform), (extension, []))[1].append(platform)
        for extension, platforms in groups.values():
            if extension.one_run and any(e is extension for e, _, _ in running.values()):
                continue
            log(f"{extension.name}: starting {', '.join(platforms)} "
                f"({'; '.join(f'{p}: {state.reason(extension, p)}' for p in platforms)})")
            for platform in platforms:
                extension.attempts[platform] = state.attempt(extension, platform)
            run = dispatch(extension, app, platforms, set(running))
            started += 1
            if extension.one_run and len(platforms) == 1:
                # It was found out by this start: the run builds the other platforms too.
                group = list(platforms)
                platforms = [p for p in targets if p in platforms
                             or (state.startable(extension, p) and (extension.repository, p) not in busy)]
                for platform in platforms:
                    if platform not in group:
                        extension.attempts[platform] = state.attempt(extension, platform)
            if run:
                running[run[0]] = (extension, platforms, run[1])
                did["started"].append(f"[{extension.name} {', '.join(platforms)}]({run[1]})")
                for platform in platforms:
                    extension.attempts[platform].update(run=run[1], conclusion="running")
                    unstarted.pop((extension.repository, platform), None)
            else:
                # Not started, so nothing will say that it ended: it is tried
                # again, a few times in this pass, and by the next pass.
                for platform in platforms:
                    # As it was before this start, which did not happen.
                    before = dict(extension.attempts.pop(platform, None) or {})
                    if before.get("tries", 1) > 1:
                        extension.attempts[platform] = dict(before, tries=before["tries"] - 1, conclusion="failure")
                    unstarted[(extension.repository, platform)] = unstarted.get((extension.repository, platform), 0) + 1
                    retry_soon = retry_soon or unstarted[(extension.repository, platform)] < 3
                # Unless it did start, and only its run was not found: that
                # one is waited for like any build not started here.
                lost = [r["id"] for r in runs_in_progress(extension, app.token(extension.repository)) if r["id"] not in running]
                if lost:
                    foreign[extension.repository] = {run_id: None for run_id in lost}
            time.sleep(1)
        if changed:
            # What is consistent now is offered now: a platform does not wait for the others.
            start_sites(publish_catalog()[0])
        if not running and not todo and not any(foreign.values()):
            break
        if not WAIT and not retry_soon:
            # A build that could not be started will not end, and so will not
            # start the next step: this one stays to try it again.
            break
        if time.time() > DEADLINE + 600:
            again = True
            break
        time.sleep(30)

    # A build still going when time ran out is looked at again by the next
    # pass; one a step started is looked up by the next step.
    for extension, platforms, _ in (running.values() if WAIT else ()):
        for platform in platforms:
            extension.attempts[platform]["key"] = ""
    state = State(targets, extensions)
    catalog, uploaded = publish_catalog()
    if not uploaded:
        log("extensions-catalog.json: unchanged")
    if not again:
        start_sites(catalog)

    waited = {text.split(" (")[0] for text in did["left"]}
    lines = ["### What this step did", ""]
    if env("REASON"):
        lines += [f"Woken by: {env('REASON')}", ""]
    lines += [f"- **Started ({len(did['started'])}):** " + ("; ".join(did["started"]) or "nothing"),
              "- **Finished since the step before:** " + ("; ".join(did["finished"]) or "nothing"),
              "- **Site rebuilt:** " + (", ".join(sorted(set(did["site"]))) or "no")]
    if did["left"]:
        lines.append("- **Left alone, a build not started here is running:** " + "; ".join(did["left"]))
    lines += ["", "### Where every extension stands", "",
              "| Extension | " + " | ".join(targets) + " |", "|---|" + "---|" * len(targets)]
    for extension in sorted(extensions, key=lambda e: e.name):
        cells = []
        for platform in targets:
            reason, attempt = state.reason(extension, platform), extension.attempts.get(platform) or {}
            cell = "current" if not reason else reason
            # A build that was not started here is running: nothing is started
            # for the extension meanwhile, which the cell would not say.
            if reason and extension.name in waited and not attempt.get("run"):
                cell += " (waiting for a build that is running)"
            # Failing tests are said as such: they are not a build that failed.
            if not reason and (extension.package(platform) or {}).get("tests") == "failed":
                cell += ", tests failed"
            if attempt.get("run"):
                cell += f" ([{'build' if not reason else attempt.get('conclusion')}]({attempt['run']}))"
            cells.append(cell)
        lines.append(f"| {extension.name}{' (' + extension.note + ')' if extension.note else ''} | " + " | ".join(cells) + " |")
    if env("GITHUB_STEP_SUMMARY"):
        with open(env("GITHUB_STEP_SUMMARY"), "a", encoding="utf8") as f:
            f.write("\n".join(lines) + "\n")
    print("\n".join(lines))

    if again:
        log("Out of time: starting another run to carry on.")
        status, body = request(f"{API}/repos/{SLICER}/actions/workflows/{env('SELF_WORKFLOW')}/dispatches",
                               env("GH_TOKEN"), "POST", {"ref": env("SELF_REF")})
        if status not in (200, 204):
            log(f"::warning::Could not start another run (HTTP {status}: {body})")


if __name__ == "__main__":
    main()
