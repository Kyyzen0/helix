# Assimp is the reader of every interchange format Helix imports except obj, usd and blend:
# glTF and GLB (with Draco compression), FBX, STL, PLY, Collada, 3DS, 3MF, DXF, X3D and X.
#
# Every importer is switched off by default and the ones above are switched on, so the library
# holds no reader nobody asked for (the .blend reader of Assimp is obsolete and is left out on
# purpose: Helix converts a .blend with Blender itself). There are no exporters. Draco comes
# from the copy Assimp carries and everything links into one static library, with the zlib of the
# system (the copy of zlib Assimp carries does not compile against the current macOS SDK).

helix_add_archive_dependency(assimp
    URL ${HELIX_ASSIMP_URL}
    URL_HASH SHA256=${HELIX_ASSIMP_SHA256}
    CMAKE_ARGS
        ${HELIX_DEP_CMAKE_ARGS}
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DASSIMP_BUILD_TESTS:BOOL=OFF
        -DASSIMP_INSTALL_PDB:BOOL=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS:BOOL=OFF
        -DASSIMP_BUILD_SAMPLES:BOOL=OFF
        -DASSIMP_BUILD_DOCS:BOOL=OFF
        -DASSIMP_NO_EXPORT:BOOL=ON
        -DASSIMP_WARNINGS_AS_ERRORS:BOOL=OFF
        -DASSIMP_BUILD_ZLIB:BOOL=OFF
        -DASSIMP_BUILD_DRACO:BOOL=ON
        -DASSIMP_BUILD_DRACO_STATIC:BOOL=ON
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT:BOOL=OFF
        -DASSIMP_BUILD_GLTF_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_FBX_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_STL_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_PLY_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_COLLADA_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_3DS_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_3MF_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_DXF_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_X3D_IMPORTER:BOOL=ON
        -DASSIMP_BUILD_X_IMPORTER:BOOL=ON
)
list(APPEND HELIX_DEPS assimp)
