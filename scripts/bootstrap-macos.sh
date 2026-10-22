#!/usr/bin/env bash

# Bootstraps the host tools a macOS build of enzo needs, into a folder inside
# the repository, so a build does not depend on a package manager or on a
# globally installed toolchain.
#
# Homebrew is not used on purpose. Its downloads land in the user cache and its
# lock files are renamed atomically, both of which a sandboxed or CI runner
# often forbids. Everything here lands under toolchain/ and is removed with a
# single rm -rf.
#
# Usage: ./scripts/bootstrap-macos.sh [--with-qt]
#
# Writes toolchain/env.sh, which the build scripts source before configuring.
#
# Qt is only downloaded with --with-qt. The superbuild fetches it on its own,
# so the plain path does not download it twice.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TOOLCHAIN_DIR="${REPO_ROOT}/toolchain"

# The versions the linux CI workflow pins, so a macOS build resolves the same
# dependencies.
CMAKE_VERSION="3.31.6"
NINJA_VERSION="1.12.1"
AQTINSTALL_VERSION="3.3.0"
MOLTENVK_VERSION="1.4.2"

# The archive of the Vulkan loader for macOS on Apple silicon. It is the build
# of the loader of the Vulkan SDK, packaged for macOS.
VULKAN_LOADER_BOTTLE="4b99cecb4fe7115a6faf770cfa9e8f14426c24baa6e4566e2473fe5beedad5af"

# The Qt version cmake/versions.cmake pins.
QT_VERSION="6.10.3"

UNAME="$(uname -m)"
if [[ "${UNAME}" == "arm64" ]]; then
    ARCH="arm64"
else
    ARCH="x86_64"
fi

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

mkdir -p "${TOOLCHAIN_DIR}/downloads"

# Fetches a url into the downloads folder unless it is already there.
fetch() {
    local url="$1" out="$2"
    if [[ -s "${out}" ]]; then
        log "already downloaded: $(basename "${out}")"
        return 0
    fi
    log "downloading $(basename "${out}")"
    curl -fsSL --retry 3 --retry-delay 2 -o "${out}.part" "${url}"
    mv "${out}.part" "${out}"
}

# ---------------------------------------------------------------- cmake ----

install_cmake() {
    if [[ -x "${TOOLCHAIN_DIR}/cmake/CMake.app/Contents/bin/cmake" ]]; then
        log "cmake already installed"
        return 0
    fi
    local tarball="${TOOLCHAIN_DIR}/downloads/cmake-${CMAKE_VERSION}-macos-universal.tar.gz"
    fetch \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-macos-universal.tar.gz" \
        "${tarball}"
    log "unpacking cmake"
    mkdir -p "${TOOLCHAIN_DIR}/cmake"
    # The macOS archive holds a bundle, so the tools sit inside the app.
    #
    # It is unpacked with the leading component dropped rather than moved
    # afterwards, because a sandboxed macOS host refuses to rename a folder.
    #
    # Only the versioned root folder is dropped. The archive holds the bundle
    # itself below it, so dropping two would land the tools in Contents/
    # instead of in CMake.app/Contents/.
    tar -xzf "${tarball}" -C "${TOOLCHAIN_DIR}/cmake" --strip-components=1
    [[ -x "${TOOLCHAIN_DIR}/cmake/CMake.app/Contents/bin/cmake" ]] \
        || die "cmake did not unpack to the expected bundle layout"
}

# ---------------------------------------------------------------- ninja ----

install_ninja() {
    if [[ -x "${TOOLCHAIN_DIR}/ninja/ninja" ]]; then
        log "ninja already installed"
        return 0
    fi
    local zip="${TOOLCHAIN_DIR}/downloads/ninja-mac.zip"
    fetch \
        "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-mac.zip" \
        "${zip}"
    log "unpacking ninja"
    mkdir -p "${TOOLCHAIN_DIR}/ninja"
    # The archive holds a single binary at its root.
    ( cd "${TOOLCHAIN_DIR}/ninja" && unzip -o "${zip}" >/dev/null )
    chmod +x "${TOOLCHAIN_DIR}/ninja/ninja"
}

# -------------------------------------------------------------- aqtinstall ----

install_aqt() {
    local venv="${TOOLCHAIN_DIR}/aqt-venv"
    if [[ -x "${venv}/bin/python" ]]; then
        log "aqtinstall venv already present"
    else
        log "creating the aqtinstall venv"
        python3 -m venv "${venv}"
    fi
    "${venv}/bin/python" -m pip install --quiet --disable-pip-version-check --upgrade pip
    "${venv}/bin/python" -m pip install --quiet --disable-pip-version-check \
        "aqtinstall==${AQTINSTALL_VERSION}"
    log "aqtinstall $("${venv}/bin/python" -m pip show aqtinstall | awk '/^Version:/{print $2}') installed"
}

# ------------------------------------------------------------- moltenvk ----

# Apple silicon and every recent mac expose no native Vulkan driver, so a
# Vulkan application on mac goes through MoltenVK, which translates Vulkan onto
# Metal. The loader finds it through an ICD manifest that points at the dylib.
install_moltenvk() {
    local dir="${TOOLCHAIN_DIR}/vulkan/MoltenVK/MoltenVK/dynamic/dylib/macOS"
    if [[ -f "${dir}/MoltenVK_icd.json" ]]; then
        log "MoltenVK already installed"
        return 0
    fi
    local tarball="${TOOLCHAIN_DIR}/downloads/MoltenVK-macos.tar"
    fetch \
        "https://github.com/KhronosGroup/MoltenVK/releases/download/v${MOLTENVK_VERSION}/MoltenVK-macos.tar" \
        "${tarball}"
    log "unpacking MoltenVK"
    mkdir -p "${TOOLCHAIN_DIR}/vulkan"
    tar -xf "${tarball}" -C "${TOOLCHAIN_DIR}/vulkan"
    [[ -f "${dir}/MoltenVK_icd.json" ]] || die "MoltenVK archive did not contain ${dir}"
}

# --------------------------------------------------------- vulkan loader ----

# The Diligent engine asks the Vulkan loader for the portability enumeration
# extension on macOS, which the driver of MoltenVK does not offer. The loader
# itself has the extension, so the application needs it beside MoltenVK.
#
# The loader is fetched as a prebuilt archive, the way every other tool here
# is, so that a build does not need a package manager for it.
install_vulkan_loader() {
    local dir="${TOOLCHAIN_DIR}/vulkan/loader"
    if [[ -f "${dir}/lib/libvulkan.1.dylib" ]]; then
        log "Vulkan loader already installed"
        return 0
    fi

    local venv="${TOOLCHAIN_DIR}/aqt-venv"
    [[ -x "${venv}/bin/python" ]] || install_aqt

    # The archive lives in a registry, which hands out a token for reading it.
    local token
    token="$(curl -fsSL "https://ghcr.io/token?scope=repository:homebrew/core/vulkan-loader:pull&service=ghcr.io" \
        | "${venv}/bin/python" -c 'import json,sys; print(json.load(sys.stdin)["token"])')"
    [[ -n "${token}" ]] || die "the registry did not hand out a token for the Vulkan loader"

    local archive="${TOOLCHAIN_DIR}/downloads/vulkan-loader.tar.gz"
    if [[ ! -s "${archive}" ]]; then
        log "downloading the Vulkan loader"
        curl -fsSL --retry 3 --retry-delay 2 \
            -H "Authorization: Bearer ${token}" \
            -o "${archive}" \
            "https://ghcr.io/v2/homebrew/core/vulkan-loader/blobs/sha256:${VULKAN_LOADER_BOTTLE}"
    fi

    log "unpacking the Vulkan loader"
    local stage="${TOOLCHAIN_DIR}/downloads/loader-stage"
    mkdir -p "${stage}"
    tar -xzf "${archive}" -C "${stage}"
    mkdir -p "${dir}"
    cp -R "${stage}"/vulkan-loader/*/lib "${dir}/"
    [[ -f "${dir}/lib/libvulkan.1.dylib" ]] || die "the Vulkan loader archive did not contain libvulkan.1.dylib"
}

# ------------------------------------------------------------------- qt ----

# Downloads the Qt build the project pins, so the superbuild finds it without
# rebuilding Qt from source. The superbuild in cmake/superbuild/deps/qt.cmake
# does the same thing, and this only exists so a plain build can use it.
install_qt() {
    local root="${TOOLCHAIN_DIR}/qt"
    if [[ -d "${root}/${QT_VERSION}" ]]; then
        log "Qt already installed"
        return 0
    fi
    local venv="${TOOLCHAIN_DIR}/aqt-venv"
    [[ -x "${venv}/bin/python" ]] || install_aqt

    # aqtinstall names the macOS desktop build clang_64 and unpacks it into a
    # folder called macos. That package carries an arm64 slice, so it serves
    # an Apple silicon host without a second download.
    local archs=("clang_64" "macos")
    for arch in "${archs[@]}"; do
        log "downloading Qt ${QT_VERSION} (${arch})"
        # The archives are kept, because a host that refuses to delete a file
        # would otherwise fail the download over the cleanup.
        if "${venv}/bin/python" -m aqt install-qt mac desktop "${QT_VERSION}" "${arch}" \
            --modules qtshadertools --outputdir "${root}" --keep ; then
            log "Qt ${QT_VERSION} installed as ${arch}"
            return 0
        fi
        warn "aqtinstall could not fetch the ${arch} build"
    done
    die "no Qt ${QT_VERSION} macOS build could be downloaded"
}

# ------------------------------------------------------------ bison, flex ----

# daslang regenerates its parsers from a bison grammar during the build. The
# committed parsers were produced by bison 3.8.2, and macOS ships bison 2.3,
# which generates an incompatible parser. daslang's own build looks for a
# Homebrew keg, because it says Xcode "does not provide new enough versions".
#
# The binaries come from conda-forge, which publishes the same versions for
# macOS, so a build does not need a package manager to have a new enough bison.
install_bison_flex() {
    local dir="${TOOLCHAIN_DIR}/bison"
    if [[ -x "${dir}/bin/bison" && -x "${dir}/bin/flex" && -x "${dir}/bin/m4" ]]; then
        log "bison, flex and m4 already installed"
        return 0
    fi

    local conda_arch
    if [[ "${ARCH}" == "arm64" ]]; then
        conda_arch="osx-arm64"
    else
        conda_arch="osx-64"
    fi

    # The .conda format is a zip holding zstd compressed tarballs.
    local pkg="${TOOLCHAIN_DIR}/downloads/bison-3.8.2-${conda_arch}.conda"
    fetch \
        "https://conda.anaconda.org/conda-forge/${conda_arch}/bison-3.8.2-h151c81c_0.conda" \
        "${pkg}"

    # This one is published as a plain bzip2 tarball.
    local flex_pkg="${TOOLCHAIN_DIR}/downloads/flex-2.6.4-${conda_arch}.tar.bz2"
    fetch \
        "https://conda.anaconda.org/conda-forge/${conda_arch}/flex-2.6.4-h1474e2a_1004.tar.bz2" \
        "${flex_pkg}"

    # Bison calls m4 to expand its macros. The m4 of the host writes a cache
    # file next to its other temporary files, which a sandboxed or otherwise
    # restricted host refuses, so the toolchain carries its own m4.
    local m4_build
    case "${conda_arch}" in
        osx-arm64) m4_build="m4-1.4.21-h84a0fba_0.conda" ;;
        *)         m4_build="m4-1.4.21-hf3981d6_0.conda" ;;
    esac
    local m4_pkg="${TOOLCHAIN_DIR}/downloads/${m4_build}"
    fetch \
        "https://conda.anaconda.org/conda-forge/${conda_arch}/${m4_build}" \
        "${m4_pkg}"

    local venv="${TOOLCHAIN_DIR}/aqt-venv"
    [[ -x "${venv}/bin/python" ]] || install_aqt
    "${venv}/bin/python" -m pip install --quiet --disable-pip-version-check zstandard

    local stage="${TOOLCHAIN_DIR}/downloads/conda-stage"
    mkdir -p "${stage}"
    log "unpacking bison"
    "${venv}/bin/python" "${SCRIPT_DIR}/extract-conda.py" "${pkg}" "${stage}"
    log "unpacking flex"
    "${venv}/bin/python" "${SCRIPT_DIR}/extract-conda.py" "${flex_pkg}" "${stage}"
    log "unpacking m4"
    "${venv}/bin/python" "${SCRIPT_DIR}/extract-conda.py" "${m4_pkg}" "${stage}"

    mkdir -p "${dir}"
    # The packages share a bin and lib folder, so they are merged into one
    # prefix rather than kept apart.
    #
    # A file that is already in the prefix is kept. A bison that was built
    # from the sources into this prefix comes with its own data files, and
    # replacing either of them with the ones of the package would mix two
    # different builds.
    for folder in bin lib include; do
        [[ -d "${stage}/${folder}" ]] || continue
        mkdir -p "${dir}/${folder}"
        local item
        while IFS= read -r item; do
            [[ -e "${dir}/${folder}/$(basename "${item}")" ]] && continue
            cp -R "${item}" "${dir}/${folder}/"
        done < <(find "${stage}/${folder}" -mindepth 1 -maxdepth 1)
    done
    if [[ -d "${stage}/share" && ! -e "${dir}/share/bison" ]]; then
        mkdir -p "${dir}/share"
        cp -R "${stage}/share/." "${dir}/share/"
    fi
    # The packages carry their own libiconv, which the binaries reach through a
    # relative rpath, so the prefix has to stay together.
    chmod +x "${dir}/bin/bison" "${dir}/bin/flex" "${dir}/bin/m4"
    [[ -x "${dir}/bin/bison" ]] || die "bison did not unpack to ${dir}/bin"
    log "bison $("${dir}/bin/bison" --version 2>/dev/null | head -1), flex $("${dir}/bin/flex" --version 2>/dev/null | head -1), m4 $("${dir}/bin/m4" --version 2>/dev/null | head -1)"
}

# The conda build of bison crashes when the locale is the C locale, which is
# the locale cmake asks a tool for its version in. The shim runs the bison of
# the toolchain without that locale, so the build finds a working one.
#
# The same build has the folder of its data files baked in, because the
# package was compiled on the machine that built it. The shim points bison at
# the data files of the toolchain, which is where the package unpacked them.
install_bison_shim() {
    local shim_dir="${TOOLCHAIN_DIR}/bison/shim"
    mkdir -p "${shim_dir}"
    printf '%s\n' \
        '#!/bin/sh' \
        '# Runs the bison of the toolchain without the C locale, which the' \
        '# conda build of bison does not survive. cmake asks a tool for its' \
        '# version in the C locale, so without this the build cannot even' \
        '# find bison. The data files of bison are pointed at as well, but' \
        '# only when they are in the toolchain, because the package has the' \
        '# folder of the machine that built it baked in. A bison that was' \
        '# built from the sources into the same prefix brings its own data' \
        '# files, and pointing it at the ones of the package would mix two' \
        '# builds. The m4 of the toolchain is named too, so the macros are' \
        '# expanded by a build that can write its cache.' \
        'data_dir="'"${TOOLCHAIN_DIR}"'/bison/share/bison"' \
        'if [ -d "${data_dir}" ]; then' \
        '    BISON_PKGDATADIR="${data_dir}"' \
        '    export BISON_PKGDATADIR' \
        'fi' \
        'exec env -u LC_ALL -u LC_CTYPE M4="'"${TOOLCHAIN_DIR}"'/bison/bin/m4" "'"${TOOLCHAIN_DIR}"'/bison/bin/bison" "$@"' \
        > "${shim_dir}/bison"
    chmod +x "${shim_dir}/bison"
}

# ------------------------------------------------------------------ env ----

write_env() {
    log "writing toolchain/env.sh"
    # The file is written with printf rather than a here document, because a
    # here document needs a temporary file that a sandboxed shell cannot
    # create.
    printf '%s\n' \
        '# Sourced by the build scripts. Exports the toolchain this repository bootstraps.' \
        '#' \
        '#   source toolchain/env.sh' \
        '#' \
        '# The MoltenVK manifest tells the Vulkan loader which driver to use, because' \
        '# macOS has no native Vulkan driver.' \
        'export ENZO_TOOLCHAIN_DIR="${ENZO_TOOLCHAIN_DIR:-'"${TOOLCHAIN_DIR}"'}"' \
        'export PATH="${ENZO_TOOLCHAIN_DIR}/cmake/CMake.app/Contents/bin:${ENZO_TOOLCHAIN_DIR}/ninja:${ENZO_TOOLCHAIN_DIR}/bison/shim:${ENZO_TOOLCHAIN_DIR}/bison/bin:${PATH}"' \
        'export BISON_EXECUTABLE="${ENZO_TOOLCHAIN_DIR}/bison/shim/bison"' \
        'export FLEX_EXECUTABLE="${ENZO_TOOLCHAIN_DIR}/bison/bin/flex"' \
        'export BISON_PKGDATADIR="${ENZO_TOOLCHAIN_DIR}/bison/share/bison"' \
        'export M4="${ENZO_TOOLCHAIN_DIR}/bison/bin/m4"' \
        'export CMAKE_PREFIX_PATH="${ENZO_TOOLCHAIN_DIR}/qt/'"${QT_VERSION}"'/macos:${CMAKE_PREFIX_PATH:-}"' \
        'export QT_DIR="${ENZO_TOOLCHAIN_DIR}/qt/'"${QT_VERSION}"'/macos"' \
        'export QT_VERSION="'"${QT_VERSION}"'"' \
        'export MOLTENVK_DIR="${ENZO_TOOLCHAIN_DIR}/vulkan/MoltenVK/MoltenVK/dynamic/dylib/macOS"' \
        'export VULKAN_LOADER_DIR="${ENZO_TOOLCHAIN_DIR}/vulkan/loader/lib"' \
        'export VK_ICD_FILENAMES="${MOLTENVK_DIR}/MoltenVK_icd.json"' \
        'export VK_DRIVER_FILES="${VK_ICD_FILENAMES}"' \
        'export DYLD_LIBRARY_PATH="${VULKAN_LOADER_DIR}:${MOLTENVK_DIR}:${DYLD_LIBRARY_PATH:-}"' \
        > "${TOOLCHAIN_DIR}/env.sh"
}

main() {
    local with_qt=0
    for arg in "$@"; do
        case "${arg}" in
            --with-qt) with_qt=1 ;;
            *) die "unknown argument: ${arg}" ;;
        esac
    done

    log "bootstrapping the macOS toolchain into ${TOOLCHAIN_DIR}"
    install_cmake
    install_ninja
    install_aqt
    install_moltenvk
    install_vulkan_loader
    install_bison_flex
    install_bison_shim
    if [[ "${with_qt}" -eq 1 ]]; then
        install_qt
    else
        log "skipping Qt. Pass --with-qt to download it here instead."
    fi
    write_env
    log "done. Source toolchain/env.sh before building."
}

main "$@"
