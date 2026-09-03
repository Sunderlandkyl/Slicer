# Building with GitHub Actions

Slicer is built on Linux, macOS and Windows by the [`Build`](https://github.com/Slicer/Slicer/actions/workflows/build.yml)
workflow. This page describes how that workflow is organized, and how another
repository can build an extension against the resulting nightly build.

## How the build is organized

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

| Stage | External projects |
|---|---|
| 1 | Python (and the libraries it needs), DCMTK, curl, teem, LibArchive, RapidJSON, JsonCpp, TBB, the launcher |
| 2 | VTK |
| 3 | ITK, SlicerExecutionModel |
| 4 | CTK, qRestAPI |
| 5 | Everything remaining, through `Slicer-dependencies` |

A stage names the targets it wants; those that are not part of the current
configuration are skipped, using the list of external projects that
`SuperBuild.cmake` writes to `SlicerDependencies.txt` in the build tree. Adding
or removing an external project therefore does not require touching the
workflow, only rebalancing the stages if one of them becomes too long.

The finished prerequisites are published as the assets of a release named after
the key, for example `slicer-deps-linux-1a2b3c4d5e6f7890`. That release is an
implementation detail of the CI and can be deleted at any time: it is
regenerated on demand.

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

Archiving a build tree takes about fifteen minutes, so it is done only for the
runs that publish: the scheduled one, a build of the default branch, and a
manual run asking for it. Those assets are then attached to the `nightly`
release, which is what extension repositories consume.

## Building an extension against the latest nightly

Use the `setup-slicer-build` action. It restores the nightly build tree at the
fixed path, installs the matching Qt and CMake, and outputs the value to pass as
`Slicer_DIR`.

```yaml
name: Build extension

on: [push, pull_request]

jobs:
  build:
    strategy:
      matrix:
        include:
          - { platform: linux, runner: ubuntu-22.04 }
          - { platform: macos, runner: macos-15-intel }
          - { platform: windows, runner: windows-2022 }
    runs-on: ${{ matrix.runner }}
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

      - name: Package
        shell: bash
        run: cmake --build ../build --target package
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
matching Qt, and `sccache`. It also sets `Slicer_DIR` in the environment, so a
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
| `sccache` | `true` | Enable the compiler cache |
| `free-disk-space` | `false` | Remove unneeded pre-installed tooling |

To run an extension's tests, use the launcher under `xvfb` on Linux:

```yaml
      - name: Test
        shell: bash
        run: |
          export QTWEBENGINE_DISABLE_SANDBOX=1
          xvfb-run -a ctest --test-dir ../build --output-on-failure
```

The [`Extension build (self-test)`](https://github.com/Slicer/Slicer/actions/workflows/extension-build-test.yml)
workflow builds the templates under `Extensions/Testing` this way and is the
reference implementation.

### Pinning to a specific build

`setup-slicer-build` restores the `nightly` release by default. Pass
`release-tag` to pin to another release, and `repository` to consume a fork's
releases:

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

## Tests

The tests run in the job that produced the build, right after it and before
packaging: handing a 15 GB build tree to a second job costs more time than the
isolation is worth. A failure is recorded but does not stop the job, so the
package and the build tree are still produced; the job then fails at its last
step. The scheduled run never fails on test failures, so a nightly is always
published.

Every run publishes a `test-results-<platform>` artifact with the JUnit report
and the CTest logs, and writes a summary table to the run's page.

On Linux the suite runs under `xvfb-run` with a software OpenGL renderer, since
Slicer requires a display even with `--no-main-window`. macOS and Windows
runners provide a session, so no wrapper is needed.

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
