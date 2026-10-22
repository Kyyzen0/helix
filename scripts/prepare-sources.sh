#!/usr/bin/env bash
# Prepares the dependency archives the superbuild downloads.
#
# The superbuild extracts its archives with cmake -E tar, which stops at the
# first file it cannot write. Archives published from a git repository carry
# editor and git metadata, and a sandbox that protects those refuses to write
# them inside a directory tree, so the extraction fails on the first such
# file.
#
# This script extracts each archive itself, leaving that metadata out, and
# puts the tree where the superbuild expects it. None of the metadata takes
# part in the build.
#
# Usage: ./scripts/prepare-sources.sh <superbuild-dir>

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUPERBUILD_DIR="${1:?usage: prepare-sources.sh <superbuild-dir>}"

# shellcheck source=/dev/null
source "${REPO_ROOT}/toolchain/env.sh"

DOWNLOAD_DIR="${SUPERBUILD_DIR}/Download"
SOURCE_DIR="${SUPERBUILD_DIR}/Source"

# The excluded names are the ones a sandbox protects.
EXCLUDES=(
    --exclude=".gitmodules"
    --exclude=".gitattributes"
    --exclude=".gitignore"
    --exclude=".github"
    --exclude=".vscode"
    --exclude=".idea"
    --exclude=".clang-format"
)

# Each dependency: the folder its archive sits in, and the folder its source
# is extracted into.
prepare() {
    local dep="$1"
    local archive
    archive="$(find "${DOWNLOAD_DIR}/${dep}" -maxdepth 1 -type f 2>/dev/null | head -1)"

    if [ -z "${archive}" ]; then
        echo "no archive for ${dep}, skipping"
        return 0
    fi

    local dest="${SOURCE_DIR}/${dep}"
    if [ -f "${dest}/CMakeLists.txt" ]; then
        echo "${dep} is already extracted"
        return 0
    fi

    local work
    work="$(mktemp -d "${TMPDIR:-/tmp}/prepare.XXXXXX")"

    case "${archive}" in
        *.tar.gz|*.tgz) tar -xzf "${archive}" -C "${work}" "${EXCLUDES[@]}" ;;
        *.tar.xz) tar -xJf "${archive}" -C "${work}" "${EXCLUDES[@]}" ;;
        *.tar.bz2) tar -xjf "${archive}" -C "${work}" "${EXCLUDES[@]}" ;;
        *.tar) tar -xf "${archive}" -C "${work}" "${EXCLUDES[@]}" ;;
        *) echo "unsupported archive type for ${dep}: ${archive}" >&2; rm -rf "${work}"; return 1 ;;
    esac

    # The archive holds one folder, which becomes the source folder.
    local root
    root="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -1)"
    if [ -z "${root}" ]; then
        echo "the ${dep} archive held no folder" >&2
        rm -rf "${work}"
        return 1
    fi

    rm -rf "${dest}"
    mkdir -p "${dest}"
    cp -R "${root}/." "${dest}/"

    rm -rf "${work}"
    echo "prepared ${dep} in ${dest}"
}

for dep in argparse boost eigen fastnoise2 glm manifold tbb; do
    prepare "${dep}"
done
