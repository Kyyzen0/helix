#!/usr/bin/env bash
# Compiles the files of the interface without producing an object file, so a
# change can be checked on a host that cannot build the whole tree.
#
# The flags are taken from the build tree that is already there, and the headers
# of the Qt that the toolchain holds, so the check uses the same compiler and
# the same headers as the real build.
#
# Usage: ./scripts/check-gui.sh <source file> [...]

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NINJA_FILE="${REPO_ROOT}/build/enzo-build/enzo-build/build.ninja"
QT_LIB="${REPO_ROOT}/toolchain/qt/6.10.3/macos/lib"

if [[ ! -f "${NINJA_FILE}" ]]; then
    echo "no build tree to take the flags from: ${NINJA_FILE}" >&2
    exit 1
fi

if [[ ! -d "${QT_LIB}" ]]; then
    echo "no Qt in the toolchain: ${QT_LIB}" >&2
    exit 1
fi

RULE="$(awk '/main\.cpp\.o:/{f=1} f&&/^$/{exit} f&&/^  (DEFINES|INCLUDES|FLAGS) = /{print}' "${NINJA_FILE}")"

DEFINES="$(printf '%s\n' "${RULE}" | awk '/^  DEFINES = /{$1="";$2="";print}' | sed 's/\\"/"/g')"
INCLUDES="$(printf '%s\n' "${RULE}" | awk '/^  INCLUDES = /{$1="";$2="";print}')"
FLAGS="$(printf '%s\n' "${RULE}" | awk '/^  FLAGS = /{$1="";$2="";print}')"

# Every framework of Qt carries its headers in a folder of its own.
QT_INCLUDES=()
for headers in "${QT_LIB}"/*.framework/Headers; do
    [[ -d "${headers}" ]] && QT_INCLUDES+=("-I${headers}")
done
# The modules that are not frameworks keep their headers in plain folders.
for folder in "${QT_LIB}"/include/*/; do
    [[ -d "${folder}" ]] && QT_INCLUDES+=("-I${folder}")
done

status=0
for source in "$@"; do
    full="${REPO_ROOT}/${source}"
    if [[ ! -f "${full}" ]]; then
        echo "MISSING  ${source}"
        status=1
        continue
    fi
    # shellcheck disable=SC2086
    if clang++ -fsyntax-only -std=gnu++20 ${DEFINES} ${INCLUDES} ${QT_INCLUDES[@]} ${FLAGS} "${full}" 2> /tmp/check-gui.log; then
        echo "OK       ${source}"
    else
        echo "FAIL     ${source}"
        grep -v "xcrun_db" /tmp/check-gui.log | head -25
        status=1
    fi
done

exit "${status}"
