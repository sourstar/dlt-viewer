# Reproducible build container

Builds DLT Viewer without installing Qt, CMake or a compiler on the host.
The image is Fedora 42 and carries two toolchains:

| Target | Toolchain | Produced here |
|---|---|---|
| Windows x86_64 | mingw-w64 GCC 14 + Qt 6.10 (`mingw64-qt6-*`) | yes |
| Linux x86_64 | system GCC + Qt 6 | yes |
| Windows ARM64 | MSVC ARM64 + Qt ARM64 | **no** — see below |

## Build the image

```sh
docker build -t dlt-viewer-build:fedora42 -f docker/Dockerfile docker/
```

## Build

From the repository root:

```sh
# Windows x86_64 -> out/DLTViewer-windows-x86_64/ and .7z
docker run --rm -v "$PWD:/src" -v "$PWD/out:/out" dlt-viewer-build:fedora42 windows-x64

# Linux x86_64
docker run --rm -v "$PWD:/src" -v "$PWD/out:/out" dlt-viewer-build:fedora42 linux-x64

# unit tests
docker run --rm -v "$PWD:/src" -v "$PWD/out:/out" dlt-viewer-build:fedora42 tests
```

On Windows PowerShell, substitute `${PWD}`; from Git Bash prefix the command
with `MSYS_NO_PATHCONV=1` and pass a native path:

```sh
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "C:\path\to\dlt-viewer:/src" \
  -v "C:\path\to\dlt-viewer\out:/out" \
  dlt-viewer-build:fedora42 windows-x64
```

The Windows output directory is self-contained: `dlt-viewer.exe`,
`dlt-commander.exe`, the DLT plugins, the Qt DLLs, the Qt platform/style/
image/TLS plugins, and the mingw runtime. Unzip and run.

## Why Windows ARM64 is not built here

An ARM64 Windows binary needs an ARM64 Windows Qt build. Qt does not publish
one for mingw, and the MSVC ARM64 toolchain does not run on Linux, so there is
nothing to cross-compile against — building Qt itself for
`aarch64-w64-mingw32` first would take hours and tens of gigabytes.

ARM64 is instead built natively on GitHub's `windows-11-arm` runner by
`.github/workflows/BuildFork.yml`, which is the same approach upstream uses in
`BuildPR.yml`. Push this branch to a GitHub fork and run the
**Build Windows (fork)** workflow; it uploads both
`DLTViewer-perf-windows-x86_64` and `DLTViewer-perf-windows-arm64`.

Note that Windows on ARM runs x64 binaries under emulation, so the x86_64
build produced here does work on a Snapdragon machine — just slower than a
native ARM64 build, which matters for a tool whose problem is throughput.

## Notes on the CMake changes this needed

Cross-compiling the project to Windows from Linux required four fixes, all
no-ops for the existing MSVC and Linux builds:

* `scripts/{windows,linux}/version.cmake` selected themselves on `WIN32`,
  which CMake does not define until the first `project()` call. They now also
  honour the target system name and the `DLT_TARGET_IS_WINDOWS` environment
  variable. An environment variable is used rather than a `-D` cache entry
  because CMake can delete the cache and restart configure, which drops `-D`
  values but preserves the environment.
* `scripts/windows/version.cmake` called `parse_version.bat`; it now falls
  back to parsing `src/version.h` in CMake on non-Windows hosts.
* `toolchain-mingw64-dlt.cmake` re-resolves `CMAKE_RC_COMPILER` the way CMake
  does. Fedora's stock toolchain hardcodes `/usr/bin/...-windres` while CMake
  finds `/usr/sbin/...-windres`; when Qt6 enabled the RC language the mismatch
  triggered exactly the cache reset described above.
* `src/cmake/Windows.cmake` assumed the Qt SDK plugin layout
  (`<lib>/../plugins`) and now discovers the plugin directory, so
  distribution layouts such as `<prefix>/lib/qt6/plugins` also work.
