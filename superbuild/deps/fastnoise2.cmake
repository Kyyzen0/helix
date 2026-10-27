# FastNoise2 provides the noise nodes. It fetches its own FastSIMD at configure
# time, so that version is left to FastNoise2.

helix_add_archive_dependency(fastnoise2
    URL ${HELIX_FASTNOISE2_URL}
    URL_HASH SHA256=${HELIX_FASTNOISE2_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DFASTNOISE2_TOOLS:BOOL=OFF
        -DFASTNOISE2_TESTS:BOOL=OFF
        -DFASTNOISE2_UTILITY:BOOL=OFF
        # Only the nodes call FastNoise2, so it links in rather than shipping as
        # its own library.
        -DBUILD_SHARED_LIBS:BOOL=OFF
)
list(APPEND HELIX_DEPS fastnoise2)
