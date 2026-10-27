# Builds every dependency into one shared prefix, then configures helix against
# it.

include(ExternalProject)

set(HELIX_DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps"
    CACHE PATH "Where the superbuild installs its dependencies")

set_directory_properties(PROPERTIES EP_BASE ${CMAKE_BINARY_DIR}/superbuild)

# Where the dependency sources live. A dependency whose source is already in
# this folder is built from there, instead of being extracted again from its
# archive.
set(HELIX_SOURCE_ROOT "${CMAKE_BINARY_DIR}/superbuild/Source")

# Collects each dependency as it is added, so helix can depend on the whole set.
set(HELIX_DEPS)

# Include Qt first, since the folder it unpacks into is the second prefix
# everything below searches.
include(deps/qt.cmake)

# The settings shared by the dependencies and by helix, so one compiler, language
# standard and ABI covers the whole prefix.
#
# Joins the two search prefixes with a bar, which LIST_SEPARATOR turns back into
# a semicolon in each sub project.
set(HELIX_TOOLCHAIN_CMAKE_ARGS
    -DCMAKE_PREFIX_PATH:STRING=${HELIX_DEPS_PREFIX}|${HELIX_QT_PREFIX}
    -DCMAKE_CXX_STANDARD:STRING=20
    -DCMAKE_CXX_STANDARD_REQUIRED:BOOL=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
)

# The oldest macOS the whole tree is built for, and the one architecture it is
# built for. Every dependency and helix itself take the same values, so no
# library ends up asking for a system release the host does not have, or
# carrying a slice for an architecture helix does not ship.
#
# The floor is 13.3, because the standard library of the current Xcode pulls
# the formatting header in through the stream header, and the conversion of a
# floating point number it uses there was only added to the system in 13.3.
set(HELIX_OSX_DEPLOYMENT_TARGET "13.3")
list(APPEND HELIX_TOOLCHAIN_CMAKE_ARGS
    -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${HELIX_OSX_DEPLOYMENT_TARGET}
    -DCMAKE_OSX_ARCHITECTURES:STRING=arm64)

# Forwards the toolchain settings that were actually chosen.
foreach(setting IN ITEMS
    CMAKE_BUILD_TYPE
    CMAKE_C_COMPILER
    CMAKE_CXX_COMPILER
    CMAKE_TOOLCHAIN_FILE
    CMAKE_MAKE_PROGRAM
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_SHARED_LINKER_FLAGS)
    if(NOT "${${setting}}" STREQUAL "")
        list(APPEND HELIX_TOOLCHAIN_CMAKE_ARGS "-D${setting}:STRING=${${setting}}")
    endif()
endforeach()

# The tools that the host provides through the environment, such as the bison
# the daslang parsers are generated with. The environment is not a cmake
# variable, so these are read from it directly.
foreach(tool IN ITEMS BISON_EXECUTABLE FLEX_EXECUTABLE)
    if(NOT "$ENV{${tool}}" STREQUAL "")
        list(APPEND HELIX_TOOLCHAIN_CMAKE_ARGS "-D${tool}:STRING=$ENV{${tool}}")
    endif()
endforeach()

# daslang regenerates its parsers from a grammar during the build, and the
# committed parsers were produced by bison 3.8.2. An older bison generates a
# parser daslang cannot compile, and macOS ships bison 2.3.
#
# The version is checked here, where the toolchain of the host is still known,
# rather than in the build of daslang, where a wrong bison shows up as a parse
# error in a file of a third party project. The toolchain of the repository
# carries a bison that is new enough, and the message points at it.
if(APPLE)
    if(NOT "$ENV{BISON_EXECUTABLE}" STREQUAL "")
        set(helix_bison "$ENV{BISON_EXECUTABLE}")
    else()
        find_program(helix_bison NAMES bison)
    endif()

    if(NOT helix_bison)
        message(FATAL_ERROR
            "daslang needs bison 3.8 or newer to regenerate its parsers, and none was found. "
            "Run ./scripts/bootstrap-macos.sh and source toolchain/env.sh before configuring the build.")
    endif()

    execute_process(
        COMMAND "${helix_bison}" --version
        OUTPUT_VARIABLE helix_bison_version_output
        ERROR_QUIET
    )
    string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" helix_bison_version "${helix_bison_version_output}")

    if(helix_bison_version VERSION_LESS "3.8")
        message(FATAL_ERROR
            "daslang needs bison 3.8 or newer to regenerate its parsers, and ${helix_bison} is ${helix_bison_version}. "
            "Run ./scripts/bootstrap-macos.sh and source toolchain/env.sh before configuring the build.")
    endif()

    message(STATUS "The parsers of daslang are generated with bison ${helix_bison_version}, at ${helix_bison}")
endif()

# What every dependency is configured with.
set(HELIX_DEP_CMAKE_ARGS
    ${HELIX_TOOLCHAIN_CMAKE_ARGS}
    -DCMAKE_INSTALL_PREFIX:PATH=${HELIX_DEPS_PREFIX}
    -DBUILD_TESTING:BOOL=OFF
)

# The libraries end up beside each other in the packaged tree, so they look for
# their siblings next to themselves rather than in the prefix they were built in.
#
# The loader path keyword names the directory of the
# binary that carries the path, so a library next to itself is reached the same
# way.
list(APPEND HELIX_DEP_CMAKE_ARGS -DCMAKE_INSTALL_RPATH:STRING=@loader_path)

include(deps/archive.cmake)
include(deps/tbb.cmake)
include(deps/boost.cmake)
include(deps/manifold.cmake)
include(deps/eigen.cmake)
include(deps/glm.cmake)
include(deps/argparse.cmake)
include(deps/fastnoise2.cmake)

# The settings of helix itself that a caller may give to the superbuild, which are handed
# on to the build of helix when they are set.
set(HELIX_PASSTHROUGH_ARGS)
if(HELIX_METAL_CPP_DIR)
    list(APPEND HELIX_PASSTHROUGH_ARGS -DHELIX_METAL_CPP_DIR:PATH=${HELIX_METAL_CPP_DIR})
endif()
if(HELIX_ALLOW_RUNTIME_SHADERS)
    list(APPEND HELIX_PASSTHROUGH_ARGS -DHELIX_ALLOW_RUNTIME_SHADERS:BOOL=${HELIX_ALLOW_RUNTIME_SHADERS})
endif()

# Builds helix last, into a normal cmake build folder that ninja can be run in
# directly.
ExternalProject_Add(helix
    SOURCE_DIR ${CMAKE_SOURCE_DIR}
    BINARY_DIR ${CMAKE_BINARY_DIR}/helix-build
    DEPENDS ${HELIX_DEPS}
    LIST_SEPARATOR |
    CMAKE_ARGS
        ${HELIX_TOOLCHAIN_CMAKE_ARGS}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
        -DHELIX_SUPERBUILD:BOOL=OFF
        ${HELIX_PASSTHROUGH_ARGS}
        # helix copies the shared libraries the dependencies built into its own
        # tree, so it needs to know where they are.
        -DHELIX_DEPS_PREFIX:PATH=${HELIX_DEPS_PREFIX}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)
