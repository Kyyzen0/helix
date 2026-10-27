# Configures only the Boost libraries helix names. Boost pulls in whatever those
# depend on by itself.
#
# The library list is a cmake list, so the semicolons have to survive being
# written into a command line. LIST_SEPARATOR carries them across as bars.
set(HELIX_BOOST_LIBRARIES algorithm container_hash dll filesystem signals2 system)
list(JOIN HELIX_BOOST_LIBRARIES | HELIX_BOOST_LIBRARY_ARG)

# The Boost archive carries git and editor metadata that a sandbox may refuse
# to write, which makes the automatic extraction fail on the first such file.
#
# The tree can instead be prepared ahead of time with
# scripts/prepare-sources.sh, which leaves those files out. When that has
# happened, the source directory is used as it is and no extraction runs.
helix_add_archive_dependency(boost
    URL ${HELIX_BOOST_URL}
    URL_HASH SHA256=${HELIX_BOOST_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DBOOST_INCLUDE_LIBRARIES:STRING=${HELIX_BOOST_LIBRARY_ARG}
        -DBUILD_SHARED_LIBS:BOOL=ON
        -DBOOST_INSTALL_LAYOUT:STRING=system
)
list(APPEND HELIX_DEPS boost)
