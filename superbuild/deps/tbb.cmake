# TBB is the parallel backend for both helix and manifold, so the two share this
# one copy of the scheduler.
helix_add_archive_dependency(tbb
    URL ${HELIX_TBB_URL}
    URL_HASH SHA256=${HELIX_TBB_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DTBB_TEST:BOOL=OFF
        -DTBB_EXAMPLES:BOOL=OFF
        -DTBB_STRICT:BOOL=OFF
        -DTBBMALLOC_BUILD:BOOL=OFF
)
list(APPEND HELIX_DEPS tbb)
