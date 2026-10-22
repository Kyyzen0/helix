# Adds a dependency that is downloaded as an archive and built from source.
#
# When the source tree is already present under the superbuild's Source
# folder, it is built from there and nothing is downloaded or extracted. That
# is the case when the tree was prepared by scripts/prepare-sources.sh, which
# is needed on a machine where the archive carries files that cannot be
# written, such as git and editor metadata.
#
#   enzo_add_archive_dependency(<name>
#       URL <url>
#       URL_HASH <hash>
#       [DEPENDS <dep>...]
#       [CMAKE_ARGS <arg>...]
#       )

include(ExternalProject)

function(enzo_add_archive_dependency name)
    cmake_parse_arguments(ARG "" "URL;URL_HASH" "DEPENDS;CMAKE_ARGS" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "enzo_add_archive_dependency: unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(source "${ENZO_SOURCE_ROOT}/${name}")

    if(EXISTS "${source}/CMakeLists.txt")
        message(STATUS "Building ${name} from the source in ${source}")
        ExternalProject_Add(${name}
            SOURCE_DIR ${source}
            LIST_SEPARATOR |
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E true
            UPDATE_COMMAND ""
            DEPENDS ${ARG_DEPENDS}
            CMAKE_ARGS ${ARG_CMAKE_ARGS}
        )
    else()
        ExternalProject_Add(${name}
            URL ${ARG_URL}
            URL_HASH ${ARG_URL_HASH}
            LIST_SEPARATOR |
            DEPENDS ${ARG_DEPENDS}
            CMAKE_ARGS ${ARG_CMAKE_ARGS}
        )
    endif()
endfunction()
