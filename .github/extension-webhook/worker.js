// Starts the next step of extension-reconcile.yml when something an extension
// is built from, or a build of one, has changed.
//
// Two GitHub Apps, with nothing in common but this relay:
//
//   - The Slicer nightly app is installed in every extension repository that
//     follows the nightly. Its webhook points here. This relay holds its
//     webhook secret, to know that an event is from GitHub, and nothing else
//     of it: its private key, which starts builds in the extension
//     repositories, stays a secret of the Slicer repository's workflows.
//   - The relay app is installed in the Slicer repository alone, with the
//     Actions permission, and has no webhook. This relay holds its private
//     key, and uses it for one thing: starting extension-reconcile.yml. What
//     a leak of this key can do is start workflows of the Slicer repository
//     that can be started by hand, with the inputs they define.
//
// An event starts the reconciliation when:
//
//   - a platform of an extension has been built (workflow_job, completed, on
//     the branch the extension is built from): the job publishes what it
//     built itself, so what depends on the extension can be built for that
//     platform, without waiting for the other platforms;
//   - a build of an extension has ended (workflow_run, completed, of
//     slicer-nightly-build.yml), whether a commit to the extension or the
//     reconciliation started it, published or failed.
//   A commit to an extension is not an event to act on: the extension builds
//   and publishes it itself, and the two above follow.
//   - the nightly app is installed in more repositories.
//
// Nothing is decided here beyond "something may have changed": the
// reconciliation works out what to build from what is published. Many events
// are one reason to look: a run is started only when none is running or
// waiting to start. One that is running stays for as long as builds are
// going, sees the end of each itself, and looks for commits to the
// extensions and the builds they start; it needs no telling. What an event
// arriving in its last seconds is about is followed by other events once it
// has ended (a commit is followed by the end of its build), or is caught by
// the backstop schedule. So a burst of builds is one run.
//
// It also hands the web packages of the extensions to the pages that install
// them (GET /gh/<owner>/<repository>/<tag>/<file>.whl): a page cannot download
// a file of a GitHub release, which answers without the header that lets
// another site read it, so a site that offers extensions would have to carry
// a copy of each. Only the wheels of the release RELEASE_TAG of the
// repositories that build extensions (extension-repositories.json of the
// Slicer release) are handed out, so that this is not a relay for anything
// else. What a wheel contains is not looked at here: the page checks it
// against the hash the extensions catalog gives. This part needs none of the
// secrets.
//
// And it starts the nightly build of Slicer on time (the cron trigger of
// wrangler.toml): GitHub starts the scheduled runs of a workflow late, by hours
// at times. It starts NIGHTLY_WORKFLOW with the nightly input, as the relay
// app, which makes the run behave as the scheduled one; the schedule of the
// workflow stays, for when this is not there, and finds nothing left to do.
//
// A Cloudflare Worker (wrangler.toml); it uses nothing but fetch and WebCrypto,
// so it runs on any similar runtime. Configuration:
//   secrets  WEBHOOK_SECRET         the webhook secret of the Slicer nightly app
//            RELAY_APP_PRIVATE_KEY  the private key of the relay app, in PKCS#8:
//                                   openssl pkcs8 -topk8 -nocrypt -in relay.pem
//   vars     RELAY_APP_ID           the ID of the relay app
//            SLICER_REPOSITORY      e.g. Slicer/Slicer, where the relay app is
//                                   installed
//            SLICER_REF             branch of extension-reconcile.yml, e.g. main
//            REFS                   optional, JSON {"owner/repository": "branch"}:
//                                   the branches built instead of the default
//                                   one, as SLICER_EXTENSION_REFS of the Slicer
//                                   repository
//            NIGHTLY_WORKFLOW       optional, e.g. build.yml: the workflow the
//                                   cron trigger starts, on SLICER_REF; none
//                                   when empty
//            RELEASE_TAG            optional (nightly): the release of the Slicer
//                                   repository that lists the extension
//                                   repositories, and of each of those, whose
//                                   wheels are handed out

const API = "https://api.github.com";
const WORKFLOW = "extension-reconcile.yml";
const BUILD_WORKFLOW = "slicer-nightly-build.yml";
// The job of a platform in extension-build.yml, as the workflow calling it
// names it: "<calling job> / <platform>".
const PLATFORM_JOB = /(?:^|\/ )(linux|macos|macos-arm64|windows|web)$/;
const encoder = new TextEncoder();
// What a page of any site may read of the answers to the requests for a wheel.
const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
  "Access-Control-Expose-Headers": "Content-Length",
};
const WHEEL = /^\/gh\/([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.+-]+\.whl)$/;
// The repositories that build extensions, as last read, and when.
let registry = { repositories: null, read: 0, from: "" };

export default {
  async fetch(request, env) {
    if (request.method !== "POST") {
      const path = new URL(request.url).pathname;
      if (path.startsWith("/gh/")) {
        return wheel(request, path, env);
      }
      return new Response("Slicer extension webhook\n", { status: 200 });
    }
    const body = await request.text();
    if (!(await verify(body, request.headers.get("X-Hub-Signature-256") || "", env.WEBHOOK_SECRET))) {
      return new Response("bad signature\n", { status: 401 });
    }
    let payload;
    try {
      payload = JSON.parse(body);
    } catch {
      return new Response("not JSON\n", { status: 400 });
    }
    // Said plainly, rather than as whatever fails first for lack of it.
    const missing = ["RELAY_APP_ID", "RELAY_APP_PRIVATE_KEY", "SLICER_REPOSITORY", "SLICER_REF"].filter((k) => !env[k]);
    if (missing.length) {
      return new Response(`not configured: ${missing.join(", ")} missing\n`, { status: 500 });
    }
    const reason = reasonOf(request.headers.get("X-GitHub-Event"), payload, env);
    if (!reason) {
      return new Response("ignored\n", { status: 202 });
    }
    let status;
    try {
      status = await dispatch(env, reason);
    } catch (error) {
      console.log(`${reason}: ${error}`);
      return new Response(`${reason}: ${error}\n`, { status: 500 });
    }
    console.log(`${reason}: ${status}`);
    // Not 2xx when the start failed, so that GitHub lists the delivery as
    // failed, from where it can be delivered again.
    if (status === 208) {
      return new Response(`${reason}: a run is going already\n`, { status: 200 });
    }
    return new Response(`${reason}: HTTP ${status}\n`, { status: status === 204 || status === 200 ? 200 : 502 });
  },

  // The cron trigger: the nightly build. A failure is thrown, so that
  // Cloudflare lists the invocation as failed.
  async scheduled(event, env) {
    if (!env.NIGHTLY_WORKFLOW) {
      console.log("NIGHTLY_WORKFLOW is not set: no nightly build to start");
      return;
    }
    const missing = ["RELAY_APP_ID", "RELAY_APP_PRIVATE_KEY", "SLICER_REPOSITORY", "SLICER_REF"].filter((k) => !env[k]);
    if (missing.length) {
      throw new Error(`not configured: ${missing.join(", ")} missing`);
    }
    // Not held back by a run that is going: the workflow lets one build of a
    // branch run at a time, and a nightly that finds nothing new stops.
    const status = await start(env, env.NIGHTLY_WORKFLOW, { nightly: "true" }, false);
    console.log(`nightly ${env.NIGHTLY_WORKFLOW} on ${env.SLICER_REF}: HTTP ${status}`);
    if (status !== 204 && status !== 200) {
      throw new Error(`the nightly build could not be started: HTTP ${status}`);
    }
  },
};

// The wheel of an extension, for a page to read: see the top of the file.
async function wheel(request, path, env) {
  const refuse = (status, text) => new Response(text + "\n", { status, headers: CORS });
  if (request.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: { ...CORS, "Access-Control-Max-Age": "86400" } });
  }
  if (request.method !== "GET" && request.method !== "HEAD") {
    return refuse(405, "GET only");
  }
  const match = WHEEL.exec(path);
  if (!match) {
    return refuse(404, "not the address of a wheel: /gh/<owner>/<repository>/<tag>/<file>.whl");
  }
  const [, owner, name, tag, file] = match;
  if (!env.SLICER_REPOSITORY) {
    return refuse(500, "not configured: SLICER_REPOSITORY missing");
  }
  const release = env.RELEASE_TAG || "nightly";
  if (tag !== release) {
    return refuse(403, `only the wheels of the release ${release}`);
  }
  let repositories;
  try {
    repositories = await extensionRepositories(env.SLICER_REPOSITORY, release);
  } catch (error) {
    return refuse(502, `the list of the extension repositories could not be read: ${error}`);
  }
  if (!repositories.includes(`${owner}/${name}`.toLowerCase())) {
    return refuse(403, `${owner}/${name} is not a repository that builds an extension for ${env.SLICER_REPOSITORY}`);
  }
  const asset = await fetch(`https://github.com/${owner}/${name}/releases/download/${tag}/${file}`, {
    method: request.method,
    headers: { "User-Agent": "slicer-extension-webhook" },
    redirect: "follow",
  });
  if (!asset.ok) {
    return refuse(asset.status === 404 ? 404 : 502, `${owner}/${name}: ${file}: HTTP ${asset.status}`);
  }
  const headers = new Headers(CORS);
  headers.set("Content-Type", "application/octet-stream");
  headers.set("X-Content-Type-Options", "nosniff");
  // The file of a name is replaced by the next build: not kept for long.
  headers.set("Cache-Control", "public, max-age=300");
  for (const kept of ["Content-Length", "ETag", "Last-Modified"]) {
    if (asset.headers.get(kept)) headers.set(kept, asset.headers.get(kept));
  }
  return new Response(asset.body, { status: 200, headers });
}

// owner/name, in lower case, of the repositories that build extensions: read
// again after a minute, and what was read before stands when that fails.
async function extensionRepositories(slicer, release) {
  const from = `https://github.com/${slicer}/releases/download/${release}/extension-repositories.json`;
  if (registry.repositories && registry.from === from && Date.now() - registry.read < 60000) {
    return registry.repositories;
  }
  try {
    const response = await fetch(from, { headers: { "User-Agent": "slicer-extension-webhook" }, redirect: "follow" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const listed = (await response.json()).repositories || [];
    registry = { repositories: listed.map((r) => String(r.repository || "").toLowerCase()).filter(Boolean), read: Date.now(), from };
  } catch (error) {
    if (!registry.repositories || registry.from !== from) throw error;
  }
  return registry.repositories;
}

// The branch an extension repository is built from.
function branchOf(repository, env) {
  let refs = {};
  try {
    refs = JSON.parse(env.REFS || "{}");
  } catch {
    refs = {};
  }
  return refs[repository.full_name] || repository.default_branch;
}

// Why the reconciliation should run; null when the event changes nothing it
// looks at. Kept short, and of letters GitHub chose: it becomes the name of
// the run.
function reasonOf(event, payload, env) {
  const repository = payload.repository || {};
  if (repository.full_name && repository.full_name === env.SLICER_REPOSITORY) {
    return null;
  }
  if (event === "workflow_job") {
    const job = payload.workflow_job || {};
    // A platform's job of the branch the extension is built from, which has
    // published what it built (or failed to): "build / linux", "build / web".
    if (payload.action !== "completed" || job.head_branch !== branchOf(repository, env)
        || !PLATFORM_JOB.test(String(job.name)) || !["success", "failure"].includes(job.conclusion)) {
      return null;
    }
    return `${repository.full_name} ${String(job.name).match(PLATFORM_JOB)[1]} ${job.conclusion} ${String(job.head_sha).slice(0, 10)}`;
  }
  if (event === "workflow_run") {
    const run = payload.workflow_run || {};
    // Only the builds that publish: of the branch, pushed or started by the
    // reconciliation. Not those of pull requests or other branches.
    if (payload.action !== "completed" || !String(run.path).endsWith("/" + BUILD_WORKFLOW)
        || run.head_branch !== branchOf(repository, env) || !["push", "workflow_dispatch"].includes(run.event)) {
      return null;
    }
    // A build that succeeded has been told of already, platform by platform,
    // by its jobs: its end adds nothing, and would start the same step twice.
    // One that did not may have ended without any platform's job saying so.
    if (run.conclusion === "success") {
      return null;
    }
    return `${repository.full_name} ${run.conclusion} ${String(run.head_sha).slice(0, 10)}`;
  }
  if ((event === "installation" && payload.action === "created")
      || (event === "installation_repositories" && payload.action === "added")) {
    const added = payload.repositories_added || payload.repositories || [];
    return `installed in ${added.map((r) => r.full_name).join(", ") || "a repository"}`.slice(0, 200);
  }
  return null;
}

async function verify(body, header, secret) {
  if (!secret || !header.startsWith("sha256=")) {
    return false;
  }
  const hex = header.slice("sha256=".length);
  if (!/^[0-9a-f]{64}$/.test(hex)) {
    return false;
  }
  const signature = new Uint8Array(hex.match(/../g).map((b) => parseInt(b, 16)));
  const key = await crypto.subtle.importKey("raw", encoder.encode(secret), { name: "HMAC", hash: "SHA-256" },
    false, ["verify"]);
  // Constant time.
  return crypto.subtle.verify("HMAC", key, signature, encoder.encode(body));
}

function base64url(bytes) {
  let text = "";
  for (const b of bytes) {
    text += String.fromCharCode(b);
  }
  return btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

async function appJwt(appId, pem) {
  if (pem.includes("BEGIN RSA PRIVATE KEY")) {
    throw new Error("RELAY_APP_PRIVATE_KEY is PKCS#1; convert it: openssl pkcs8 -topk8 -nocrypt -in relay.pem");
  }
  const der = Uint8Array.from(atob(pem.replace(/-----[^-]+-----|\s/g, "")), (c) => c.charCodeAt(0));
  const key = await crypto.subtle.importKey("pkcs8", der, { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false, ["sign"]);
  const now = Math.floor(Date.now() / 1000);
  const unsigned = base64url(encoder.encode(JSON.stringify({ alg: "RS256", typ: "JWT" }))) + "."
    + base64url(encoder.encode(JSON.stringify({ iat: now - 60, exp: now + 540, iss: String(appId) })));
  const signature = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key, encoder.encode(unsigned));
  return `${unsigned}.${base64url(new Uint8Array(signature))}`;
}

function github(path, token, init = {}) {
  return fetch(`${API}${path}`, {
    ...init,
    headers: {
      Authorization: `Bearer ${token}`,
      Accept: "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
      "User-Agent": "slicer-extension-webhook",
      ...(init.body ? { "Content-Type": "application/json" } : {}),
    },
  });
}

// Starts the reconciliation, unless a run of it is going or about to.
function dispatch(env, reason) {
  return start(env, WORKFLOW, { reason }, true);
}

// Starts a workflow of the Slicer repository, on SLICER_REF, with a token of
// the relay app's installation there, limited to that repository and to the
// Actions permission; the HTTP status of the start, or of the step that
// failed. With `once`, 208 and nothing started when a run of it is not over.
async function start(env, workflow, inputs, once) {
  const jwt = await appJwt(env.RELAY_APP_ID, env.RELAY_APP_PRIVATE_KEY);
  let response = await github(`/repos/${env.SLICER_REPOSITORY}/installation`, jwt);
  if (!response.ok) {
    console.log(`the relay app is not installed in ${env.SLICER_REPOSITORY}? HTTP ${response.status}`);
    return response.status;
  }
  const { id } = await response.json();
  response = await github(`/app/installations/${id}/access_tokens`, jwt, {
    method: "POST",
    body: JSON.stringify({
      repositories: [env.SLICER_REPOSITORY.split("/")[1]],
      permissions: { actions: "write" },
    }),
  });
  if (!response.ok) {
    return response.status;
  }
  const { token } = await response.json();
  // A run that is going, or about to, sees what this event is about.
  if (once) {
    response = await github(`/repos/${env.SLICER_REPOSITORY}/actions/workflows/${workflow}/runs?per_page=20`, token);
    if (response.ok) {
      const { workflow_runs: runs } = await response.json();
      if ((runs || []).some((run) => run.status !== "completed")) {
        return 208;
      }
    }
  }
  response = await github(`/repos/${env.SLICER_REPOSITORY}/actions/workflows/${workflow}/dispatches`, token, {
    method: "POST",
    body: JSON.stringify({ ref: env.SLICER_REF, inputs }),
  });
  if (!response.ok && response.status !== 204) {
    console.log(await response.text());
  }
  return response.status;
}
