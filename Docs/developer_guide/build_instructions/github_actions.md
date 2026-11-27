# Building with GitHub Actions

Slicer is built on Linux, macOS and Windows by the [`Build`](https://github.com/Slicer/Slicer/actions/workflows/build.yml)
workflow. This page describes how that workflow is organized and how a custom
application based on Slicer can build its installers the same way.

## How the build is organized

The work is shared between three workflow files:

| File | Role |
|---|---|
| `build.yml` | Slicer's `Build` workflow: decides what to build and publishes the nightly release |
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

The scheduled run, a build of the default branch, and a manual run asking for
it also publish the package to the `nightly` release, with a
`manifest-<platform>.json` saying what it was built from.

Publishing does not wait on the tests. A package that was built and verified is
published even when the suite reports failures. The run still turns red.

Nor does a platform wait for the others: each publishes its package and its
manifest at the end of its own job. A platform whose build failed keeps what
the release held of it, so the release can hold platforms built from different
commits for a while; `manifest-<platform>.json` says which. The manifest is
removed before a platform's package is replaced and uploaded after it, so it
never describes a file that is not there. Each platform then writes the notes
of the release again, from the manifests the release holds: no job comes after
the platforms.

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
`build-application.yml`, select the platforms, the Qt version and whether to run
the tests.

## Running the workflow manually

`Build` can be dispatched from the Actions tab with:

| Input | Description |
|---|---|
| `platforms` | Comma separated subset of `linux,macos,windows` |
| `run-tests` | Run the test suite |
| `publish` | Publish the result to the `nightly` release |
| `rebuild-prerequisites` | Ignore the prerequisites cache and rebuild them |
| `nightly` | Run as the scheduled nightly does: publish, build nothing when the nightly release already holds the commit, and let failing tests withhold nothing |

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
package is still produced; the job then fails at its last
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
