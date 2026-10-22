# Downloads the pinned Qt build with aqtinstall, trying each architecture in
# turn until one of them is published for the pinned version.
#
# This runs under cmake -P. Every input arrives as a -D define, because an
# external project passes its commands as one flat argument list.

if(NOT ENZO_AQT_PYTHON OR NOT ENZO_QT_HOST OR NOT ENZO_QT_VERSION
        OR NOT ENZO_QT_ROOT OR NOT ENZO_QT_ARCHS)
    message(FATAL_ERROR
        "qt-install.cmake needs ENZO_AQT_PYTHON, ENZO_QT_HOST, ENZO_QT_VERSION, "
        "ENZO_QT_ROOT and ENZO_QT_ARCHS")
endif()

# The architectures travel as one argument, separated by commas.
string(REPLACE "," ";" ENZO_QT_ARCH_LIST "${ENZO_QT_ARCHS}")

# aqtinstall takes its modules as one comma separated argument.
string(JOIN "," ENZO_QT_MODULE_LIST ${ENZO_QT_MODULES})

# aqtinstall hands work between its processes over a socket, and the path of
# that socket is built from the temporary folder. A temporary folder with a
# long path makes the socket path too long for the system to accept, so the
# inherited one is used only while it stays short enough.
set(ENZO_QT_TMPDIR "$ENV{TMPDIR}")
string(LENGTH "${ENZO_QT_TMPDIR}" ENZO_QT_TMPDIR_LENGTH)
if(NOT ENZO_QT_TMPDIR OR ENZO_QT_TMPDIR_LENGTH GREATER 70)
    set(ENZO_QT_TMPDIR "/tmp")
endif()

foreach(arch IN LISTS ENZO_QT_ARCH_LIST)
    message(STATUS "Fetching Qt ${ENZO_QT_VERSION} for ${ENZO_QT_HOST}/${arch}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "TMPDIR=${ENZO_QT_TMPDIR}"
                "${ENZO_AQT_PYTHON}" -m aqt
                --config "${ENZO_AQT_SETTINGS}"
                install-qt
                "${ENZO_QT_HOST}" desktop "${ENZO_QT_VERSION}" "${arch}"
                --modules "${ENZO_QT_MODULE_LIST}"
                --outputdir "${ENZO_QT_ROOT}"
        RESULT_VARIABLE result)
    if(result EQUAL 0)
        message(STATUS "Qt ${ENZO_QT_VERSION} installed for ${arch}")
        return()
    endif()
    message(STATUS "aqtinstall could not fetch the ${arch} build")
endforeach()

message(FATAL_ERROR "no Qt ${ENZO_QT_VERSION} build could be downloaded for ${ENZO_QT_HOST}")
