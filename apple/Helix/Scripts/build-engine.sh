#!/bin/sh
# Xcode build phase: builds the C++ engine and its nodes with CMake, so the app links the
# current ones. CMake does nothing when nothing changed.
#
# The engine is configured once, by hand, into build-core (see "Building the engine without Qt"
# in build-macos.md). Set HELIX_BUILD_CORE to use another build folder.
set -eu

root="$(cd "$SRCROOT/../.." && pwd)"
build="${HELIX_BUILD_CORE:-$root/build-core}"

if [ ! -f "$build/build.ninja" ]; then
    echo "error: the engine is not configured: $build does not exist. Configure it first, as described under \"Building the engine without Qt\" in build-macos.md." >&2
    exit 1
fi

# The toolchain the repository bootstraps holds the cmake and the ninja the build folder was
# configured with.
if [ -f "$root/toolchain/env.sh" ]; then
    . "$root/toolchain/env.sh"
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake was not found. Run scripts/bootstrap-macos.sh, as described in build-macos.md." >&2
    exit 1
fi

cmake --build "$build" --target helixCore helixOps
