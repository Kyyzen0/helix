# Downloads Qt as an official prebuilt. aqtinstall does the downloading from its
# own virtual environment, so the host only needs python.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The Qt modules that sit outside the base install.
#
# The Vulkan part of Qt is part of the base package on the platforms that
# publish it. macOS has none, so it is not asked for there.
set(ENZO_QT_MODULES qtshadertools)

set(ENZO_AQT_VENV ${CMAKE_BINARY_DIR}/aqt-venv)

# ENZO_QT_HOST is the repository aqtinstall reads, ENZO_QT_ARCHS are the
# architectures it offers for it and ENZO_QT_DIR is the folder it writes,
# which drops the host prefix.
#
# The list of architectures is tried in order, so a host whose primary
# architecture is not published for the pinned version still finds a build.
if(WIN32)
    set(ENZO_QT_HOST windows)
    set(ENZO_QT_ARCHS win64_msvc2022_64)
    set(ENZO_QT_DIR msvc2022_64)
    set(ENZO_AQT_PYTHON ${ENZO_AQT_VENV}/Scripts/python.exe)
elseif(APPLE)
    # aqtinstall names the macOS desktop build clang_64 and unpacks it into a
    # folder called macos. That package carries an arm64 slice, so it serves
    # an Apple silicon host without a second download.
    set(ENZO_QT_HOST mac)
    set(ENZO_QT_ARCHS clang_64 macos)
    set(ENZO_QT_DIR macos)
    set(ENZO_AQT_PYTHON ${ENZO_AQT_VENV}/bin/python)
else()
    set(ENZO_QT_HOST linux)
    set(ENZO_QT_ARCHS linux_gcc_64 gcc_64)
    set(ENZO_QT_DIR gcc_64)
    set(ENZO_AQT_PYTHON ${ENZO_AQT_VENV}/bin/python)
endif()

# The architectures travel as one argument, separated by commas, so neither a
# shell nor a cmake list splits them.
string(JOIN "," ENZO_QT_ARCH_LIST ${ENZO_QT_ARCHS})

# Unpacks into its own versioned folder rather than the shared dependency
# prefix, so ENZO_QT_PREFIX is a second place for everything else to search.
set(ENZO_QT_ROOT "${ENZO_DEPS_PREFIX}/qt")
set(ENZO_QT_PREFIX "${ENZO_QT_ROOT}/${ENZO_QT_VERSION}/${ENZO_QT_DIR}")

# A Qt that is already installed somewhere else is used as it is, rather than
# downloaded a second time. The bootstrap script puts one under the toolchain
# folder, and a developer machine often has one in /opt/homebrew.
#
# ENZO_QT_PREFIX_OVERRIDE names a directory that holds the Qt prefix itself,
# the one that carries the lib/cmake/Qt6 folder.
if(ENZO_QT_PREFIX_OVERRIDE AND EXISTS "${ENZO_QT_PREFIX_OVERRIDE}/lib/cmake/Qt6/Qt6Config.cmake")
    set(ENZO_QT_PREFIX "${ENZO_QT_PREFIX_OVERRIDE}")
    message(STATUS "Using the Qt installation at ${ENZO_QT_PREFIX}")
else()
    ExternalProject_Add(qt
        DOWNLOAD_COMMAND
            ${Python3_EXECUTABLE} -m venv ${ENZO_AQT_VENV}
        COMMAND
            ${ENZO_AQT_PYTHON} -m pip install --quiet --disable-pip-version-check
            aqtinstall==${ENZO_AQTINSTALL_VERSION}
        COMMAND
            ${CMAKE_COMMAND}
            -DENZO_AQT_PYTHON=${ENZO_AQT_PYTHON}
            -DENZO_AQT_RUNNER=${CMAKE_CURRENT_LIST_DIR}/aqt-run.py
            -DENZO_AQT_SETTINGS=${CMAKE_CURRENT_LIST_DIR}/aqt-settings.ini
            -DENZO_QT_HOST=${ENZO_QT_HOST}
            -DENZO_QT_VERSION=${ENZO_QT_VERSION}
        -DENZO_QT_DIR=${ENZO_QT_DIR}
            -DENZO_QT_ROOT=${ENZO_QT_ROOT}
            -DENZO_QT_ARCHS=${ENZO_QT_ARCH_LIST}
            -DENZO_QT_MODULES=${ENZO_QT_MODULES}
            -P ${CMAKE_CURRENT_LIST_DIR}/qt-install.cmake
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        USES_TERMINAL_DOWNLOAD ON
    )
    list(APPEND ENZO_DEPS qt)
endif()
