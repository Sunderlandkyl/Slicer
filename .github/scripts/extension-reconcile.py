#!/usr/bin/env python3
"""Bring the extensions up to date with the Slicer nightly, in dependency order.

Run by extension-reconcile.yml. It keeps no memory of events: each pass works
out what to do from what is published, so that running it late, twice, or after
a failure converges on the same state.

  1. The target: the Slicer commit of each platform of the nightly release.
  2. The extension repositories: those the Slicer nightly GitHub App is
     installed in that have NIGHTLY_WORKFLOW; what each published
     (extension.json of its nightly release), the head of the branch it is
     built from, and what it depends on (EXTENSION_DEPENDS).
  3. An extension is current on a platform when its package is built against
     the target Slicer commit, from that head, and against the very packages
     the extensions it depends on publish now (built_with), themselves current.
  4. Every extension that is not current, whose dependencies have all been
     dealt with, is started (a wave); the runs are waited for; and again,
     until nothing is left to start. An extension is started once for the same
     Slicer, head and dependencies: one that fails is not started again until
     one of them changes, and what depends on it is not built against
     anything else.
  5. The catalog (extensions-catalog.json of the Slicer nightly release): per
     platform, the extensions that are current, with their package and its
     hash; the others, with the reason. One file, replaced at once: what an
     Extensions Manager reads is always a consistent set.
  6. The sites that carry the web packages (NIGHTLY_WEB_WORKFLOW) are started.

Environment: GH_TOKEN (of the workflow: the release of this repository, and
starting itself), APP_ID and APP_PRIVATE_KEY (the app: starting the builds),
GITHUB_REPOSITORY, RELEASE_TAG, NIGHTLY_WORKFLOW, NIGHTLY_WEB_WORKFLOW,
REFS (JSON {repository: branch}, to build from another branch than the default
one), RETRY (true: start again what failed for the same inputs), START_SITES,
DEADLINE_MINUTES, SELF_WORKFLOW and SELF_REF.
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
PLATFORMS = ("linux", "macos", "windows", "web")
STARTED = time.time()


def env(name, default=""):
    return os.environ.get(name, default)


SLICER = env("GITHUB_REPOSITORY")
TAG = env("RELEASE_TAG", "nightly")
WORKFLOW = env("NIGHTLY_WORKFLOW", "slicer-nightly-build.yml")
WEB_WORKFLOW = env("NIGHTLY_WEB_WORKFLOW", "slicer-nightly-web.yml")
RETRY = env("RETRY") == "true"
DEADLINE = STARTED + 60 * float(env("DEADLINE_MINUTES", "300"))
REFS = json.loads(env("REFS") or "{}")


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


def public_json(url):
    """A public JSON document; None when there is none."""
    status, document = request(url)
    return document if status == 200 else None


# ------------------------------------------------------------------- the app

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


# -------------------------------------------------------------- an extension

class Extension:
    def __init__(self, repository, ref):
        self.repository, self.ref = repository, ref
        self.name = repository.split("/")[-1]
        self.head = None
        self.depends = []       # EXTENSION_DEPENDS, at the head
        self.published = {}     # extension.json
        self.attempt = None     # of this run, or of an earlier one: {key, run, conclusion, date}
        self.note = ""

    def read(self, app):
        """What is published, and what there is to build."""
        self.published = public_json(f"https://github.com/{self.repository}/releases/download/{TAG}/extension.json") or {}
        status, commit = request(f"{API}/repos/{self.repository}/commits/{self.ref}", app.token(self.repository))
        if status != 200:
            status, commit = request(f"{API}/repos/{self.repository}/commits/{self.ref}", env("GH_TOKEN"))
        self.head = commit["sha"] if status == 200 else None
        if self.head:
            status, text = request(f"https://raw.githubusercontent.com/{self.repository}/{self.head}/CMakeLists.txt",
                                   raw=True)
            text = text.decode("utf8", "replace") if status == 200 else ""
            match = re.search(r"^\s*project\(\s*([A-Za-z0-9_]+)", text, re.M)
            if match:
                self.name = match.group(1)
            match = re.search(r"set\s*\(\s*EXTENSION_DEPENDS\s+([^)]*)\)", text, re.I)
            names = [n.strip('"') for n in re.split(r"[\s;]+", match.group(1))] if match else []
            self.depends = [n for n in names if n and n != "NA"]
        if self.published.get("name"):
            self.name = self.published["name"]

    def package(self, platform):
        package = (self.published.get("packages") or {}).get(platform)
        return package if isinstance(package, dict) else None


class State:
    def __init__(self, targets, extensions):
        self.targets = targets
        self.by_name = {e.name: e for e in extensions}
        self.extensions = extensions
        self._reason = {}

    def registered(self, extension):
        """The extensions it depends on that a repository builds."""
        return [self.by_name[d] for d in extension.depends if d in self.by_name and d != extension.name]

    def reason(self, extension, platform, chain=()):
        """Why the extension is not current on a platform; "" when it is."""
        key = (extension.name, platform)
        if key in self._reason:
            return self._reason[key]
        if extension.name in chain:
            return "dependency cycle: " + " -> ".join(chain + (extension.name,))
        package, target = extension.package(platform), self.targets.get(platform)
        reason = ""
        if not package:
            reason = "not published for this platform"
        elif package.get("slicer_commit") != target:
            reason = f"built against Slicer {str(package.get('slicer_commit'))[:10]}, not {str(target)[:10]}"
        elif extension.head and (package.get("revision") or extension.published.get("revision")) != extension.head:
            reason = f"built from {str(package.get('revision') or extension.published.get('revision'))[:10]}, not {extension.head[:10]}"
        else:
            for name, used in sorted((package.get("built_with") or {}).items()):
                dependency = self.by_name.get(name)
                if not dependency:
                    continue  # nothing publishes it: built from source with the extension
                theirs = dependency.package(platform)
                if not theirs or not used.get("sha256") or theirs.get("sha256") != used.get("sha256"):
                    reason = f"built against another package of {name} than the one it publishes"
                    break
                below = self.reason(dependency, platform, chain + (extension.name,))
                if below:
                    reason = f"{name} is not current ({below})"
                    break
        self._reason[key] = reason
        return reason

    def stale(self, extension):
        return [p for p in self.targets if self.reason(extension, p)]

    def key(self, extension):
        """What a build of the extension is made from: started once per key."""
        inputs = {
            "targets": self.targets, "head": extension.head,
            "dependencies": {d.name: {p: (d.package(p) or {}).get("sha256") for p in self.targets}
                             for d in self.registered(extension)},
        }
        return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:16]

    def settled(self, extension):
        """Nothing more will be done about it in this pass: current, or started for what there is."""
        if not self.stale(extension):
            return True
        return bool(extension.attempt) and extension.attempt.get("key") == self.key(extension)


# --------------------------------------------------------------------- runs

def runs_in_progress(extension, token):
    found = []
    for status_name in ("queued", "in_progress"):
        status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                               f"?branch={extension.ref}&status={status_name}&per_page=20", token)
        if status == 200:
            found += [r for r in body["workflow_runs"] if r["event"] == "workflow_dispatch"]
    return found


def start(extension, app):
    """Start the build of an extension; the run, or the one already going."""
    token = app.token(extension.repository)
    going = runs_in_progress(extension, token)
    if going:
        # Not started for what is published now: waited for, then looked at again.
        log(f"{extension.name}: a build is already running ({going[0]['html_url']})")
        return dict(going[0], found_running=True)
    since = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(seconds=30)
    status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/dispatches",
                           token, "POST", {"ref": extension.ref})
    if status not in (200, 204):
        log(f"::warning::{extension.repository}: could not start {WORKFLOW} on {extension.ref} (HTTP {status}: {body})")
        return None
    for _ in range(40):
        time.sleep(8)
        status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                               f"?branch={extension.ref}&event=workflow_dispatch&per_page=10", token)
        for run in (body or {}).get("workflow_runs", []) if status == 200 else []:
            created = datetime.datetime.fromisoformat(run["created_at"].replace("Z", "+00:00"))
            if created >= since:
                log(f"{extension.name}: started {run['html_url']}")
                return run
    log(f"::warning::{extension.repository}: started {WORKFLOW}, but its run did not appear")
    return None


def wait(runs, app):
    """Wait for the runs; {repository: conclusion}. None for one still going at the deadline."""
    conclusions, pending = {}, dict(runs)
    while pending and time.time() < DEADLINE:
        time.sleep(30)
        for repository, run in list(pending.items()):
            status, body = request(f"{API}/repos/{repository}/actions/runs/{run['id']}", app.token(repository))
            if status == 200 and body["status"] == "completed":
                conclusions[repository] = body["conclusion"]
                log(f"{repository}: {body['conclusion']} ({body['html_url']})")
                del pending[repository]
    for repository in pending:
        conclusions[repository] = None
    return conclusions


# ------------------------------------------------------------------ catalog

def gh(*args, check=True):
    return subprocess.run(["gh", *args], check=check, capture_output=True, text=True)


def catalog_of(state):
    platforms, withheld = {}, {}
    for platform, target in state.targets.items():
        offered, held = {}, {}
        for extension in sorted(state.extensions, key=lambda e: e.name):
            reason = state.reason(extension, platform)
            if reason:
                # An extension that has never built a platform is not withheld from it.
                if extension.package(platform) or extension.attempt:
                    held[extension.name] = reason
                continue
            package = extension.package(platform)
            base = f"https://github.com/{extension.repository}/releases/download/{TAG}"
            entry = {
                "repository": extension.repository, "revision": package.get("revision") or extension.published.get("revision"),
                "file": package["file"], "url": f"{base}/{package['file']}", "sha256": package.get("sha256", ""),
                "depends": sorted(n for n in (package.get("built_with") or {}) if n in state.by_name),
                "tests": package.get("tests", ""),
            }
            if package.get("icon"):
                entry["icon"] = f"{base}/{package['icon']}"
            if platform == "web" and extension.published.get("web"):
                entry["web"] = extension.published["web"]
            offered[extension.name] = entry
        platforms[platform] = {"slicer_commit": target, "extensions": offered}
        withheld[platform] = held
    return {
        "date": now(), "slicer_repository": SLICER, "release_tag": TAG,
        "description": "The extensions built against the Slicer builds of this release, by their own repositories: "
                       "per platform, those whose package is built against this Slicer commit and against the very "
                       "packages, listed here too, of the extensions it depends on.",
        "platforms": platforms, "withheld": withheld,
        "extensions": {e.name: {"repository": e.repository, "branch": e.ref, "head": e.head, "depends": e.depends,
                                **({"attempt": e.attempt} if e.attempt else {}), **({"note": e.note} if e.note else {})}
                       for e in sorted(state.extensions, key=lambda e: e.name)},
    }


def publish(name, document, previous):
    """Upload a JSON document to the release, unless it says what is there already."""
    same = lambda d: json.dumps({k: v for k, v in (d or {}).items() if k != "date"}, sort_keys=True)
    if previous is not None and same(previous) == same(document):
        log(f"{name}: unchanged")
        return False
    path = os.path.join(tempfile.mkdtemp(), name)
    with open(path, "w", encoding="utf8") as f:
        json.dump(document, f, indent=1)
    gh("release", "upload", TAG, path, "--clobber", "-R", SLICER)
    log(f"{name}: published")
    return True


def summary(text):
    if env("GITHUB_STEP_SUMMARY"):
        with open(env("GITHUB_STEP_SUMMARY"), "a", encoding="utf8") as f:
            f.write(text + "\n")


def main():
    if not env("APP_PRIVATE_KEY"):
        log("::warning::SLICER_NIGHTLY_APP_PRIVATE_KEY is not set; nothing to reconcile.")
        return
    base = f"https://github.com/{SLICER}/releases/download/{TAG}"
    app = App(env("APP_ID"), env("APP_PRIVATE_KEY"))

    def targets_now():
        targets = {}
        for platform in PLATFORMS:
            manifest = public_json(f"{base}/slicerweb-manifest.json" if platform == "web"
                                   else f"{base}/manifest-{platform}.json")
            if manifest and manifest.get("commit"):
                targets[platform] = manifest["commit"]
        return targets

    targets = targets_now()
    if not targets:
        log(f"::notice::The {TAG} release of {SLICER} holds no Slicer build; nothing to reconcile.")
        return
    log("Slicer: " + ", ".join(f"{p} {c[:10]}" for p, c in targets.items()))

    # The repositories: extensions, and sites.
    extensions, sites = [], []
    for repository, branch in sorted(app.repositories().items()):
        token = app.token(repository)
        if request(f"{API}/repos/{repository}/actions/workflows/{WORKFLOW}", token)[0] == 200:
            extensions.append(Extension(repository, REFS.get(repository, branch)))
        if request(f"{API}/repos/{repository}/actions/workflows/{WEB_WORKFLOW}", token)[0] == 200:
            sites.append((repository, REFS.get(repository, branch)))
    log(f"Extensions: {', '.join(e.repository for e in extensions) or 'none'}; sites: {', '.join(s for s, _ in sites) or 'none'}")
    if app.complete:
        publish("extension-repositories.json", {
            "date": now(), "slicer_repository": SLICER, "release_tag": TAG,
            "description": "Repositories building Slicer extensions against this repository: their packages are in "
                           "the release nightly of each, whose extension.json says what each was built against. "
                           "extensions-catalog.json lists the ones that are consistent with each other.",
            "repositories": [{"repository": e.repository, "default_branch": e.ref} for e in extensions],
        }, public_json(f"{base}/extension-repositories.json"))

    previous = public_json(f"{base}/extensions-catalog.json")
    earlier = (previous or {}).get("extensions") or {}

    def read_state():
        for extension in extensions:
            extension.read(app)
        return State(targets, extensions)

    state = read_state()
    # What an earlier pass started, for the same inputs, is not started again.
    for extension in extensions:
        attempt = (earlier.get(extension.name) or {}).get("attempt")
        if attempt and not RETRY and attempt.get("key") == state.key(extension):
            extension.attempt = attempt
    for extension in extensions:
        status = request(f"{API}/repos/{extension.repository}/contents/.github/workflows/{WORKFLOW}?ref={extension.ref}",
                         app.token(extension.repository))[0]
        if status == 404:
            extension.note = f"no {WORKFLOW} on {extension.ref}: not built"
            extension.attempt = {"key": state.key(extension), "conclusion": "not started", "date": now()}

    wave, again = 0, False
    while True:
        startable = [e for e in extensions if not state.settled(e) and not e.note
                     and all(state.settled(d) for d in state.registered(e))]
        if not startable:
            break
        if time.time() > DEADLINE:
            again = True
            break
        wave += 1
        log(f"::group::Wave {wave}: {', '.join(e.name for e in startable)}")
        runs = {}
        for extension in startable:
            log(f"{extension.name}: {'; '.join(f'{p}: {state.reason(extension, p)}' for p in state.stale(extension))}")
            extension.attempt = {"key": state.key(extension), "conclusion": "not started", "date": now()}
            run = start(extension, app)
            if run:
                runs[extension.repository] = run
                extension.attempt["run"] = run["html_url"]
            time.sleep(1)
        conclusions = wait(runs, app)
        for extension in startable:
            if extension.repository in conclusions:
                extension.attempt["conclusion"] = conclusions[extension.repository] or "still running"
                if conclusions[extension.repository] is None:
                    # Not waited for to its end: the next pass looks at it again.
                    extension.attempt["key"] = ""
                    again = True
                elif runs[extension.repository].get("found_running"):
                    extension.attempt = None
        log("::endgroup::")
        # A nightly published meanwhile changes the target: everything is compared with it.
        targets = targets_now() or targets
        state = read_state()

    catalog = catalog_of(state)
    changed = publish("extensions-catalog.json", catalog, previous)

    lines = ["### Extensions", "", f"Waves: {wave}.", "", "| Extension | " + " | ".join(targets) + " | Build |", "|---|" + "---|" * (len(targets) + 1)]
    for extension in sorted(extensions, key=lambda e: e.name):
        cells = [("current" if not state.reason(extension, p) else state.reason(extension, p)) for p in targets]
        attempt = extension.attempt or {}
        build = extension.note or (f"[{attempt.get('conclusion')}]({attempt['run']})" if attempt.get("run") else attempt.get("conclusion", ""))
        lines.append(f"| {extension.name} | " + " | ".join(cells) + f" | {build} |")
    summary("\n".join(lines))
    print("\n".join(lines))

    if again:
        # Near the time limit of a job: another run carries on from what is published.
        log("Out of time: starting another run to carry on.")
        status, body = request(f"{API}/repos/{SLICER}/actions/workflows/{env('SELF_WORKFLOW')}/dispatches",
                               env("GH_TOKEN"), "POST", {"ref": env("SELF_REF")})
        if status not in (200, 204):
            log(f"::warning::Could not start another run (HTTP {status}: {body})")
        return

    if env("START_SITES", "true") == "true" and (changed or env("FORCE_SITES") == "true"):
        for repository, branch in sites:
            status, body = request(f"{API}/repos/{repository}/actions/workflows/{WEB_WORKFLOW}/dispatches",
                                   app.token(repository), "POST", {"ref": branch})
            log(f"{repository}: {'started' if status in (200, 204) else f'could not start (HTTP {status})'} {WEB_WORKFLOW} on {branch}")
            time.sleep(1)


if __name__ == "__main__":
    main()
