#!/bin/sh
# Builds the Swift proof that HelixCore can be imported, then runs it.
#
#   scripts/bootstrap-macos.sh && source toolchain/env.sh
#   cmake -S . -B build-core ... (see "Building the engine without Qt" in build-macos.md)
#   cmake --build build-core --target helixCore helixOps
#   apple/HelixCoreSmoke/build.sh [build folder, default build-core]
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
deps="${HELIX_DEPS_PREFIX:-$root/build-super/deps}"
build="${1:-$root/build-core}"
case "$build" in /*) ;; *) build="$PWD/$build" ;; esac

if [ ! -f "$build/libhelixCore.a" ]; then
    echo "build-core has no libhelixCore.a: run 'cmake --build $build --target helixCore helixOps' first" >&2
    exit 1
fi

out="$build/HelixCoreSmoke"
mkdir -p "$out"

# helixCore and helixGraphics are static libraries, so they sit before the engine they use.
# helixGraphics uses tbb itself, and the engine dylib finds its own dependencies (tbb, boost)
# through the rpath of the dependency prefix.
swiftc -O \
    -module-cache-path "$out/ModuleCache" \
    -cxx-interoperability-mode=default \
    -I "$root/src/HelixCore" \
    -Xcc -std=c++20 \
    "$here/main.swift" \
    -o "$out/HelixCoreSmoke" \
    -L "$build" -L "$deps/lib" -lhelixCore -lhelixGraphics -lhelixEngine -ltbb \
    -framework Metal -framework Foundation -framework QuartzCore \
    -Xlinker -rpath -Xlinker "$build" \
    -Xlinker -rpath -Xlinker "$deps/lib"

# The engine finds its nodes, the node library and daslib through these three folders. The
# build folder holds the staged daslib.
"$out/HelixCoreSmoke" "$root/nodes" "$build/nodes" "$build/helix.app/Contents"
