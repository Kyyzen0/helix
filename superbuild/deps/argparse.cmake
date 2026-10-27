# Argparse is header only, and gives the gui executable its command line.
helix_add_archive_dependency(argparse
    URL ${HELIX_ARGPARSE_URL}
    URL_HASH SHA256=${HELIX_ARGPARSE_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DARGPARSE_BUILD_TESTS:BOOL=OFF
        -DARGPARSE_BUILD_SAMPLES:BOOL=OFF
)
list(APPEND HELIX_DEPS argparse)
