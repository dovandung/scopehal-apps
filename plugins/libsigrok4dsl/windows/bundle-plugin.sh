#!/bin/bash
# Adds the libsigrok4DSL plugin to a Windows dist folder (run from the MSYS2 shell by CMake).
#
#   bundle-plugin.sh <dist dir> <plugin dll> <mingw bin dir> <build dir> <firmware dir>
#
# A DLL loaded from <exe dir>\plugins resolves its own dependencies from the exe directory,
# not from plugins\, so glib/libusb/libuvc/... must sit next to ngscopeclient.exe.
# mingw-bundledlls copies dependencies into the directory of the file it is given, so it runs
# on a copy of the plugin placed next to the exe, which is then moved into plugins\.
set -euo pipefail

dist="$1"
dll="$2"
mingwbin="$3"
build="$4"
res="$5"
name="$(basename "$dll")"

mkdir -p "$dist/plugins"
cp "$dll" "$dist/$name"

export MINGW_BUNDLEDLLS_SEARCH_PATH="$mingwbin;$build/lib/log;$build/lib/scopehal;$build/lib/xptools"
mingw-bundledlls "$dist/$name" --copy

mv -f "$dist/$name" "$dist/plugins/$name"

rm -rf "$dist/res"
cp -R "$res" "$dist/res"

echo "libsigrok4DSL plugin added: $dist/plugins/$name, firmware in $dist/res"
