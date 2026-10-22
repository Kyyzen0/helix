#!/usr/bin/env bash
# Removes the files that a sandbox refuses to write, from an archive that is
# about to be extracted.
#
# The superbuild extracts its dependency archives with cmake -E tar, which
# stops at the first file it cannot write. Archives published from a git
# repository carry editor and git metadata, and a sandbox that protects those
# refuses to write them inside a directory tree, so the extraction fails.
#
# The metadata takes no part in the build, so the archive is rewritten without
# it. The rewritten archive no longer matches the checksum the project pins,
# so this runs only for an archive that has already been downloaded and
# verified.
#
# Usage: ./scripts/sanitize-archive.sh <archive>

set -euo pipefail

ARCHIVE="${1:?usage: sanitize-archive.sh <archive>}"

if [ ! -f "${ARCHIVE}" ]; then
    echo "missing archive: ${ARCHIVE}" >&2
    exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/sanitize.XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

# The excluded names are the ones a sandbox protects. They are left out while
# the archive is unpacked, because the unpack itself is what fails on them.
EXCLUDES=(
    --exclude=".gitmodules"
    --exclude=".gitattributes"
    --exclude=".gitignore"
    --exclude=".github"
    --exclude=".vscode"
    --exclude=".idea"
    --exclude=".clang-format"
)

case "${ARCHIVE}" in
    *.tar.gz|*.tgz) tar -xzf "${ARCHIVE}" -C "${WORK}" "${EXCLUDES[@]}" ;;
    *.tar.xz) tar -xJf "${ARCHIVE}" -C "${WORK}" "${EXCLUDES[@]}" ;;
    *.tar.bz2) tar -xjf "${ARCHIVE}" -C "${WORK}" "${EXCLUDES[@]}" ;;
    *.tar) tar -xf "${ARCHIVE}" -C "${WORK}" "${EXCLUDES[@]}" ;;
    *.zip) unzip -q "${ARCHIVE}" -d "${WORK}" -x '*/.gitmodules' '*/.vscode/*' '*/.github/*' ;;
    *)
        echo "unsupported archive type: ${ARCHIVE}" >&2
        exit 1
        ;;
esac

# Repack what is left, keeping the layout the archive had.
REPACKED="${WORK}-repacked"
mkdir -p "${REPACKED}"
cp -R "${WORK}/." "${REPACKED}/"

case "${ARCHIVE}" in
    *.tar.gz|*.tgz) tar -czf "${ARCHIVE}.new" -C "${REPACKED}" . ;;
    *.tar.xz) tar -cJf "${ARCHIVE}.new" -C "${REPACKED}" . ;;
    *.tar.bz2) tar -cjf "${ARCHIVE}.new" -C "${REPACKED}" . ;;
    *.tar) tar -cf "${ARCHIVE}.new" -C "${REPACKED}" . ;;
    *.zip) ( cd "${REPACKED}" && zip -qr "${ARCHIVE}.new" . ) ;;
esac

# The archive is replaced by writing over it, because a rename onto an
# existing file is not always allowed.
cat "${ARCHIVE}.new" > "${ARCHIVE}"
rm -f "${ARCHIVE}.new" 2>/dev/null || true

echo "Sanitized ${ARCHIVE}"
