# Downloads the pinned Qt build with aqtinstall, trying each architecture in
# turn until one of them is published for the pinned version.
#
# This runs under cmake -P. Every input arrives as a -D define, because an
# external project passes its commands as one flat argument list.

if(NOT HELIX_AQT_PYTHON OR NOT HELIX_QT_HOST OR NOT HELIX_QT_VERSION
        OR NOT HELIX_QT_ROOT OR NOT HELIX_QT_ARCHS OR NOT HELIX_AQT_RUNNER)
    message(FATAL_ERROR
        "qt-install.cmake needs HELIX_AQT_PYTHON, HELIX_AQT_RUNNER, HELIX_QT_HOST, "
        "HELIX_QT_VERSION, HELIX_QT_ROOT and HELIX_QT_ARCHS")
endif()

# The architectures travel as one argument, separated by commas.
string(REPLACE "," ";" HELIX_QT_ARCH_LIST "${HELIX_QT_ARCHS}")

# A Qt that is already in place is used as it is. A build that is run again
# after a failure should not download the whole thing a second time.
if(EXISTS "${HELIX_QT_ROOT}/${HELIX_QT_VERSION}/${HELIX_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    message(STATUS "Qt ${HELIX_QT_VERSION} is already installed")
    return()
endif()

# aqtinstall takes its modules as one comma separated argument.
string(JOIN "," HELIX_QT_MODULE_LIST ${HELIX_QT_MODULES})

# aqtinstall hands work between its processes over a socket, and the path of
# that socket is built from the temporary folder. A temporary folder with a
# long path makes the socket path too long for the system to accept, so the
# inherited one is used only while it stays short enough.
set(HELIX_QT_TMPDIR "$ENV{TMPDIR}")
string(LENGTH "${HELIX_QT_TMPDIR}" HELIX_QT_TMPDIR_LENGTH)
if(NOT HELIX_QT_TMPDIR OR HELIX_QT_TMPDIR_LENGTH GREATER 70)
    set(HELIX_QT_TMPDIR "/tmp")
endif()

foreach(arch IN LISTS HELIX_QT_ARCH_LIST)
    message(STATUS "Fetching Qt ${HELIX_QT_VERSION} for ${HELIX_QT_HOST}/${arch}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "AQT_TEMP_DIR=${HELIX_QT_TMPDIR}"
                "${HELIX_AQT_PYTHON}" "${HELIX_AQT_RUNNER}"
                --config "${HELIX_AQT_SETTINGS}"
                install-qt
                "${HELIX_QT_HOST}" desktop "${HELIX_QT_VERSION}" "${arch}"
                --modules "${HELIX_QT_MODULE_LIST}"
                --outputdir "${HELIX_QT_ROOT}"
                # The archives are kept rather than removed after the install.
                # A host that refuses to delete a file would otherwise fail the
                # whole download over the cleanup, and the archives cost only
                # the room they already take in the build tree.
                --keep
        RESULT_VARIABLE result)
    # aqtinstall reports a failure when it cannot remove the folder it worked
    # in, which a host that refuses to delete a file always fails to do. The
    # install itself is what matters, so the prefix decides.
    if(result EQUAL 0 OR EXISTS "${HELIX_QT_ROOT}/${HELIX_QT_VERSION}/${HELIX_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
        message(STATUS "Qt ${HELIX_QT_VERSION} installed for ${arch}")
        return()
    endif()
    message(STATUS "aqtinstall could not fetch the ${arch} build")
endforeach()

message(FATAL_ERROR "no Qt ${HELIX_QT_VERSION} build could be downloaded for ${HELIX_QT_HOST}")
