#!/bin/sh
# Xcode build phase: puts what the engine needs at run time inside the app bundle, so the app
# runs without the build folders. The layout follows the Qt application's bundle: the libraries
# in Frameworks, the nodes, daslib and the shaders in Resources.
#
#   Helix.app/Contents/Frameworks/   libhelixEngine, libhelixOps, tbb, boost
#   Helix.app/Contents/Resources/    nodes/, daslib/, helix.metallib
set -eu

root="$(cd "$SRCROOT/../.." && pwd)"
build="${HELIX_BUILD_CORE:-$root/build-core}"

# The dependency prefix the engine was configured with.
deps="${HELIX_DEPS_PREFIX:-}"
if [ -z "$deps" ] && [ -f "$build/CMakeCache.txt" ]; then
    deps="$(sed -n 's/^HELIX_DEPS_PREFIX:[A-Z]*=//p' "$build/CMakeCache.txt")"
fi
[ -n "$deps" ] || deps="$root/build-super/deps"

contents="$TARGET_BUILD_DIR/$CONTENTS_FOLDER_PATH"
frameworks="$contents/Frameworks"
resources="$contents/Resources"
mkdir -p "$frameworks" "$resources"

copy() {
    if [ ! -f "$1" ]; then
        echo "error: $1 is missing. Build the engine first (see build-macos.md)." >&2
        exit 1
    fi
    cp -f "$1" "$2"
}

copy "$build/libhelixEngine.dylib" "$frameworks/"
copy "$build/nodes/libhelixOps.dylib" "$frameworks/"
copy "$deps/lib/libtbb.12.dylib" "$frameworks/"
copy "$deps/lib/libboost_filesystem.1.88.0.dylib" "$frameworks/"

# Every library finds its neighbours next to itself, and nothing in the build folders.
for library in "$frameworks"/*.dylib; do
    otool -l "$library" | awk '/cmd LC_RPATH/ {found = 1} found && /^ *path / {print $2; found = 0}' |
        while read -r path; do
            install_name_tool -delete_rpath "$path" "$library" 2>/dev/null
        done
    install_name_tool -add_rpath "@loader_path" "$library" 2>/dev/null
    # Editing a library breaks its signature, and the kernel refuses an unsigned one.
    codesign --force --sign - "$library" 2>/dev/null
done

# The node folders, without the sources of the node library.
rsync -a --delete \
    --exclude '*.cpp' --exclude '*.h' --exclude 'CMakeLists.txt' --exclude '.DS_Store' \
    "$root/nodes/" "$resources/nodes/"

# The daslang standard library the scripting nodes load, and the compiled viewport shaders.
rsync -a --delete "$build/helix.app/Contents/daslib/" "$resources/daslib/"
copy "$build/helix.app/Contents/Resources/helix.metallib" "$resources/"
