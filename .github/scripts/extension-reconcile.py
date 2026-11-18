#!/usr/bin/env python3
"""Bring the extensions up to date with the Slicer nightly, in dependency order.

Run by extension-reconcile.yml. It keeps no memory of events: each pass works
out what to do from what is published, so that running it late, twice, or after
a failure converges on the same state.

  1. The target: the Slicer commit of each platform of the nightly release.
  2. The extension repositories: those the Slicer nightly GitHub App is
     installed in that have NIGHTLY_WORKFLOW; what each published for each
     platform (extension-<platform>.json of its nightly release), the head of
     the branch it is built from, and what it depends on (EXTENSION_DEPENDS).
  3. An extension is current on a platform when its package is built against
     the target Slicer commit, from that head, and against the very packages
     the extensions it depends on publish now (built_with), themselves current.
  4. Each platform is a chain of its own. The build of an extension for a
     platform is started (its workflow, with the platform as `platforms`
     input) as soon as it is not current there and the extensions it depends
     on have been dealt with there, whatever the other platforms are doing;
     when it finishes, what it published is read, and what depended on it is
     started. A build is started once for the same Slicer, head and
     dependencies: one that fails is not started again until one of them
     changes, and what depends on it is not built against anything else.
  5. The catalog (extensions-catalog.json of the Slicer nightly release): per
     platform, the extensions that are current, with their package and its
     hash; the others, with the reason. One file, replaced at once, each time a
     build changes what is current: what an Extensions Manager reads is always
     a consistent set.
  With WAIT=true, one pass waits for the builds and carries on itself, down
     the chains. Otherwise a run is one step: it starts what can be started
     and stops. The end of each build, published or failed, and a commit to an
     extension, start the next step, through the webhook of the app and its
     relay (.github/extension-webhook), so nothing waits on a runner for
     hours; the builds a step started are taken up by the next one, which
     neither starts them again nor builds against them until they have ended.
  6. The sites that carry the web packages (NIGHTLY_WEB_WORKFLOW) are started
     each time the web part of the catalog changed: an extension is on a site
     as soon as it is built, without waiting for the ones that depend on it.

Environment: GH_TOKEN (of the workflow: the release of this repository, and
starting itself), APP_ID and APP_PRIVATE_KEY (the app: starting the builds),
GITHUB_REPOSITORY, RELEASE_TAG, NIGHTLY_WORKFLOW, NIGHTLY_WEB_WORKFLOW,
REFS (JSON {repository: branch}, to build from another branch than the default
one), RETRY (true: start again what failed for the same inputs), START_SITES,
FORCE_SITES, DEADLINE_MINUTES, SELF_WORKFLOW and SELF_REF.
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
# true: wait for the builds, and start what depended on them, in one pass.
# false: one step, which starts what can be started and stops; the end of each
# build starts the next step (the relay of the app's webhook,
# .github/extension-webhook), which has to be there.
WAIT = env("WAIT") == "true"
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
        self.packages = {}      # platform: "package" of its extension-<platform>.json
        self.web = None         # its entry of SlicerWeb's extension index
        self.attempts = {}      # platform: {key, run, conclusion, date}, of this pass or an earlier one
        self.note = ""
        self.one_run = False    # its workflow takes no `platforms` input: one run builds every platform

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

    def read_published(self, platforms):
        """What it published for the platforms."""
        for platform in platforms:
            described = public_json(f"https://github.com/{self.repository}/releases/download/{TAG}/extension-{platform}.json")
            if described and isinstance(described.get("package"), dict):
                self.packages[platform] = described["package"]
                if platform == "web":
                    self.web = described.get("web")
            else:
                self.packages.pop(platform, None)

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
            reason = "not published for this platform"
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
        # Not while its build is running: what depends on it waits for it.
        return (bool(attempt) and attempt.get("conclusion") != "running"
                and attempt.get("key") == self.key(extension, platform))

    def startable(self, extension, platform):
        # Only against dependencies that are current: after one that failed,
        # what depends on it is left as it is.
        return (not extension.note and not self.settled(extension, platform)
                and all(not self.reason(d, platform) for d in self.registered(extension)))


# --------------------------------------------------------------------- runs

def runs_in_progress(extension, token):
    found = []
    for status_name in ("queued", "in_progress"):
        status, body = request(f"{API}/repos/{extension.repository}/actions/workflows/{WORKFLOW}/runs"
                               f"?branch={extension.ref}&status={status_name}&per_page=20", token)
        if status == 200:
            # Those that publish: a push builds and tests only.
            found += [r for r in body["workflow_runs"] if r["event"] == "workflow_dispatch"]
    return found


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


# ------------------------------------------------------------------ catalog

def gh(*args, check=True):
    return subprocess.run(["gh", *args], check=check, capture_output=True, text=True)


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
            }
            if package.get("icon"):
                entry["icon"] = f"{base}/{package['icon']}"
            if platform == "web" and extension.web:
                entry["web"] = extension.web
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
    gh("release", "upload", TAG, path, "--clobber", "-R", SLICER)
    log(f"{name}: published")


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
    for extension in extensions:
        extension.read_source(app)
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
        if not same(registry, public_json(f"{base}/extension-repositories.json")):
            upload("extension-repositories.json", registry)

    published = public_json(f"{base}/extensions-catalog.json")
    earlier = (published or {}).get("extensions") or {}
    state = State(targets, extensions)
    # What an earlier pass started, for the same inputs, is not started again.
    running = {}    # run id: (extension, platforms, url)
    for extension in extensions:
        for platform, attempt in ((earlier.get(extension.name) or {}).get("attempts") or {}).items():
            if not RETRY and platform in targets and attempt.get("key") == state.key(extension, platform):
                # What an earlier step started and left running is taken up
                # as if this one had started it: the platforms it builds, and
                # those only, wait for it, as does what depends on them there.
                if attempt.get("conclusion") == "not started":
                    continue  # never started: started again
                if attempt.get("conclusion") == "running":
                    run = (attempt.get("run") or "").rstrip("/").split("/")[-1]
                    if not run.isdigit():
                        continue  # never started: started again
                    running.setdefault(int(run), (extension, [], attempt["run"]))[1].append(platform)
                extension.attempts[platform] = attempt
    # Builds that are going otherwise (started by hand, or by a pass that was
    # cancelled) are waited for before anything is started for the extension:
    # which platforms they build is not known.
    foreign = {}
    for extension in extensions:
        going = [r for r in runs_in_progress(extension, app.token(extension.repository)) if r["id"] not in running]
        if going:
            foreign[extension.repository] = [r["id"] for r in going]
            log(f"{extension.name}: waiting for the builds already running ({', '.join(r['html_url'] for r in going)})")

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

    started, again, checked_targets = 0, False, time.time()
    unstarted = {}  # (repository, platform): how many times its build could not be started
    while True:
        changed = False
        # The builds that finished: what they published is read again.
        for run_id, (extension, platforms, url) in list(running.items()):
            status, body = request(f"{API}/repos/{extension.repository}/actions/runs/{run_id}", app.token(extension.repository))
            if status == 200 and body["status"] == "completed":
                log(f"{extension.name} ({', '.join(platforms)}): {body['conclusion']} {url}")
                for platform in platforms:
                    extension.attempts[platform]["conclusion"] = body["conclusion"]
                extension.read_published(platforms)
                del running[run_id]
                changed = True
        for extension in extensions:
            for run_id in list(foreign.get(extension.repository, [])):
                status, body = request(f"{API}/repos/{extension.repository}/actions/runs/{run_id}", app.token(extension.repository))
                if status != 200 or body["status"] == "completed":
                    foreign[extension.repository].remove(run_id)
                    extension.read_published(targets)
                    changed = True
        # A nightly published meanwhile, or one more platform of it, changes
        # the target; a commit to an extension changes what it is built from,
        # and so what depends on it. A build already going for the old commit
        # finishes, and is built again after; what depends on it waits.
        if time.time() - checked_targets > 300:
            checked_targets = time.time()
            for extension in extensions:
                head = extension.head
                extension.read_source(app)
                if extension.head != head:
                    log(f"{extension.name}: new commit {extension.head[:10]} on {extension.ref}")
                    changed = True
            latest = targets_now()
            if latest and latest != targets:
                log("Slicer: " + ", ".join(f"{p} {c[:10]}" for p, c in latest.items()))
                targets, changed = latest, True
                for extension in extensions:
                    extension.read_published(targets)
        state = State(targets, extensions)
        busy = {(e.repository, p) for e, platforms, _ in running.values() for p in platforms}
        todo = [(e, p) for e in extensions for p in targets
                if state.startable(e, p) and (e.repository, p) not in busy and not foreign.get(e.repository)
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
                extension.attempts[platform] = {"key": state.key(extension, platform), "conclusion": "not started", "date": now()}
            run = dispatch(extension, app, platforms, set(running))
            started += 1
            if extension.one_run and len(platforms) == 1:
                # It was found out by this start: the run builds the other platforms too.
                platforms = [p for p in targets if p in platforms
                             or (state.startable(extension, p) and (extension.repository, p) not in busy)]
                for platform in platforms:
                    extension.attempts[platform] = {"key": state.key(extension, platform), "conclusion": "not started", "date": now()}
            if run:
                running[run[0]] = (extension, platforms, run[1])
                for platform in platforms:
                    extension.attempts[platform].update(run=run[1], conclusion="running")
                    unstarted.pop((extension.repository, platform), None)
            else:
                # Not started, so not dealt with: nothing will ever say that
                # it ended. It is tried again, a few times in this pass, and
                # by the next pass, whatever starts it.
                for platform in platforms:
                    extension.attempts.pop(platform, None)
                    unstarted[(extension.repository, platform)] = unstarted.get((extension.repository, platform), 0) + 1
                    retry_soon = retry_soon or unstarted[(extension.repository, platform)] < 3
                # Unless it did start, and only its run was not found: that
                # one is waited for like any build not started here.
                lost = [r["id"] for r in runs_in_progress(extension, app.token(extension.repository)) if r["id"] not in running]
                if lost:
                    foreign[extension.repository] = lost
            time.sleep(1)
        if changed:
            # What is consistent now is offered now: a platform does not wait for the others.
            catalog = catalog_of(state)
            if not same(catalog, published):
                upload("extensions-catalog.json", catalog)
                published = catalog
            # The sites show each web package as soon as it is offered.
            start_sites(catalog)
        if not running and not todo and not any(foreign.values()):
            break
        if not WAIT and not retry_soon:
            # What was started tells, when it ends, that there is a next step.
            # A build that could not be started tells nothing: the step stays
            # to try it again, a few times.
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
    catalog = catalog_of(state)
    if not same(catalog, published):
        upload("extensions-catalog.json", catalog)
    else:
        log("extensions-catalog.json: unchanged")
    if not again:
        start_sites(catalog)

    lines = ["### Extensions", "", f"Builds started: {started}.", "",
             "| Extension | " + " | ".join(targets) + " |", "|---|" + "---|" * len(targets)]
    for extension in sorted(extensions, key=lambda e: e.name):
        cells = []
        for platform in targets:
            reason, attempt = state.reason(extension, platform), extension.attempts.get(platform) or {}
            cell = "current" if not reason else reason
            if attempt.get("run"):
                cell += f" ([{attempt.get('conclusion')}]({attempt['run']}))"
            cells.append(cell)
        lines.append(f"| {extension.name}{' (' + extension.note + ')' if extension.note else ''} | " + " | ".join(cells) + " |")
    summary("\n".join(lines))
    print("\n".join(lines))

    if again:
        # Near the time limit of a job: another run carries on from what is published.
        log("Out of time: starting another run to carry on.")
        status, body = request(f"{API}/repos/{SLICER}/actions/workflows/{env('SELF_WORKFLOW')}/dispatches",
                               env("GH_TOKEN"), "POST", {"ref": env("SELF_REF")})
        if status not in (200, 204):
            log(f"::warning::Could not start another run (HTTP {status}: {body})")


if __name__ == "__main__":
    main()
