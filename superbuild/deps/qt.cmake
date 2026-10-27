# Downloads Qt as an official prebuilt. aqtinstall does the downloading from its
# own virtual environment, so the host only needs python.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The Qt modules that sit outside the base install.
#
# qtshadertools compiles the Qt Quick shaders of the interface at build time.
set(HELIX_QT_MODULES qtshadertools)

set(HELIX_AQT_VENV ${CMAKE_BINARY_DIR}/aqt-venv)

# HELIX_QT_HOST is the repository aqtinstall reads, HELIX_QT_ARCHS are the
# architectures it offers for it and HELIX_QT_DIR is the folder it writes,
# which drops the host prefix.
#
# The list of architectures is tried in order, so a host whose primary
# architecture is not published for the pinned version still finds a build.
#
# aqtinstall names the macOS desktop build clang_64 and unpacks it into a folder
# called macos. The Qt Company publishes that package as a universal binary
# (arm64 and x86_64) and no arm64-only one, so the download still carries an
# x86_64 slice. Helix itself, and every dependency, is built for arm64 only.
set(HELIX_QT_HOST mac)
set(HELIX_QT_ARCHS clang_64 macos)
set(HELIX_QT_DIR macos)
set(HELIX_AQT_PYTHON ${HELIX_AQT_VENV}/bin/python)

# The architectures travel as one argument, separated by commas, so neither a
# shell nor a cmake list splits them.
string(JOIN "," HELIX_QT_ARCH_LIST ${HELIX_QT_ARCHS})

# Unpacks into its own versioned folder rather than the shared dependency
# prefix, so HELIX_QT_PREFIX is a second place for everything else to search.
set(HELIX_QT_ROOT "${HELIX_DEPS_PREFIX}/qt")
set(HELIX_QT_PREFIX "${HELIX_QT_ROOT}/${HELIX_QT_VERSION}/${HELIX_QT_DIR}")

# A Qt that is already installed somewhere else is used as it is, rather than
# downloaded a second time. The bootstrap script puts one under the toolchain
# folder, and a developer machine often has one in /opt/homebrew.
#
# HELIX_QT_PREFIX_OVERRIDE names a directory that holds the Qt prefix itself,
# the one that carries the lib/cmake/Qt6 folder.
if(HELIX_QT_PREFIX_OVERRIDE AND EXISTS "${HELIX_QT_PREFIX_OVERRIDE}/lib/cmake/Qt6/Qt6Config.cmake")
    set(HELIX_QT_PREFIX "${HELIX_QT_PREFIX_OVERRIDE}")
    message(STATUS "Using the Qt installation at ${HELIX_QT_PREFIX}")
else()
    ExternalProject_Add(qt
        DOWNLOAD_COMMAND
            ${Python3_EXECUTABLE} -m venv ${HELIX_AQT_VENV}
        COMMAND
            ${HELIX_AQT_PYTHON} -m pip install --quiet --disable-pip-version-check
            aqtinstall==${HELIX_AQTINSTALL_VERSION}
        COMMAND
            ${CMAKE_COMMAND}
            -DHELIX_AQT_PYTHON=${HELIX_AQT_PYTHON}
            -DHELIX_AQT_RUNNER=${CMAKE_CURRENT_LIST_DIR}/aqt-run.py
            -DHELIX_AQT_SETTINGS=${CMAKE_CURRENT_LIST_DIR}/aqt-settings.ini
            -DHELIX_QT_HOST=${HELIX_QT_HOST}
            -DHELIX_QT_VERSION=${HELIX_QT_VERSION}
        -DHELIX_QT_DIR=${HELIX_QT_DIR}
            -DHELIX_QT_ROOT=${HELIX_QT_ROOT}
            -DHELIX_QT_ARCHS=${HELIX_QT_ARCH_LIST}
            -DHELIX_QT_MODULES=${HELIX_QT_MODULES}
            -P ${CMAKE_CURRENT_LIST_DIR}/qt-install.cmake
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        USES_TERMINAL_DOWNLOAD ON
    )
    list(APPEND HELIX_DEPS qt)
endif()
