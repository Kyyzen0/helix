#!/bin/sh
# Rewrites the sample scenes of this folder with the engine in build-core. Needs the engine built
# (see "Building the engine without Qt" in build-macos.md), and the Metal toolchain only if the
# engine was.
#
#   apple/Helix/Samples/make-samples.sh [build folder, default build-core]
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
deps="${HELIX_DEPS_PREFIX:-$root/build-super/deps}"
build="${1:-$root/build-core}"
case "$build" in /*) ;; *) build="$PWD/$build" ;; esac

out="$build/make-samples"
mkdir -p "$out"
swiftc -O \
    -module-cache-path "$out/ModuleCache" \
    -cxx-interoperability-mode=default \
    -I "$root/src/HelixCore" \
    -Xcc -std=c++20 \
    "$here/make-samples.swift" \
    -o "$out/make-samples" \
    -L "$build" -L "$deps/lib" -lhelixCore -lhelixGraphics -lhelixEngine -ltbb \
    -framework Metal -framework Foundation -framework QuartzCore \
    -framework ImageIO -framework CoreGraphics -framework CoreFoundation \
    -Xlinker -rpath -Xlinker "$build" \
    -Xlinker -rpath -Xlinker "$deps/lib"

"$out/make-samples" "$root/nodes" "$build/nodes" "$build/helix.app/Contents" "$here"
