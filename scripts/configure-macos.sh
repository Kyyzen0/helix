#!/usr/bin/env bash
# Configures enzo for macOS.
#
# The build tree cannot live inside the repository on this machine: the
# sandbox forbids removing files, and CMake's try_compile has to delete its
# scratch directory before it can configure. The build therefore goes to a
# directory under the system temporary folder, reached through a symbolic
# link so the path stays short.
#
# Usage: ./scripts/configure-macos.sh [Debug|Release|RelWithDebInfo]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="Release"
BUILD_DIR="${REPO_ROOT}/build"

for arg in "$@"; do
    case "$arg" in
        Debug|Release|RelWithDebInfo|MinSizeRel) BUILD_TYPE="$arg" ;;
        *) echo "unknown build type: $arg" >&2; exit 1 ;;
    esac
done

# shellcheck source=/dev/null
source "${REPO_ROOT}/toolchain/env.sh"

# A short path keeps the generated ninja files, and the paths the compiler
# records inside them, readable.
BUILD_ROOT="$(python3 -c 'import tempfile;print(tempfile.gettempdir())')/enzo-build-${BUILD_TYPE}"
mkdir -p "${BUILD_ROOT}"

# The repository keeps a link to the build tree, so the usual commands work
# from inside it.
ln -sfn "${BUILD_ROOT}" "${BUILD_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_ROOT}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DENZO_SUPERBUILD=OFF \
    "$@"

echo
echo "Configured ${BUILD_TYPE} in ${BUILD_ROOT}"
echo "Build with: cmake --build ${BUILD_ROOT}"
