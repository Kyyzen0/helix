#!/usr/bin/env bash
# Compiles a source file of the project without producing an object file, so a
# change can be checked on a host that cannot build the whole tree.
#
# The flags are taken from the build tree that is already there, so the check
# uses the same compiler, defines, includes and standard as the real build.
#
# Usage: ./scripts/check-compile.sh <source file> [...]

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NINJA_FILE="${REPO_ROOT}/build/enzo-build/enzo-build/build.ninja"

if [[ ! -f "${NINJA_FILE}" ]]; then
    echo "no build tree to take the flags from: ${NINJA_FILE}" >&2
    exit 1
fi

# The rule of a file that is always part of the engine, so the flags are the
# ones the engine itself is built with. The three lines do not come in a fixed
# order, so each one is picked out on its own.
#
# The reference file can be another one, for a file that belongs to another
# target, such as a node.
REFERENCE="${CHECK_COMPILE_REFERENCE:-NetworkManager.cpp}"
RULE="$(awk -v ref="${REFERENCE}" '$0 ~ ref "\\.o:" {f=1} f&&/^$/{exit} f&&/^  (DEFINES|INCLUDES|FLAGS) = /{print}' "${NINJA_FILE}")"

# The defines hold escaped quotes, which the shell would pass on unchanged.
DEFINES="$(printf '%s\n' "${RULE}" | awk '/^  DEFINES = /{$1="";$2="";print}' | sed 's/\\"/"/g')"
INCLUDES="$(printf '%s\n' "${RULE}" | awk '/^  INCLUDES = /{$1="";$2="";print}')"
FLAGS="$(printf '%s\n' "${RULE}" | awk '/^  FLAGS = /{$1="";$2="";print}')"

status=0
for source in "$@"; do
    full="${REPO_ROOT}/${source}"
    if [[ ! -f "${full}" ]]; then
        echo "MISSING  ${source}"
        status=1
        continue
    fi
    # shellcheck disable=SC2086
    if clang++ -fsyntax-only ${DEFINES} ${INCLUDES} ${FLAGS} "${full}" 2> /tmp/check-compile.log; then
        echo "OK       ${source}"
    else
        echo "FAIL     ${source}"
        head -30 /tmp/check-compile.log
        status=1
    fi
done

exit "${status}"
