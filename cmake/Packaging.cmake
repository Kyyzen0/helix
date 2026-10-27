# Turns the installed tree into a tar.gz package of the application bundle, and
# gathers the Qt and third party libraries the application loads at runtime.

set(CPACK_GENERATOR "TGZ")

set(CPACK_PACKAGE_NAME Helix)
set(CPACK_PACKAGE_VENDOR "The Helix contributors")
set(CPACK_PACKAGE_CONTACT "Parker Britt <parker@parkerbritt.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Kyyzen0/helix")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Node based procedural 3D modeling framework")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Helix")
configure_file(${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_BINARY_DIR}/LICENSE.txt COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_BINARY_DIR}/LICENSE.txt)
set(CPACK_PACKAGE_EXECUTABLES ${AppExec} "Helix")
set(CPACK_STRIP_FILES ON)

# Names the archive after the platform it was built for.
set(CPACK_SYSTEM_NAME macos-${CMAKE_SYSTEM_PROCESSOR})

# The executable sits one folder deeper than the libraries, so it reaches them
# through the folder above it.
#
# A bundle keeps its executable in MacOS and its libraries in Frameworks and
# lib, both of which sit next to MacOS inside Contents.
set_target_properties(${AppExec} PROPERTIES
    INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../lib"
)

include(CPack)

# Copies the Qt libraries, plugins and qml modules the app reaches at install
# time, along with a qt.conf pointing at them.
qt_generate_deploy_qml_app_script(
    TARGET ${AppExec}
    OUTPUT_SCRIPT HELIX_QT_DEPLOY_SCRIPT
)

# Sets the destinations in the install script itself, since Qt reads them when
# the deployment runs rather than when it is generated.
#
# A bundle keeps its frameworks, plugins and resources in the folders the
# system expects to find them in.
install(CODE "
    set(QT_DEPLOY_BIN_DIR \"${HELIX_BIN_DIR}\")
    set(QT_DEPLOY_LIB_DIR \"${HELIX_APP_DIR}Frameworks\")
    set(QT_DEPLOY_LIBEXEC_DIR \"${HELIX_APP_DIR}Helpers\")
    set(QT_DEPLOY_PLUGINS_DIR \"${HELIX_APP_DIR}PlugIns\")
    set(QT_DEPLOY_QML_DIR \"${HELIX_APP_DIR}Resources/qml\")
    set(QT_DEPLOY_TRANSLATIONS_DIR \"${HELIX_APP_DIR}Resources/translations\")
")
install(SCRIPT ${HELIX_QT_DEPLOY_SCRIPT})

# Installs helix's own binaries after the Qt deployment, so they land with their
# install runpath.
#
# The bundle carries the executable, so it is installed as a whole into the
# folder the bundle belongs in.
install(TARGETS ${AppExec} helixEngine
    RUNTIME_DEPENDENCY_SET helixRuntime
    BUNDLE DESTINATION ${HELIX_BUNDLE_DIR}
    LIBRARY DESTINATION ${HELIX_LIB_DIR}
    ARCHIVE DESTINATION ${HELIX_LIB_DIR}
)

# The compiled viewport shaders, which the renderer loads from the Resources folder of the
# bundle. Their absence is an error at install time, since a bundle without them cannot draw.
if(HELIX_HAS_METAL_COMPILER)
    install(FILES ${HELIX_METALLIB} DESTINATION ${HELIX_APP_DIR}Resources)
else()
    install(CODE "message(FATAL_ERROR \"The bundle needs helix.metallib, which this build did not compile\")")
endif()

# The shared libraries the dependencies built, so the bundle carries them and
# the loader finds them next to the libraries helix itself installed.
#
# The Qt libraries are left out, since the Qt deployment copies them into the
# bundle's own framework folder.
set(HELIX_DEP_LIB_FILES "")
if(HELIX_DEPS_PREFIX)
    file(GLOB HELIX_DEP_LIB_FILES "${HELIX_DEPS_PREFIX}/lib/*.dylib")
endif()
if(HELIX_DEP_LIB_FILES)
    install(FILES ${HELIX_DEP_LIB_FILES} DESTINATION ${HELIX_LIB_DIR})
endif()

# Puts the launcher on PATH, so a shell reaches it by name.
install(CODE "
    set(root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    file(MAKE_DIRECTORY \"\${root}/bin\")
    file(REMOVE \"\${root}/bin/${AppExec}\")
    file(CREATE_LINK \"../${HELIX_BIN_DIR}/${AppExec}\" \"\${root}/bin/${AppExec}\" SYMBOLIC)
")

# Installs the licence into the folder holding bin and lib.
cmake_path(SET HELIX_APP_ROOT NORMALIZE "${HELIX_APP_DIR}.")
install(FILES ${CMAKE_BINARY_DIR}/LICENSE.txt
    DESTINATION ${HELIX_APP_ROOT}
)

# The fonts and icons the application reads at runtime. The theme is compiled
# into the binary.
install(DIRECTORY static/
    DESTINATION ${HELIX_APP_DIR}share
    PATTERN "theme" EXCLUDE
)

# Installs daslib beside helix/bin, where the runtime looks for the standard
# library.
install(DIRECTORY ${DAS_DASLIB_DIR}/
    DESTINATION ${HELIX_APP_DIR}daslib
)

# Signs the bundle, once every file in it is in place.
#
# The Qt deployment rewrites the frameworks and the plugins it copies, and
# that breaks the signatures they carry. macOS refuses to load a library whose
# signature no longer matches, so the application would be killed before it
# opens a window.
install(CODE "
    set(bundle \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${HELIX_BUNDLE_DIR}/${CMAKE_PROJECT_NAME}.app\")
    include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignBundle.cmake\")
")
