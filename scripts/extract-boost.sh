# Extracts the pinned Boost archive into the folder the superbuild expects.
#
# The superbuild extracts its archives with cmake -E tar, which stops at the
# first file it cannot write. The Boost archive carries editor and git
# metadata, and a sandbox that protects those refuses to write them inside a
# directory tree, so the extraction fails on boost-1.88.0/.gitmodules.
#
# This script does the same extraction, leaving those files out. Boost does
# not need them to build.
#
# Usage: ./scripts/extract-boost.sh <superbuild-dir>

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUPERBUILD_DIR="${1:?usage: extract-boost.sh <superbuild-dir>}"

# shellcheck source=/dev/null
source "${REPO_ROOT}/toolchain/env.sh"

ARCHIVE="${SUPERBUILD_DIR}/Download/boost/boost-1.88.0-cmake.tar.xz"
DEST="${SUPERBUILD_DIR}/Source/boost"

if [ ! -f "${ARCHIVE}" ]; then
    echo "missing archive: ${ARCHIVE}" >&2
    exit 1
fi

# The superbuild puts the extracted tree in place under Source/boost, and the
# archive holds a boost-1.88.0 folder that becomes that directory.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/boost-extract.XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

tar -xf "${ARCHIVE}" -C "${WORK}" \
    --exclude=".gitmodules" \
    --exclude=".vscode" \
    --exclude=".github" \
    --exclude=".idea" \
    --exclude=".clang-format"

if [ ! -d "${WORK}/boost-1.88.0" ]; then
    echo "the archive did not contain boost-1.88.0" >&2
    exit 1
fi

mkdir -p "$(dirname "${DEST}")"
rm -rf "${DEST}"
cp -R "${WORK}/boost-1.88.0" "${DEST}"

echo "Boost extracted into ${DEST}"
