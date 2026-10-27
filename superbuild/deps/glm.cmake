# GLM is built header only, so nothing links a glm library.
helix_add_archive_dependency(glm
    URL ${HELIX_GLM_URL}
    URL_HASH SHA256=${HELIX_GLM_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DGLM_BUILD_LIBRARY:BOOL=OFF
        -DGLM_BUILD_TESTS:BOOL=OFF
        -DGLM_BUILD_INSTALL:BOOL=ON
)
list(APPEND HELIX_DEPS glm)
