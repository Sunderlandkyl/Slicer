# Building with GitHub Actions

Slicer is built on Linux, macOS and Windows by the [`Build`](https://github.com/Slicer/Slicer/actions/workflows/build.yml)
workflow. This page describes how that workflow is organized, how another
repository can build an extension against the resulting nightly build, and how
a custom application based on Slicer can build its installers the same way.

## How the build is organized

The work is shared between three workflow files:

| File | Role |
|---|---|
| `build.yml` | Slicer's `Build` workflow: decides what to build, publishes the nightly release and starts the extension builds |
| `build-application.yml` | Reusable: computes the prerequisites key, builds Slicer (or a custom application) and packages and tests it |
| `build-prerequisites.yml` | Reusable, called by the previous one only when a platform lacks its prerequisites: the five stages described below |

Since the stages are a workflow of their own, a run that reuses the
prerequisites shows a single skipped `Prerequisites` job.

A full Slicer Superbuild takes several hours and produces a build tree of
15 GB or more, which does not fit in a single GitHub-hosted runner job (limited
to 6 hours). The workflow therefore splits the work in two phases while keeping
the Superbuild structure intact:

| Phase | Target built | When it runs |
|---|---|---|
| Prerequisites | `Slicer-dependencies` | Only when the description of an external project changes |
| Slicer | `Slicer` | On every run |

A single job cannot build every external project within the 6 hour limit, so
the first phase is itself chained across five jobs, each building a group of
external projects in the tree produced by the previous one:

| Stage | Builds |
|---|---|
| 1 | What VTK and ITK need: `python` (with zlib, bzip2, LZMA, LibFFI, sqlite, OpenSSL), `DCMTK` (with OpenJPEG), `tbb` |
| 2 | `VTK` |
| 3 | `ITK` |
| 4 | `CTK` (with PythonQt and QtTesting) |
| 5 | Everything else, through `Slicer-dependencies` |

The split follows the critical path rather than a hand-picked grouping, which
is what makes it checkable: stage 1 is the prerequisites of VTK and ITK, stages
2 to 4 are the chain itself, and stage 5 is whatever is left.

```
zlib -> OpenSSL -> python -> VTK -> ITK -> CTK
                     DCMTK ---^-------^------^
```

`ITK` depends on `VTK` because Slicer enables `ITKVtkGlue`, so nothing on that
chain can overlap.

The rule matters when adding an external project. A project that VTK or ITK
needs belongs in stage 1; a project that merely *uses* them belongs in stage 5
and needs no change at all, since `Slicer-dependencies` covers everything.
Naming a consumer in an early stage silently pulls its dependencies in with it:
`teem` uses VTK, and naming it in stage 1 used to build the whole of VTK there,
leaving the stage called VTK with nothing to do.

Because a stage gets the whole dependency subtree of what it asks for, each one
reports the projects it actually completed, in its log and in the run summary,
taken from the build so it cannot drift from this table.

A stage names the targets it wants; those that are not part of the current
configuration are skipped, using the list of external projects that
`SuperBuild.cmake` writes to `SlicerDependencies.txt` in the build tree. Adding
or removing an external project therefore does not require touching the
workflow, only rebalancing the stages if one of them becomes too long.

The finished prerequisites are published as assets of the `nightly`
pre-release, named after the key, for example
`slicer-deps-linux-1a2b3c4d5e6f7890.tar.zst` (split into `.part-NN` files above
the 2 GiB asset limit). A `<key>.parts` list, uploaded last, marks a set as
complete. Every later run, on any branch, restores those assets instead of
rebuilding: the five stages are skipped entirely and the job that builds Slicer
downloads them directly. The prerequisites are rebuilt only when the key
changes, which happens when an external project or a configure option changes,
and never merely because a run is new.

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

The prerequisites are keyed on the content of `SuperBuild/`, `SuperBuild.cmake`,
`CMake/ExternalProject*.cmake`, the build options in `CMakeLists.txt` and the
toolchain. Changing the tag of an external project produces a new key and
rebuilds the prerequisites; nothing else does.

### Fixed build paths

The CMake package configuration files exported by a Slicer build tree
(`SlicerConfig.cmake`, `SlicerTargets.cmake`, and those of VTK, ITK, CTK, ...)
embed absolute paths. A build tree is therefore only usable when restored at the
path it was built at. The workflow uses a short, fixed root per platform:

macOS is built on the Intel image. Qt 5.15.2 has no arm64 build, and Slicer's
own arm64 recipe is still a work in progress that turns DICOM, SimpleITK and
internationalization off. GitHub provides the Intel image until August 2027,
by which time Slicer's arm64 support should have caught up.

| Platform | Root | Source tree | Superbuild tree | `Slicer_DIR` |
|---|---|---|---|---|
| Linux | `/home/runner/S` | `/home/runner/S/Slicer` | `/home/runner/S/SR` | `/home/runner/S/SR/Slicer-build` |
| macOS | `/Users/runner/S` | `/Users/runner/S/Slicer` | `/Users/runner/S/SR` | `/Users/runner/S/SR/Slicer-build` |
| Windows | `C:/S` | `C:/S/Slicer` | `C:/S/SR` | `C:/S/SR/Slicer-build` |

The paths are also short enough to satisfy the Windows path length limit
enforced by `PreventDirWithTooManyChars`.

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
platform. Linux and the web are out, with their extensions under way, while
Windows and macOS are still building. A platform whose build failed keeps what
the release held of it, so the release can hold platforms built from different
commits for a while; `manifest-<platform>.json` says which, and the extensions
of a platform are built against the commit of that platform. The manifest is
removed before a platform's files are replaced and uploaded after them, so it
never describes files that are not all there. A last job attests the packages
and writes the notes of the release.

Archiving a build tree takes about fifteen minutes, so it is done only for the
runs that publish: the scheduled one, a build of the default branch, and a
manual run asking for it. Those assets are then attached to the `nightly`
release, which is what extension repositories consume.

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
      contents: write   # the prerequisites are stored on a release
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

The stages need a Slicer revision whose `SuperBuild.cmake` defines the
`Slicer-dependencies` target and writes `SlicerDependencies.txt`.

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
of its own and never checks Slicer out:

```yaml
name: Build extension

on: [push, pull_request]

jobs:
  build:
    uses: Slicer/Slicer/.github/workflows/extension-build.yml@main
    with:
      platforms: linux,macos,windows,web
```

`web` builds the extension for [SlicerWeb](https://github.com/lassoan/SlicerWeb),
Slicer compiled to WebAssembly, against the web build tree the nightly
publishes (`slicerweb-build.tar.zst`): in SlicerWeb's toolchain container, with
SlicerWeb's own extension stage, into a Pyodide wheel. When SlicerWeb's catalog
(its `extensions/` folder) describes the extension, that description is used:
its build subdirectory, its dependencies, which are built first from their own
repositories, and its browser-specific options. Otherwise the extension is
built with `web-build-subdirectory` and no dependencies.

Inputs:

| Input | Default | Description |
|---|---|---|
| `platforms` | `linux` | Platforms to build on, comma separated: `linux`, `macos`, `windows`, `web` |
| `source-directory` | `.` | Directory of the extension within the repository |
| `extension-name` | repository name | CMake project name of the extension |
| `cmake-options` | | Additional options for the configure step |
| `build-dependencies` | `true` | Provide the extensions it depends on (`EXTENSION_DEPENDS`), see below |
| `run-tests` | `true` | Run the extension's tests |
| `package` | `true` | Build the extension's package |
| `load-in-slicer` | `true` | Load the built extension into Slicer |
| `upload-package` | `true` | Upload the package as a workflow artifact |
| `artifact-prefix` | repository name | Name of the package artifact, without the platform |
| `submodules` | `false` | Passed to the checkout action |
| `slicer-repository` | `Slicer/Slicer` | Repository publishing the build |
| `slicer-ref` | `main` | Ref of Slicer providing the actions and the helper script; pin it to whatever the workflow is pinned to |
| `release-tag` | `nightly` | Release to build against |
| `qt-version` | from the manifest | Qt version to install |
| `web-build-subdirectory` | | For `web`, of an extension SlicerWeb's catalog does not describe: `inner-build` with a superbuild, `.` without |
| `publish` | `false` | Publish the packages to a release of the extension's own repository |
| `publish-tag` | `nightly` | Tag of that release |
| `publish-build-tree` | `false` | Publish the build tree with the package, for the extensions that depend on this one |
| `timeout-minutes` | `120` | Timeout of each desktop platform's job |
| `web-timeout-minutes` | `240` | Timeout of the web platform's job |

### Publishing the packages

The packages can be published to a release of the extension's own repository,
the way Slicer publishes its nightly. A called workflow cannot grant itself
more than its caller has, so the calling job passes `contents: write`:

```yaml
jobs:
  build:
    permissions:
      contents: write
    uses: Slicer/Slicer/.github/workflows/extension-build.yml@main
    with:
      platforms: linux,macos,windows
      # A commit to the default branch, and the builds the Slicer repository
      # starts (see below), publish; a pull request or another branch builds
      # and tests.
      publish: >-
        ${{ github.event_name == 'workflow_dispatch'
            || (github.event_name == 'push'
                && github.ref_name == github.event.repository.default_branch) }}
```

with, for the Slicer repository to start one run per platform:

```yaml
on:
  workflow_dispatch:
    inputs:
      platforms:
        default: ""
# ...
      platforms: ${{ inputs.platforms || 'linux,macos,windows' }}
```

The packages go to the `nightly` release of the extension's repository, with
one description per platform, `extension-<platform>.json`, which says what the
package was built from and against:

```json
{"name": "SlicerIGT", "repository": "SlicerIGT/SlicerIGT", "platform": "linux",
 "package": {"file": "...", "sha256": "...", "revision": "<commit of the extension>",
             "slicer_commit": "<commit of Slicer>", "slicer_revision": "34012",
             "tests": "passed", "depends": ["SlicerIGSIO"],
             "built_with": {"SlicerIGSIO": {"repository": "IGSIO/SlicerIGSIO", "revision": "...",
                                            "package": "...", "sha256": "..."}},
             "build_tree": {"files": ["SlicerIGT-build-linux.tar.zst"], "sha256": {"...": "..."},
                            "config_dir": "E/SlicerIGT/inner-build", "subdirectory": "inner-build"}},
 "previous": {"...": "the package built against the Slicer before"}}
```

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
install beside it. There is no fallback: when the dependency has not been
built against the same Slicer commit, or publishes no build tree, the build
fails and says so. Only a dependency that no registered repository builds is
built from source, in the same job, from its entry in the
[ExtensionsIndex](https://github.com/Slicer/ExtensionsIndex).

That makes the order of the builds matter, and a change in an extension a
reason to build the ones that depend on it again. No extension repository can
start a build in another, so the Slicer repository does it, with the
[`Reconcile extensions`](https://github.com/Slicer/Slicer/actions/workflows/extension-reconcile.yml)
workflow. From what is published, and nothing else, it works out which
extensions are not *current*, that is, built against the Slicer commit of the
nightly, from the head of their branch, and against the very packages their
dependencies publish now. Each platform is a chain of its own: the build of
an extension for a platform is started, as a run of its own (the extension's
workflow takes the platform as its `platforms` input), as soon as the
extensions it depends on are current on that platform, whatever the other
platforms are doing, and the catalog below is published again each time one
finishes. An extension is
started once for the same Slicer, head and dependencies: one that fails is not
started again until one of those changes (or the workflow is run with
`retry`), and what depends on it is left as it is.

It then publishes `extensions-catalog.json` to the nightly release:

```json
{"platforms": {"linux": {"slicer_commit": "...", "extensions": {
     "SlicerIGT": {"repository": "...", "revision": "...", "file": "...", "url": "...",
                   "sha256": "...", "depends": ["SlicerIGSIO"], "tests": "passed"}}},
               "web": {"...": "..."}},
 "withheld": {"linux": {"SlicerHeart": "SlicerIGT is not current (...)"}},
 "extensions": {"SlicerIGT": {"repository": "...", "branch": "...", "head": "...",
                              "attempt": {"run": "...", "conclusion": "success"}}}}
```

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
after a failure does no harm, and what starts it only decides how soon a
change is seen. There are two ways a chain starts:

- **A nightly.** Each platform of Slicer publishes as soon as it is built, and
  says so. The extensions that depend on no other are started for that
  platform; when one has published, those that depend on it are, provided what
  they depend on is current there; and so on, until none is left.
- **A commit to an extension.** The extension builds it itself: its workflow
  runs on the push and, on the default branch, publishes each platform as soon
  as it is built, from the job that built it. When a platform is published,
  the extensions that depend on it are started for that platform, provided
  what they depend on is current there; and so on.

In both, a platform never waits for another, and a publish over a newer commit
of the branch is skipped.

What tells the Slicer repository that a platform of an extension has been
published is the relay of the app's webhook (see below). With it (the
`SLICER_EXTENSION_RELAY` variable of the Slicer repository set to `true`),
each run of `Reconcile extensions` is one step: it starts what can be started
and stops, so nothing waits on a runner, and the builds a step started are
taken up by the next one. Without it, a run waits for the builds and goes down
the chains itself, reading what the extensions have published as it goes; it
is then started by the nightly, a schedule every six hours, or by hand, so a
commit to an extension reaches what depends on it at the next of those. An
extension with several dependencies is started once all of them are current,
not once per dependency. A build that fails is started once more, since
runners, downloads and tests fail for reasons that are not the extension's;
after that it is left until Slicer, the extension or what it depends on
changes, or the workflow is run with `retry`, and what depends on it is left
as it is. The `wait` input runs the whole chain in one run all the same.

A commit or a pull request to an extension is not at fault when what it
depends on is not built for the current Slicer yet, as happens while a nightly
is being followed: that platform is then skipped, with a notice, and the run
stays green. It is built when the dependency is there.

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

      - name: Set up the Slicer build
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
| `platform` | `linux`, `macos` or `windows` |
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

An extension repository decides for itself when to build against a new nightly.
Two ways, which can be combined.

**Poll, the supported way.** A scheduled run asks what the published revision
is, and stops when it has already built it. This needs no coordination with the
Slicer repository and no credentials, which is why it is the recommended way
for the extensions of the catalog.

```yaml
on:
  push:
  pull_request:
  schedule:
    - cron: "0 12 * * *"          # after the Slicer nightly
  workflow_dispatch:               # see "Be told" below

jobs:
  check:
    runs-on: ubuntu-latest
    outputs:
      built: ${{ steps.built.outputs.cache-hit }}
      revision: ${{ steps.nightly.outputs.revision }}
    steps:
      # 2 kB, rather than restoring the whole build tree to find out.
      - name: Read the published revision
        id: nightly
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release download nightly -R Slicer/Slicer -D . --clobber \
            -p manifest-linux.json
          rev=$(sed -n 's/^  "slicer_revision": "\(.*\)",$/\1/p' manifest-linux.json)
          echo "revision=$rev" >> "$GITHUB_OUTPUT"

      # A hit means this extension revision was already built against this
      # Slicer revision.
      - name: Have we built this already?
        id: built
        uses: actions/cache/restore@v4
        with:
          path: .built-marker
          key: built-${{ github.sha }}-slicer-${{ steps.nightly.outputs.revision }}
          lookup-only: true

  build:
    needs: check
    if: needs.check.outputs.built != 'true'
    uses: Slicer/Slicer/.github/workflows/extension-build.yml@main
    with:
      platforms: linux,macos,windows

  record:
    needs: [check, build]
    runs-on: ubuntu-latest
    steps:
      - run: date -u > .built-marker
      - uses: actions/cache/save@v4
        with:
          path: .built-marker
          key: built-${{ github.sha }}-slicer-${{ needs.check.outputs.revision }}
```

**Be told.** For a repository that wants to build as soon as the nightly lands
rather than waiting for its own schedule:

1. Name the workflow `.github/workflows/slicer-nightly-build.yml` and give it a
   `workflow_dispatch` trigger, as above, with no required inputs.
2. Install the Slicer nightly GitHub App on the repository.

Nothing has to be registered with the Slicer repository: once a nightly is
published, the `Reconcile extensions` workflow lists the repositories the app
is installed on and starts that workflow on the default branch of each, in the
order of their dependencies (see above). Repositories without it are skipped.
Uninstalling the app opts out.

The repository of a SlicerWeb site follows the web runtime in the same way,
with a workflow named `.github/workflows/slicer-nightly-web.yml` instead: it is
started when the catalog changed, which a new web runtime
(`slicerweb-wheels.zip`) makes it do, after the extensions have been built
against it.

The app holds the Actions permission, which starts workflows but cannot change
code, and read access to the repository's contents, used only to see whether
the workflow exists. It is a separate app from the one the Slicer repository
uses for its own automation, so installing it grants nothing more.

The relay is what makes this prompt: a small Cloudflare Worker, in
`.github/extension-webhook/`, that the webhook of the Slicer nightly app points
to. The app is subscribed to the *Workflow job* and *Workflow run* events,
which its Actions permission already allows, so the installations are asked
for nothing more. When the job of a platform of an extension ends on the
branch the extension is built from, when a build of one ends, or when the app
is installed in more repositories, the relay starts `Reconcile extensions`.
It does so as a second app, the relay app, installed in the Slicer repository
alone with the Actions permission. The relay then holds the webhook secret of
the nightly app, to know that an event is from GitHub, and the private key of
the relay app, and nothing else: the private key of the nightly app, which
starts builds in every extension repository, stays a secret of the Slicer
repository's workflows, and the nightly app needs no installation in the
Slicer repository. The relay runs just as well with the key of the nightly app
itself, installed in the Slicer repository too, which saves an app when trying
this on forks; it should not be set up that way for real, since that key then
lives outside GitHub. The schedule is only a backstop: GitHub starts scheduled
runs late, or not at all, under load.

To set the relay up:

1. Create the relay app (no webhook; repository permission *Actions: read and
   write*), install it in the Slicer repository only, and convert its private
   key to PKCS#8 (`openssl pkcs8 -topk8 -nocrypt -in relay.pem`).
2. Fill in `RELAY_APP_ID`, `SLICER_REPOSITORY` and `SLICER_REF` in
   `wrangler.toml` and run `npx wrangler deploy`, then `npx wrangler secret
   put` for `WEBHOOK_SECRET` and `RELAY_APP_PRIVATE_KEY`.
3. In the settings of the Slicer nightly app, set the webhook URL to the
   Worker's, the same secret, and subscribe to *Workflow job* and *Workflow run*.
4. Set the `SLICER_EXTENSION_RELAY` variable of the Slicer repository to
   `true`, so that a run of `Reconcile extensions` is one step.

GitHub lists each delivery and its answer in the nightly app's *Advanced*
settings, and can deliver one again from there.

To try this on forks, create a private GitHub App with these permissions,
install it on the fork of the extension, and set its ID and private key as the
`SLICER_NIGHTLY_APP_ID` variable and `SLICER_NIGHTLY_APP_PRIVATE_KEY` secret of
the fork of Slicer. Without them the step is skipped.

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
| `platforms` | Comma separated subset of `linux,macos,windows` |
| `run-tests` | Run the test suite |
| `publish` | Publish the result to the `nightly` release |
| `rebuild-prerequisites` | Ignore the prerequisites cache and rebuild them |

Pull requests build and test Linux only. Pushes to the default branch and the
nightly schedule build all three platforms.

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

Every published package is also attested, in the publish job, with
`actions/attest-build-provenance`, which records how it was built and can be
checked with `gh attestation verify`. That
is supply-chain evidence and has nothing to do with whether an operating system
will run the package.

## Relation to the older CI workflow

`.github/workflows/ci.yml` builds Slicer inside the `slicer/slicer-base`
Docker image, which ships a prebuilt superbuild. It only covers Linux, runs no
tests, and skips the build entirely whenever a file under `SuperBuild/` differs
from the `nightly-main` branch, because the image's prerequisites would then be
stale.

The `Build` workflow covers those cases: it builds the prerequisites itself
when they change, on the three platforms, and runs the tests. The two can be
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
