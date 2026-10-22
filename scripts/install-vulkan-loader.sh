#!/usr/bin/env bash
# Places the Vulkan loader where the loader lookup of DiligentCore finds it.
#
# DiligentCore loads Vulkan through volk, which tries a series of names with
# dlopen: libvulkan.dylib, then libvulkan.1.dylib, then /usr/local/lib/
# libvulkan.dylib, then libMoltenVK.dylib. A bare name is not searched in
# DYLD_LIBRARY_PATH, because the dynamic linker ignores that variable for
# signed processes. The loader therefore has to sit next to the application,
# or in /usr/local/lib.
#
# This script copies it next to the application bundle and, when that is not
# enough, into /usr/local/lib.
#
# Usage: ./scripts/install-vulkan-loader.sh <app-bundle>

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${1:?usage: install-vulkan-loader.sh <app-bundle>}"

# shellcheck source=/dev/null
source "${REPO_ROOT}/toolchain/env.sh"

LOADER="${VULKAN_LOADER_DIR}/libvulkan.1.4.363.dylib"

if [ ! -f "${LOADER}" ]; then
    echo "missing loader: ${LOADER}" >&2
    exit 1
fi

# In the frameworks folder of the bundle, which the loader path of the
# executable reaches. A plain dlopen of a bare name looks there.
FRAMEWORKS_DIR="${APP}/Contents/Frameworks"
if [ ! -d "${FRAMEWORKS_DIR}" ]; then
    echo "no frameworks folder in ${APP}" >&2
    exit 1
fi
for name in libvulkan.1.4.363.dylib libvulkan.1.dylib libvulkan.dylib; do
    cp -f "${LOADER}" "${FRAMEWORKS_DIR}/${name}"
done
echo "installed the loader in ${FRAMEWORKS_DIR}"

# Next to the executable as well, which is where the engine looks for it when
# it opens the loader itself rather than through the loader path.
MACOS_DIR="${APP}/Contents/MacOS"
if [ -d "${MACOS_DIR}" ]; then
    for name in libvulkan.1.4.363.dylib libvulkan.1.dylib libvulkan.dylib; do
        cp -f "${LOADER}" "${MACOS_DIR}/${name}"
    done
    echo "installed the loader in ${MACOS_DIR}"
fi

# The lookup also tries this absolute path, which is where a developer
# machine keeps a system wide Vulkan.
if [ -d /usr/local/lib ] && [ -w /usr/local/lib ]; then
    cp -f "${LOADER}" /usr/local/lib/libvulkan.1.dylib
    cp -f "${LOADER}" /usr/local/lib/libvulkan.dylib
    echo "installed the loader in /usr/local/lib"
else
    echo "/usr/local/lib is not writable, the loader is only in the bundle"
fi

# The bundle was signed when it was installed, and the loader that was just
# copied into it is not part of that signature. The bundle is signed again, so
# that it stays whole.
cmake -Dbundle="${APP}" -P "${REPO_ROOT}/cmake/SignBundle.cmake"
