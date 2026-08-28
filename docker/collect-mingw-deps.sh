#!/usr/bin/env bash
# Copy the mingw-w64 / Qt6 runtime DLLs and Qt plugins that the cross-built
# Windows binaries need, so the staged directory runs standalone on Windows.
set -euo pipefail

STAGE="${1:?usage: collect-mingw-deps.sh <stage-dir>}"
SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw
DLLDIR="$SYSROOT/bin"
PLUGINDIR="$SYSROOT/lib/qt6/plugins"

copied_marker="$(mktemp -d)"
trap 'rm -rf "$copied_marker"' EXIT

# Recursively resolve DLL imports, copying anything that lives in the sysroot.
resolve() {
  local binary="$1"
  local dep
  # shellcheck disable=SC2013
  for dep in $(x86_64-w64-mingw32-objdump -p "$binary" 2>/dev/null \
                 | awk '/DLL Name:/ {print $3}'); do
    [ -e "$copied_marker/$dep" ] && continue
    if [ -f "$DLLDIR/$dep" ]; then
      : > "$copied_marker/$dep"
      cp -n "$DLLDIR/$dep" "$STAGE/" 2>/dev/null || true
      resolve "$DLLDIR/$dep"
    else
      # Not in the sysroot -> it is a Windows system DLL, skip it.
      : > "$copied_marker/$dep"
    fi
  done
}

shopt -s nullglob
for binary in "$STAGE"/*.exe "$STAGE"/plugins/*.dll; do
  resolve "$binary"
done

# Qt platform / style / image plugins are loaded at runtime, so objdump cannot
# see them; copy the ones a widgets app actually needs.
for sub in platforms styles imageformats tls; do
  if [ -d "$PLUGINDIR/$sub" ]; then
    mkdir -p "$STAGE/$sub"
    cp -n "$PLUGINDIR/$sub"/*.dll "$STAGE/$sub/" 2>/dev/null || true
    for plugin in "$STAGE/$sub"/*.dll; do resolve "$plugin"; done
  fi
done

# qt.conf keeps Qt from looking for plugins in the Fedora build prefix.
cat > "$STAGE/qt.conf" <<'CONF'
[Paths]
Prefix = .
Plugins = .
CONF

echo "collect-mingw-deps: staged $(find "$STAGE" -name '*.dll' | wc -l) DLLs"
