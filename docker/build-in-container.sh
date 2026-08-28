#!/usr/bin/env bash
# Build entrypoint executed inside the dlt-viewer build container.
#   build-in-container.sh windows-x64   -> cross-compiled Windows x86_64 build
#   build-in-container.sh linux-x64     -> native Linux x86_64 build
#   build-in-container.sh tests         -> native build + ctest
# Source tree is expected at /src (read-write bind mount); packaged output
# lands in /out.
set -euo pipefail

TARGET="${1:-windows-x64}"
SRC=/src
OUT=/out
NPROC="$(nproc)"

mkdir -p "$OUT"

common_flags=(
  -DCMAKE_BUILD_TYPE=Release
  -DDLT_PARSER=OFF
)

case "$TARGET" in
  windows-x64)
    BUILD_DIR="$SRC/build/windows-x64"
    INSTALL_DIR="$BUILD_DIR/install"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    # DLT_TARGET_IS_WINDOWS is exported, not passed with -D: the version
    # scripts read it before the first project() call, and an environment
    # variable also survives a configure restart. Toolchain: see
    # docker/toolchain-mingw64-dlt.cmake.
    export DLT_TARGET_IS_WINDOWS=1

    cmake -S "$SRC" -B "$BUILD_DIR" -G Ninja \
      "${common_flags[@]}" \
      -DCMAKE_TOOLCHAIN_FILE=/usr/local/lib/dlt-build/toolchain-mingw64-dlt.cmake \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DQT_HOST_PATH=/usr \
      -DDLT_USE_QT_RPATH=OFF \
      -DDLT_APP_DIR_NAME=. \
      -DDLT_EXECUTABLE_INSTALLATION_PATH=. \
      -DDLT_LIBRARY_INSTALLATION_PATH=. \
      -DDLT_RESOURCE_INSTALLATION_PATH=. \
      -DDLT_PLUGIN_INSTALLATION_PATH=plugins

    cmake --build "$BUILD_DIR" -j "$NPROC"
    cmake --install "$BUILD_DIR"

    STAGE="$OUT/DLTViewer-windows-x86_64"
    rm -rf "$STAGE"; mkdir -p "$STAGE"
    cp -a "$INSTALL_DIR/." "$STAGE/"
    bash /usr/local/lib/dlt-build/collect-mingw-deps.sh "$STAGE"

    # Strip the packaged binaries. The build tree keeps its symbols for
    # debugging; only the copies that ship get stripped. Distribution Qt and
    # mingw DLLs are already stripped, so this mostly affects our own targets.
    before=$(du -sb "$STAGE" | cut -f1)
    find "$STAGE" -type f \( -name '*.exe' -o -name '*.dll' \) -print0 \
        | xargs -0 -r x86_64-w64-mingw32-strip --strip-all 2>/dev/null || true
    after=$(du -sb "$STAGE" | cut -f1)
    echo "strip: $((before/1024)) KiB -> $((after/1024)) KiB (saved $(((before-after)/1024)) KiB)"

    ( cd "$OUT" && rm -f DLTViewer-windows-x86_64.7z \
        && 7za a -bd -mx=7 "DLTViewer-windows-x86_64.7z" "DLTViewer-windows-x86_64" >/dev/null )
    echo "=== Windows x86_64 artifacts ==="
    find "$STAGE" -maxdepth 1 -name '*.exe' -printf '%f  %s bytes\n'
    ls -lh "$OUT/DLTViewer-windows-x86_64.7z"
    ;;

  linux-x64)
    BUILD_DIR="$SRC/build/linux-x64"
    INSTALL_DIR="$BUILD_DIR/install"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    cmake -S "$SRC" -B "$BUILD_DIR" -G Ninja \
      "${common_flags[@]}" \
      -DDLT_USE_QT_RPATH=ON \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DDLT_APP_DIR_NAME=DLTViewer \
      -DDLT_EXECUTABLE_INSTALLATION_PATH=DLTViewer/usr/bin \
      -DDLT_LIBRARY_INSTALLATION_PATH=DLTViewer/usr/lib \
      -DDLT_RESOURCE_INSTALLATION_PATH=DLTViewer/usr/share \
      -DDLT_PLUGIN_INSTALLATION_PATH=DLTViewer/usr/bin/plugins

    cmake --build "$BUILD_DIR" -j "$NPROC"
    cmake --install "$BUILD_DIR"
    ( cd "$INSTALL_DIR" && tar czf "$OUT/DLTViewer-linux-x86_64.tgz" . )
    echo "=== Linux x86_64 artifacts ==="
    ls -lh "$OUT/DLTViewer-linux-x86_64.tgz"
    ;;

  tests)
    BUILD_DIR="$SRC/build/tests"
    rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"
    cmake -S "$SRC" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDLT_PARSER=OFF
    cmake --build "$BUILD_DIR" -j "$NPROC"
    ctest --test-dir "$BUILD_DIR/qdlt" --output-on-failure
    ;;

  shell)
    exec /bin/bash
    ;;

  *)
    echo "unknown target: $TARGET (expected windows-x64 | linux-x64 | tests | shell)" >&2
    exit 2
    ;;
esac
