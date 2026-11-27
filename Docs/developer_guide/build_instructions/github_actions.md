# Building with GitHub Actions

Slicer is built on Linux, macOS (Intel and Apple silicon) and Windows, and for
the web browser, by the [`Build`](https://github.com/Slicer/Slicer/actions/workflows/build.yml)
workflow. This page describes how that workflow is organized, how another
repository can build an extension against the resulting nightly build, and how
a custom application based on Slicer can build its installers the same way.

## How the build is organized

The work is shared between four workflow files:

| File | Role |
|---|---|
| `build.yml` | Slicer's `Build` workflow: decides what to build and publishes the nightly release. Each platform published is reported with a `reconcile-extensions` event, on which `extension-reconcile.yml` starts the extension builds |
| `build-application.yml` | Reusable: computes the prerequisites key of each platform, and calls the two below |
| `build-desktop.yml` | Reusable, called by `build-application.yml` for each of Linux, macOS and Windows: builds the prerequisites if the platform lacks them, then builds Slicer (or a custom application) and packages and tests it |
| `build-web.yml` | Reusable, called by `build-application.yml` when `web` is among the platforms: builds the WebAssembly runtime of [SlicerWeb](https://github.com/lassoan/SlicerWeb) in its toolchain container, split the same way into stored prerequisites and Slicer |

A full Slicer Superbuild takes several hours and produces a build tree of
15 GB or more. Most of it is the external projects (VTK, ITK, CTK, Python,
DCMTK, ...), which change rarely, so the work is split in two phases while
keeping the Superbuild structure intact:

| Phase | Target built | When it runs |
|---|---|---|
| Prerequisites | `Slicer-dependencies` | Only when the description of an external project changes |
| Slicer | `Slicer` | On every run |

The prerequisites of a platform are built by one job, which takes from an hour
and a half (Linux) to three and a half (Windows, macOS on Intel). A
GitHub-hosted job is stopped after 6 hours with nothing kept, so that job
warns when it has run for more than four and a half: the build then has to be
split, or made to start from the prerequisites of an earlier build. A run that
reuses the prerequisites shows the job as skipped.

Each platform is a chain of its own, in a call of `build-desktop.yml` or
`build-web.yml`: its application is built as soon as its own prerequisites
are, whatever the other platforms still have to build.

Adding or removing an external project does not require touching the
workflow: `Slicer-dependencies` covers them all.

The finished prerequisites are published as assets of the `nightly`
pre-release, named after the key, for example
`slicer-deps-linux-1a2b3c4d5e6f7890.tar.zst` (split into `.part-NN` files above
the 2 GiB asset limit). A `<key>.parts` list, uploaded last, marks a set as
complete. Every later run, on any branch, restores those assets instead of
rebuilding: the job that builds Slicer downloads them, whether they were
there already or have just been built. The key is computed from the content of `SuperBuild/`,
`SuperBuild.cmake`, `CMake/ExternalProject*.cmake`, the build options in
`CMakeLists.txt` and the toolchain: changing the tag of an external project
rebuilds the prerequisites, a new run alone never does.

Keeping them on the nightly release means that the repository has a single
release. The assets are an implementation detail of the CI and can be deleted
at any time: they are regenerated on demand. The three most recent sets per
platform are kept and older ones are removed automatically; publishing the
nightly replaces its own assets but leaves these alone.

`Slicer-dependencies` is an aggregate target added by `SuperBuild.cmake`. It
depends on every entry of `Slicer_DEPENDENCIES` and `Slicer_REMOTE_DEPENDENCIES`,
so building it produces the same superbuild tree as a full build, minus the
inner `Slicer-build` directory.

The second phase restores that tree and runs the *same* superbuild
configuration. Because the external projects are already built and the
superbuild is configured with `Slicer_EP_UPDATE_DISCONNECTED`, their update,
configure and build steps are skipped, and only the inner `Slicer` target is
built. Nothing about the Superbuild structure changes: a developer running
`cmake --build .` in the same tree gets exactly the same behaviour.

### Fixed build paths

The CMake package configuration files exported by a Slicer build tree
(`SlicerConfig.cmake`, `SlicerTargets.cmake`, and those of VTK, ITK, CTK, ...)
embed absolute paths. A build tree is therefore only usable when restored at the
path it was built at. The workflow uses a short, fixed root per platform:

| Platform | Root | Source tree | Superbuild tree | `Slicer_DIR` |
|---|---|---|---|---|
| Linux | `/home/runner/S` | `/home/runner/S/Slicer` | `/home/runner/S/SR` | `/home/runner/S/SR/Slicer-build` |
| macOS (both) | `/Users/runner/S` | `/Users/runner/S/Slicer` | `/Users/runner/S/SR` | `/Users/runner/S/SR/Slicer-build` |
| Windows | `C:/S` | `C:/S/Slicer` | `C:/S/SR` | `C:/S/SR/Slicer-build` |

The paths are also short enough to satisfy the Windows path length limit
enforced by `PreventDirWithTooManyChars`.

macOS is two platforms, since nothing built for one architecture is usable by
the other: `macos` is the Intel build (`macos-15-intel` image, Qt 5.15.2), and
`macos-arm64` the Apple silicon one (`macos-15` image). Qt provides no arm64
build of Qt 5, so `macos-arm64` is built with Qt 6, the version of which is the
`macos-arm64-qt-version` input of `build-application.yml`. The build tree
records the Qt version it was built with, and an extension built against it
installs the same one. Both use the same root: they never share a machine.

### Artifacts

Each successful build publishes:

| Artifact | Content | When |
|---|---|---|
| `package-<platform>` | The installer produced by CPack | Always |
| `test-results-<platform>` | The JUnit report and the CTest logs | Always |
| `logs-<platform>` | The packaging log | Always |
| `build-<platform>` | `manifest-<platform>.json`, plus the source tree, the inner build tree and the prerequisites, as `zstd` archives | Only when publishing |

Publishing does not wait on the tests. A package that was built and verified is
published even when the suite reports failures, because withholding it would
leave extension repositories building against a stale nightly over test
problems they cannot act on. The run still turns red.

Nor does a platform wait for the others: each publishes its package, its build
trees and its manifest to the `nightly` release at the end of its own job, and
tells the `Reconcile extensions` workflow, which starts the extensions of that
platform. A platform whose build failed keeps what the release held of it, so
the release can hold platforms built from different commits for a while;
`manifest-<platform>.json` says which, and the extensions of a platform are
built against the commit of that platform.

Archiving a build tree takes about fifteen minutes, so it is done only for the
runs that publish: the scheduled one, a build of the default branch, and a
manual run asking for it.

## Building a custom application

A custom application based on Slicer, whose top-level `CMakeLists.txt` fetches
the Slicer sources and adds them with `add_subdirectory` as in the
[SlicerCustomAppTemplate](https://github.com/KitwareMedical/SlicerCustomAppTemplate),
builds its installers by calling `build-application.yml`:

```yaml
name: Build installers
on:
  push:
    branches: [main]
  workflow_dispatch:
permissions:
  contents: read
jobs:
  build:
    uses: Slicer/Slicer/.github/workflows/build-application.yml@main
    permissions:
      contents: write       # the prerequisites are stored on a release
      id-token: write       # to attest the packages it publishes; a called
      attestations: write   # workflow cannot ask for more than it is granted
    with:
      name: MyApp
      ci-ref: main      # the same ref as on the `uses:` line
      run-tests: false
      extra-cmake-options: "-DBUILD_TESTING:BOOL=OFF"
```

The installers are uploaded as the `package-<platform>` artifacts of the run;
the calling workflow decides whether to attach them to a release.

The application builds its own prerequisites, the first time and whenever its
top-level `CMakeLists.txt` or its `SuperBuild/` directory changes, and keeps
them on a `prerequisites` pre-release of its own repository
(`prerequisites-release` changes the name). It never uses those of Slicer: its
options and its Slicer revision differ. That file pins the Slicer revision, so
pin it by commit: a branch name is not followed, and moving the branch does not
rebuild the prerequisites (bump `deps-key-version` to force it).

The prerequisites need a Slicer revision whose `SuperBuild.cmake` defines the
`Slicer-dependencies` target.

`ci-ref` has to be given because the actions and scripts the workflow runs are
checked out separately: the local actions of a called workflow resolve against
the checkout of the calling repository. The other inputs, described in
`build-application.yml`, select the platforms, the Qt version, the macOS signing
identity (with the signing secrets passed by the caller) and whether to run the
tests.

## Building an extension against the latest nightly

Call the `extension-build` workflow. It restores the published build tree,
configures, builds, tests and packages the extension, loads the result into
Slicer and uploads the package. The extension repository carries no build steps
of its own and never checks Slicer out. What it carries says when to build and
what it lets the build do, and nothing of how, so that it does not have to
change when the workflow does:

```yaml
# .github/workflows/slicer-nightly-build.yml
name: Build against the Slicer nightly

on:
  push:
  pull_request:
  workflow_dispatch:
    inputs:
      platforms:
        description: "Platforms to build (default: all)"

jobs:
  build:
    permissions:
      contents: write
    uses: Slicer/Slicer/.github/workflows/extension-build.yml@main
```

A commit to the default branch and a manual run build, test and publish the
packages (see below); a pull request builds and tests, and is never published.
A push to another branch builds nothing, its pull request does: the run it
starts ends at once. `push-branches` names other branches to build. Pass
`publish: false` to only build and test; `permissions` stays, as the workflow
declares the permission whether it publishes or not, and a run is refused when
its caller does not grant what it declares.

The extension is built for every platform the Slicer nightly has a build for.
A manual run can be given the platforms to build, which is how the Slicer
repository starts one run per platform; to always build for some platforms
only, pass them: `platforms: linux,windows`.

`web` builds the extension for [SlicerWeb](https://github.com/lassoan/SlicerWeb),
Slicer compiled to WebAssembly, against the web build tree the nightly
publishes (`slicerweb-build.tar.zst`): in SlicerWeb's toolchain container, with
SlicerWeb's own extension stage, into a Pyodide wheel. When SlicerWeb's catalog
(its `extensions/` folder) describes the extension, that description is used:
its build subdirectory, its dependencies and its browser-specific options.
Otherwise the extension is built with `web-build-subdirectory` and the
dependencies its `CMakeLists.txt` names (`EXTENSION_DEPENDS`). Either way the
dependencies are provided as on the desktop platforms (see below). The
extension has to be at the top of its repository (`source-directory: .`).

Inputs:

| Input | Default | Description |
|---|---|---|
| `platforms` | what a manual run is given, or else all the Slicer release has | Platforms to build on, comma separated: `linux`, `macos` (Intel), `macos-arm64` (Apple silicon), `windows`, `web` |
| `push-branches` | the default branch | Branches whose commits are built when pushed, comma separated; `*` for every branch |
| `source-directory` | `.` | Directory of the extension within the repository |
| `extension-name` | repository name | CMake project name of the extension |
| `cmake-options` | | Additional options for the configure step |
| `source-ref` | | Branch, tag or commit of the extension to build instead of the one the workflow runs for, to try a branch that does not have the workflow. Not published |
| `source-repository` | this one | Repository of `source-ref`, e.g. the one this is a fork of |
| `generator` | Ninja | CMake generator on Linux and macOS. `Unix Makefiles` is what the Slicer extension build uses, for an extension that does not build with Ninja |
| `run-tests` | `true` | Run the extension's tests |
| `artifact-prefix` | repository name | Name of the package artifact, without the platform |
| `submodules` | `false` | Passed to the checkout action |
| `slicer-repository` | `Slicer/Slicer` | Repository publishing the build |
| `slicer-ref` | `main` | Ref of Slicer providing the actions and the helper script; pin it to whatever the workflow is pinned to |
| `release-tag` | `nightly` | Release to build against |
| `web-build-subdirectory` | what SlicerWeb's catalog says, or `.` | For `web`, where the extension itself is built: `inner-build` with a superbuild, `.` without. A value given here is used instead of the catalog's |
| `publish` | `true` | Publish the packages to a release of the extension's own repository. A pull request is never published |
| `publish-tag` | `nightly` | Tag of that release |
| `publish-build-tree` | `false` | Publish the build tree with the package, for the extensions that depend on this one |

### Publishing the packages

Unless `publish` is `false`, the packages are published to a release of the
extension's own repository, the way Slicer publishes its nightly. A called
workflow cannot grant itself more than its caller has, so the calling job
grants `contents: write`, as in the workflow above. Every run that builds then
publishes, except those of a pull request and those given a `source-ref`: the
commits to the default branch, and the manual runs, which the Slicer
repository starts for a new nightly, one per platform.

The packages go to the `nightly` release of the extension's repository, with
one description per platform, `extension-<platform>.json`, which says what the
package was built from and against:

```json
{"name": "SlicerIGT", "repository": "SlicerIGT/SlicerIGT", "platform": "linux", "date": "...",
 "package": {"file": "...", "sha256": "...", "revision": "<commit of the extension>", "date": "...",
             "slicer_commit": "<commit of Slicer>", "slicer_revision": "34012",
             "tests": "passed", "metadata": {"...": "..."}, "depends": ["SlicerIGSIO"],
             "built_with": {"SlicerIGSIO": {"repository": "IGSIO/SlicerIGSIO", "revision": "...",
                                            "package": "...", "sha256": "..."}},
             "build_tree": {"files": ["SlicerIGT-build-linux.tar.zst"], "sha256": {"...": "..."},
                            "config_dir": "E/SlicerIGT/inner-build", "subdirectory": "inner-build"}},
 "previous": {"...": "the package built against the Slicer before"}}
```

`metadata` is what the extension says of itself (description, category, ...),
for Slicer's Extensions Manager. A package published with an icon file, as a
web package can be, names it as `icon`.

There is one per platform so that a platform is published as soon as it is
built, by a run of its own, without waiting for the others. The one of `web`
also holds the extension's entry of SlicerWeb's extension index.

A platform is published when it built, packaged and loaded. Failing tests fail
the run, and are noted in the release, but withhold nothing; a platform that
did not build keeps the package the release had. When Slicer moves on, the
package built against the Slicer before is kept as `previous` until the next
one replaces it, so that the Slicer builds still in use are not left without.

### Extensions that depend on other extensions

The extensions an extension depends on (`EXTENSION_DEPENDS` of its
`CMakeLists.txt`, and theirs in turn) are treated the way an extension treats
Slicer: they are not built again, their published build tree is restored. An
extension that others depend on sets `publish-build-tree: true`, and is built
at fixed paths (`<root>/E/<Name>`, from a copy of its source in
`<root>/ES/<Name>`; `/build/ext/<Name>` for the web), since the files CMake
finds a build tree by record absolute paths. An extension that depends on it
downloads that tree for its platform, checks it against the hash in
`extension-<platform>.json`, is handed it as `<Name>_DIR`, and records the package it was
built against in `built_with`.

So what an extension is linked against is, byte for byte, what its users
install beside it, and nothing else is built against. A dependency that
publishes no build tree fails the build, which says so. When a dependency has
not been built against the same Slicer commit yet, as happens while a nightly
is being followed, the run of a commit or of a pull request skips that
platform, with a notice, and stays green; a manual run fails (the Slicer
repository starts those only once the dependency is there). Only a dependency
that no registered repository builds is built from source, in the same job,
from its entry in the
[ExtensionsIndex](https://github.com/Slicer/ExtensionsIndex).

That makes the order of the builds matter, and a change in an extension a
reason to build the ones that depend on it again. No extension repository can
start a build in another, so the Slicer repository does it, with the
[`Reconcile extensions`](https://github.com/Slicer/Slicer/actions/workflows/extension-reconcile.yml)
workflow. From what is published, and nothing else, it works out which
extensions are not *current*, that is, built against the Slicer commit of the
nightly, from the head of their branch, and against the very packages their
dependencies publish now. Each platform is a chain of its own: the build of an
extension for a platform is started, as a manual run of the extension's
workflow with that platform as its `platforms` input, as soon as the
extensions it depends on are current there. A build that fails is started once
more, since runners, downloads and tests fail for reasons that are not the
extension's; after that it is left until Slicer, the extension or what it
depends on changes, or the workflow is run with `retry`, and what depends on
it is left as it is.

The workflow publishes `extensions-catalog.json` to the nightly release:

```json
{"platforms": {"linux": {"slicer_commit": "...", "extensions": {
     "SlicerIGT": {"repository": "...", "revision": "...", "file": "...", "url": "...",
                   "sha256": "...", "depends": ["SlicerIGSIO"], "tests": "passed", "tier": 5}}},
               "web": {"...": "..."}},
 "withheld": {"linux": {"SlicerHeart": "SlicerIGT is not current (...)"}},
 "extensions": {"SlicerIGT": {"repository": "...", "branch": "...", "head": "...", "tier": 5,
                              "attempts": {"linux": {"run": "...", "conclusion": "success"}}}}}
```

`tier` says whether the extension is reviewed. Installing the Slicer nightly
app is all it takes for a repository to be built and listed, so being listed
says nothing about it. An extension described in the
[Slicer ExtensionsIndex](https://github.com/Slicer/ExtensionsIndex) has the
tier given there (1 to 5), provided the repository that builds it is the one
its description names (`scm_url`): the tier is that of the repository, and a
repository that takes the name of a reviewed extension does not take its
tier. Every other extension is of tier 0, not reviewed, and it is for what
reads the catalog to decide what to do with those: the SlicerWeb application
offers them only to a user who asks for them. A fork is not the repository it
copies; the forks an organization builds its extensions from are named in the
`SLICER_EXTENSION_TRUSTED_FORKS` repository variable
(`{"fork": "repository"}`), and the catalog then says which repository each
stands for (`reviewed_as`).

This one file is what an Extensions Manager reads: per platform, the
extensions built against the Slicer commit of the nightly and against the
packages their dependencies publish, which are consistent with each other by
construction, with the address and the hash of each package. A newer commit of
an extension is a reason to build it again, not to withdraw it, and what
depends on it, meanwhile. The file is replaced at once, so a reader sees the
set before or the set after, whatever is being built meanwhile.
`extension-repositories.json`, on the same release, lists the repositories:
those the Slicer nightly GitHub App is installed in that have
`slicer-nightly-build.yml` (see below).

The workflow remembers nothing between runs, so running it late, twice or
after a failure does no harm. It is started when a platform of the nightly is
published, when a platform of an extension is (through the relay described
below), every six hours, and by hand; a run stays for as long as builds are
going, and starts what depended on each as it publishes. A commit to an
extension is built by the extension's own workflow, on the push, and the
extensions that depend on it follow in the same way. A platform never waits
for another, and a publish over a newer commit of the branch is skipped.

A superbuild extension needs nothing extra: its own tree, `inner-build`, is
where the tests, the package and the modules are looked for.

Loading the extension into Slicer is what proves the two are compatible. A
mismatch between the extension and the build it was made against shows up
there rather than at build time.

The [`Extension build (self-test)`](https://github.com/Slicer/Slicer/actions/workflows/extension-build-test.yml)
workflow builds the templates under `Extensions/Testing` through this same
workflow, so what is verified is what extension repositories run.

### Doing the steps directly

An extension that needs something the workflow does not offer can use the
`setup-slicer-build` action on its own. It restores the nightly build tree at
the fixed path, installs the matching Qt and CMake, and outputs the value to
pass as `Slicer_DIR`:

```yaml
    steps:
      - uses: actions/checkout@v5

      - name: Set up Slicer build
        id: slicer
        uses: Slicer/Slicer/.github/actions/setup-slicer-build@main

      - name: Build
        shell: bash
        run: |
          cmake -S . -B ../build \
            -DSlicer_DIR:PATH=${{ steps.slicer.outputs.slicer-dir }} \
            -DCMAKE_BUILD_TYPE:STRING=Release
          cmake --build ../build --parallel
```

Outputs of the action:

| Output | Description |
|---|---|
| `slicer-dir` | Value to pass as `-DSlicer_DIR` |
| `source-dir` | The Slicer source tree the build was produced from |
| `superbuild-dir` | The superbuild tree |
| `root` | The fixed root directory |
| `platform` | `linux`, `macos`, `macos-arm64` or `windows` |
| `launcher` | The Slicer launcher executable in the build tree |
| `slicer-version`, `slicer-revision` | Version of the restored build |
| `qt-dir` | The Qt prefix used by the build |

The action installs everything the build needs: the system packages, CMake, the
matching Qt, and `ccache`. It also sets `Slicer_DIR` in the environment, so a
later step can use it without going through the output. It needs no checkout of
Slicer: it fetches the helper script it runs from the Slicer repository itself.

Inputs:

| Input | Default | Description |
|---|---|---|
| `repository` | `Slicer/Slicer` | Repository publishing the builds |
| `ref` | `main` | Branch or tag providing the helper script |
| `release-tag` | `nightly` | Release to restore |
| `qt-version` | from the manifest | Qt version to install |
| `cmake-version` | `3.31.x` | CMake version to install |
| `compiler-cache` | `true` | Enable the `ccache` compiler cache on Linux and macOS |
| `free-disk-space` | `false` | Remove unneeded pre-installed tooling |

The action puts the helper script in `SLICER_CI`, and the steps the reusable
workflow runs are its commands, so they can be used one at a time and run the
same way locally:

```yaml
      - run: '"$SLICER_CI" extension-configure'   # SLICER_EXT_SOURCE_DIR, SLICER_EXT_CMAKE_OPTIONS
      - run: '"$SLICER_CI" extension-build'
      - run: '"$SLICER_CI" extension-test'        # xvfb and software OpenGL as needed
      - run: '"$SLICER_CI" extension-package'
      - run: '"$SLICER_CI" extension-load'
```

Running the tests directly needs a display and OpenGL, which the runners lack;
`extension-test` is what arranges both, and on Windows the driver it uses is
installed by `"$SLICER_CI" install-software-opengl`.

### Building when a new nightly is published

The builds against a new nightly are started by the Slicer repository: its
`Reconcile extensions` workflow starts the workflow of each extension
repository through the Slicer nightly GitHub App, in the order of their
dependencies (see above). For a repository to be built that way:

1. Name the workflow `.github/workflows/slicer-nightly-build.yml` and give it a
   `workflow_dispatch` trigger with no required inputs, as in the example
   above; its optional `platforms` input lets each platform be a run of its
   own.
2. Install the Slicer nightly GitHub App on the repository.

Nothing has to be registered with the Slicer repository: the workflow lists
the repositories the app is installed on and starts that workflow on the
default branch of each. Repositories without it are skipped. Uninstalling the
app opts out.

The repository of a SlicerWeb site follows the web runtime in the same way,
with a workflow named `.github/workflows/slicer-nightly-web.yml` instead: it is
started when the catalog changed, which a new web runtime
(`slicerweb-wheels.zip`) makes it do, after the extensions have been built
against it.

The app holds the Actions permission, which starts workflows but cannot change
code, and read access to the repository's contents, used only to see whether
the workflow exists. It is a separate app from the one the Slicer repository
uses for its own automation, so installing it grants nothing more.

To try this on forks, create a private GitHub App with these permissions,
install it on the fork of the extension, and set its ID and private key as the
`SLICER_NIGHTLY_APP_ID` variable and `SLICER_NIGHTLY_APP_PRIVATE_KEY` secret of
the fork of Slicer. Without them the `Reconcile extensions` job is skipped.

A relay makes this prompt: a small Cloudflare Worker, in
`.github/extension-webhook/`, that the webhook of the Slicer nightly app points
to. When a platform of an extension has been built on the branch the extension
is built from, when a build of one ends without success, or when the app is installed in more
repositories, it starts `Reconcile extensions`, unless a run of it is going
already. It does so as a second app, the relay app, installed in the Slicer
repository alone, so that the private key of the nightly app, which starts
builds in every extension repository, stays a secret of the Slicer repository.
Without the relay everything works the same, later: the schedule is the
backstop.

To set the relay up:

1. Create the relay app (no webhook; repository permission *Actions: read and
   write*), install it in the Slicer repository only, and convert its private
   key to PKCS#8 (`openssl pkcs8 -topk8 -nocrypt -in relay.pem`).
2. Fill in `RELAY_APP_ID`, `SLICER_REPOSITORY` and `SLICER_REF` in
   `wrangler.toml` and run `npx wrangler deploy`, then `npx wrangler secret
   put` for `WEBHOOK_SECRET` and `RELAY_APP_PRIVATE_KEY`.
3. In the settings of the Slicer nightly app, set the webhook URL to the
   Worker's, the same secret, and subscribe to *Workflow job* and *Workflow run*.

GitHub lists each delivery and its answer in the nightly app's *Advanced*
settings, and can deliver one again from there.

The Worker also starts the nightly build on time. GitHub starts scheduled runs
late, by hours at times, so the cron trigger of `wrangler.toml` starts the
workflow named by `NIGHTLY_WORKFLOW` (`build.yml`) on `SLICER_REF`, as the
relay app, with its `nightly` input: the run then behaves as the scheduled one
does. The schedule of the workflow stays for when the Worker is not there; a
run it starts later finds the nightly already built and stops within a minute.

The same Worker hands the web packages of the extensions to the pages that
install them (`GET /gh/<owner>/<repository>/nightly/<file>.whl`), since a page
cannot download a file of a GitHub release, which answers without the header
that lets another site read it. Only the wheels of the nightly release of the
repositories listed in `extension-repositories.json` are handed out, and the
page checks what it receives against the hash the catalog gives.

### Pinning to a specific build

Both the workflow and the action restore the `nightly` release by default. Pass
`release-tag` to pin to another release, and `slicer-repository` (`repository`
for the action) to consume a fork's releases:

```yaml
      - uses: Slicer/Slicer/.github/actions/setup-slicer-build@main
        with:
          repository: Slicer/Slicer
          release-tag: nightly
```

## Running the workflow manually

`Build` can be dispatched from the Actions tab with:

| Input | Description |
|---|---|
| `platforms` | Comma separated subset of `linux,macos,macos-arm64,windows,web` |
| `run-tests` | Run the test suite |
| `publish` | Publish the result to the `nightly` release |
| `rebuild-prerequisites` | Ignore the prerequisites cache and rebuild them |
| `nightly` | Run as the scheduled nightly does: publish, build nothing when the nightly release already holds the commit, and let failing tests withhold nothing |
| `force` | With `nightly`, build even if the nightly release already holds the commit |

Pull requests build and test Linux, and build the web runtime; the package and
the runtime are artifacts of the run (`package-linux`, `package-web`). Pushes
to the default branch, the nightly schedule and manual runs build every
platform.

The scheduled run exists to keep the `nightly` release in step with the default
branch, so it stops early when no commit has landed since the last one: it
compares the commit recorded in the published manifest against the head of the
default branch and, when they match, builds nothing. Use the `force` input to
build the same commit again.

## Tests

The tests run in the job that produced the build, right after it and before
packaging: handing a 15 GB build tree to a second job costs more time than the
isolation is worth. A failure is recorded but does not stop the job, so the
package and the build tree are still produced; the job then fails at its last
step. The scheduled run never fails on test failures, so a nightly is always
published.

Every run publishes a `test-results-<platform>` artifact with the JUnit report
and the CTest logs, and writes a summary table to the run's page.

The runners have no GPU, and Slicer requires OpenGL even with
`--no-main-window`. On Linux the suite runs under `xvfb-run` with Mesa's software
renderer. On Windows, the OpenGL available without a GPU is too old for VTK to
create a render window, so a pinned build of Mesa is installed before the tests
(`slicer-ci.sh install-software-opengl`). macOS runners provide a session and
OpenGL, so they need no setup.

Test data is downloaded by `ExternalData` from `SlicerTestingData` and cached
between runs through the `ExternalData_OBJECT_STORES` environment variable.

To exclude known-failing tests without editing the workflow, set the
`SLICER_CI_TEST_EXCLUDE` repository variable to a CTest regular expression.

## Signing and notarizing the macOS package

macOS refuses to open an application that Apple has not notarized without a
warning, and notarization requires the package to be signed with a Developer ID
certificate. Both happen automatically when the secrets below exist, and are
skipped when they do not, so a fork builds without them. Without them the
bundle is still signed ad-hoc, which is enough to run locally, and is required
on Apple silicon, which refuses to execute unsigned code at all.

| Secret or variable | What it is |
|---|---|
| `MACOS_CERTIFICATE` | The Developer ID Application certificate, a `.p12` file, base64 encoded |
| `MACOS_CERTIFICATE_PASSWORD` | Its password |
| `MACOS_SIGNING_IDENTITY` (variable) | The identity name, for example `Developer ID Application: Example (TEAMID)` |
| `APPLE_API_KEY` | An App Store Connect API key, the `.p8` file, base64 encoded |
| `APPLE_API_KEY_ID`, `APPLE_API_ISSUER_ID` | That key's identifiers |

An API key is used rather than an Apple ID and password because it needs no
two-factor prompt.

The certificate is imported into a temporary keychain, `SLICER_CODESIGN_IDENTITY`
tells the bundle fixup to sign with it instead of ad-hoc, and the package is then
submitted with `notarytool` and stapled.

Signing with a real identity also turns on the hardened runtime and a secure
timestamp, both of which Apple requires. The hardened runtime blocks things
Slicer needs, so it is signed with the entitlements in
`CMake/SlicerMacOSEntitlements.plist`: loading Python modules and extensions
that carry a different signature, and the just-in-time compilation QtWebEngine
performs. Apple reports which entitlements a submission actually used, so that
list should be trimmed once the first notarization succeeds.

Every published package is also attested, by the job that built it and as
soon as it is published, with `actions/attest-build-provenance`, which records
how it was built and can be checked with `gh attestation verify`. That
is supply-chain evidence and has nothing to do with whether an operating system
will run the package.

## Relation to the older CI workflow

`.github/workflows/ci.yml` builds Slicer inside the `slicer/slicer-base`
Docker image, which ships a prebuilt superbuild. It only covers Linux, runs no
tests, and skips the build entirely whenever a file under `SuperBuild/` differs
from the `nightly-main` branch, because the image's prerequisites would then be
stale.

The `Build` workflow covers those cases: it builds the prerequisites itself
when they change, on every platform, and runs the tests. The two can be
run side by side while the new one settles; once it has, `ci.yml` and the
`slicer-build` action it uses can be removed.

## Compiler cache

The job that builds Slicer runs the compiler through `ccache`, restored from
the GitHub Actions cache. The prerequisites do not use it: they are built once
and then stored on the nightly release, so caching their objects would only displace the
entries that make the Slicer build faster.

`sccache` would be the more usual choice, but its GitHub Actions backend writes
one cache entry per compiled object. A full Slicer build leaves tens of
thousands of them in the repository, which makes the cache list unusable even
though their total size is small. `ccache` is stored as a single entry per
platform instead.

## Local reproduction

The workflow logic lives in `.github/scripts/slicer-ci.sh`, which can be run
locally:

```bash
export SLICER_ROOT=/home/me/S
export SLICER_QT_DIR=/opt/Qt/5.15.2/gcc_64
.github/scripts/slicer-ci.sh env
.github/scripts/slicer-ci.sh configure
.github/scripts/slicer-ci.sh build Slicer-dependencies
.github/scripts/slicer-ci.sh build Slicer
.github/scripts/slicer-ci.sh test
```

This is a thin wrapper around the standard Superbuild commands documented in
[the platform build instructions](./index.md); nothing about it is required to
build Slicer.
