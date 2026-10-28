cmake_minimum_required(VERSION 3.26)

# Helix builds for macOS on Apple silicon only. The architecture has to be
# chosen before project(), which is where the compiler is detected.
#
# The oldest macOS the bundle claims to run on. Every dependency is built for
# it too, so the tree stays free of a library the host cannot load.
#
# The floor is 13.3, because the standard library of the current Xcode pulls
# the formatting header in through the stream header, and the conversion of a
# floating point number it uses there was only added to the system in 13.3.
# Eigen reaches the stream header, so every file that includes it needs a
# release that has the conversion.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Helix targets Apple silicon only" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Oldest macOS the build supports")

# set vars
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
# set exec names
set(AppExec helix)
set(TestExec tests)
set(BenchExec bench)
set(CMAKE_AUTORCC ON)

# setup project
project(helix VERSION 0.7.0)

if(NOT APPLE)
    message(FATAL_ERROR "Helix builds on macOS only.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/versions.cmake)

# Builds the pinned dependencies first, then configures this same project again
# against them.
option(HELIX_SUPERBUILD "Build the pinned dependencies, then helix against them" ON)
if(HELIX_SUPERBUILD)
    add_subdirectory(superbuild)
    return()
endif()

# The one file that hands a Metal texture to the Qt Quick scene graph is Objective-C++,
# since the scene graph takes its Metal objects as Objective-C ids.
enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# The installed libraries sit in one directory, so they find each other next to
# themselves. The executable overrides this, since it sits in bin.
#
# macOS has no $ORIGIN. The loader path keyword names the directory of the
# binary that carries the path, so a library next to itself is reached the
# same way.
#
# A library sits next to the libraries it shares a folder with and reaches the
# frameworks one folder up, which is where the bundle keeps them.
set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../Frameworks")

set(HELIX_DEV_LIB_DIR "${CMAKE_BINARY_DIR}/nodes/")
set(HELIX_DEV_NODES_DIR "${CMAKE_SOURCE_DIR}/nodes")

set(ENGINE_SOURCES
    src/Engine/Attribute/Attribute.cpp
    src/Engine/Attribute/Transform.cpp
    src/Engine/Primitives/Primitive.cpp
    src/Engine/Primitives/Mesh.cpp
    src/Engine/Primitives/Camera.cpp
    src/Engine/Selection/PrimPath.cpp
    src/Engine/Core/InstallPaths.cpp
    src/Engine/Core/Path.cpp
    src/Engine/Network/Network.cpp
    src/Engine/Network/NetworkPath.cpp
    src/Engine/Network/NodePacket.cpp
    src/Engine/Network/Node.cpp
    src/Engine/Network/NodeSnapshot.cpp
    src/Engine/Network/NodeImpl.cpp
    src/Engine/Network/NodeLoader.cpp
    src/Engine/Network/NodeManifest.cpp
    src/Engine/Network/NodeTypeTable.cpp
    src/Engine/Network/CookContext.cpp
    src/Engine/NetworkGraph/NetworkGraph.cpp
    src/Engine/Daslang/Runtime.cpp
    src/Engine/Expression/ExpressionContext.cpp
    src/Engine/Expression/ExpressionEngine.cpp
    src/Engine/Script/ScriptEngine.cpp
    src/Engine/Expression/ExpressionFunctions.cpp
    src/Engine/Script/GeometryFunctions.cpp
    src/Engine/Script/PointScript.cpp
    src/Engine/Script/MeshAttributeReader.cpp
    src/Engine/Script/ScriptContext.cpp
    src/Engine/Parameter/Template.cpp
    src/Engine/Parameter/Parameter.cpp
    src/Engine/Parameter/Ramp.cpp
    src/Engine/Parameter/NodeParameter.cpp
    src/Engine/Parameter/Default.cpp
    src/Engine/Parameter/Range.cpp
    src/Engine/Parameter/PrmName.cpp
    src/Engine/Network/NetworkManager.cpp
    src/Engine/Network/UpdateLock.cpp
    src/Engine/Async/CookPlan.cpp
    src/Engine/Async/CookResult.cpp
    src/Engine/Async/CookScheduler.cpp
    src/Engine/Async/CookSnapshot.cpp
    src/Engine/Simulation/KeyframeStore.cpp
    src/Engine/Simulation/ParticleSolver.cpp
    src/Engine/Simulation/SpringSolver.cpp
    src/Engine/UndoRedo/UndoStack.cpp
    src/Engine/Serializer/Serializer.cpp
    src/Engine/Selection/Selection.cpp
    src/Engine/Selection/SelectionComponent.cpp
    src/Engine/Selection/IndexSet.cpp
    src/Engine/GeometryAlgorithms/MeshUtils.cpp
    src/Engine/GeometryAlgorithms/Normals.cpp
    src/Engine/GeometryAlgorithms/MeshShapes.cpp
    src/Engine/GeometryAlgorithms/BooleanUtils.cpp
    src/Engine/GeometryAlgorithms/AttributeTransfer.cpp
    src/Engine/GeometryAlgorithms/AttributeOperation.cpp
    src/Engine/GeometryAlgorithms/PlaneCut.cpp
    src/Engine/GeometryAlgorithms/ObjReader.cpp
    src/Engine/Util/UnionFind.cpp
)

set(ENGINE_INCLUDE_DIRECTORIES
    src
    extern/icecream-cpp/include
    extern/cereal/include
)


# qt
#
# The scene graph of Qt Quick runs on Metal on macOS, and the viewport hands it
# a Metal texture, so Qt needs nothing beyond the Quick module for it.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets SvgWidgets Quick Qml QuickControls2 ShaderTools)
qt_standard_project_setup()
ADD_DEFINITIONS(-DQT_NO_KEYWORDS)

# glm
find_package(glm REQUIRED)

# eigen (math)
find_package (Eigen3 REQUIRED NO_MODULE)

# tbb
find_package(TBB REQUIRED COMPONENTS tbb)

find_package(Boost REQUIRED COMPONENTS filesystem system)

# argparse (command line)
find_package(argparse REQUIRED)

# manifold (boolean ops)
find_package(manifold CONFIG REQUIRED)

# FastNoise2 (noise nodes)
find_package(FastNoise2 CONFIG REQUIRED)

# yaml-cpp (node manifests and themes)
include(FetchContent)
FetchContent_Declare(
  yaml-cpp
  URL      ${HELIX_YAML_CPP_URL}
  URL_HASH SHA256=${HELIX_YAML_CPP_SHA256}
)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

# metal-cpp (viewport renderer), Apple's header only C++ interface to Metal.
#
# The pinned archive is downloaded from Apple and checked against its hash. A folder
# holding the headers (the one with Metal/Metal.hpp in it) can be given instead, to
# build without network access:
#
#   cmake -DHELIX_METAL_CPP_DIR=/path/to/metal-cpp ...
set(HELIX_METAL_CPP_DIR "" CACHE PATH "A metal-cpp folder to use instead of the pinned download")
if(HELIX_METAL_CPP_DIR)
    set(HELIX_METAL_CPP_INCLUDE_DIR "${HELIX_METAL_CPP_DIR}")
else()
    FetchContent_Declare(
      metalcpp
      URL      ${HELIX_METAL_CPP_URL}
      URL_HASH SHA256=${HELIX_METAL_CPP_SHA256}
    )
    FetchContent_MakeAvailable(metalcpp)
    set(HELIX_METAL_CPP_INCLUDE_DIR "${metalcpp_SOURCE_DIR}")
endif()
if(NOT EXISTS "${HELIX_METAL_CPP_INCLUDE_DIR}/Metal/Metal.hpp")
    message(FATAL_ERROR "metal-cpp was not found: ${HELIX_METAL_CPP_INCLUDE_DIR}/Metal/Metal.hpp does not exist")
endif()

# daslang (das scripting language), built from the vendored submodule
set(DASLANG_DIR "${CMAKE_SOURCE_DIR}/extern/daslang")

# Trim daslang to the core libraries helix links against.
set(DAS_TOOLS_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_TESTS_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_TUTORIAL_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_AOT_EXAMPLES_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_TREE_SITTER_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_GLFW_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_AUDIO_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_STDDLG_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_STBIMAGE_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_PUGIXML_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_HV_DISABLED ON CACHE BOOL "" FORCE)
set(DAS_SQLITE_DISABLED ON CACHE BOOL "" FORCE)

# Leaves the global operator new and delete alone. helixEngine and each node
# library link daslang separately, so its allocation cache cannot own every
# free.
add_compile_definitions(DAS_NO_GLOBAL_NEW_AND_DELETE)

add_subdirectory(${DASLANG_DIR} ${CMAKE_BINARY_DIR}/daslang EXCLUDE_FROM_ALL)

# daslang writes its static libraries into its own source tree. The archive
# tools remove the previous library before they write the new one, so a build
# that keeps them there cannot replace a library from an earlier build, and it
# leaves the checkout changed. The libraries go into the build tree instead.
get_property(daslang_targets DIRECTORY ${DASLANG_DIR} PROPERTY BUILDSYSTEM_TARGETS)
foreach(daslang_target IN LISTS daslang_targets)
    get_target_property(daslang_target_type ${daslang_target} TYPE)
    if(daslang_target_type STREQUAL "STATIC_LIBRARY")
        set_target_properties(${daslang_target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/daslang/lib")
    endif()
endforeach()

# daslang exposes its public headers only as a private requirement, so the
# include path is added by hand.
list(APPEND ENGINE_INCLUDE_DIRECTORIES ${DASLANG_DIR}/include)

# daslang loads its standard library (daslib) from a folder next to the bin
# directory of the running executable.
set(DAS_DASLIB_DIR "${DASLANG_DIR}/daslib")

# A build stages into the bundle, since the runtime reads its content from the
# folder holding the executable.
set(HELIX_DEV_STAGE_DIR "${CMAKE_BINARY_DIR}/helix.app/Contents")

# daslib sits beside the staged bin directory so daslang resolves its standard
# library the same way it does from an install.
file(COPY ${DAS_DASLIB_DIR} DESTINATION ${HELIX_DEV_STAGE_DIR})

# daslang is built without RTTI, so the files that subclass its types match it.
# Otherwise their typeinfo references daslang symbols that were never emitted.
set_source_files_properties(
    src/Engine/Daslang/Runtime.cpp
    src/Engine/Expression/ExpressionFunctions.cpp
    src/Engine/Script/GeometryFunctions.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-rtti"
)

# Release flags for Apple silicon. They are set here, after the third party
# libraries above, so they reach helix's own targets (the engine, the graphics
# library, the application, the nodes and the tests) and leave the libraries
# built from vendored or fetched sources as their own build files configure
# them.
#
# -mcpu=apple-m1 targets the first Apple silicon core, which every later one
# runs, and is added only where the compiler accepts it.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mcpu=apple-m1 HELIX_HAS_MCPU_APPLE_M1)
add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-O3>")
if(HELIX_HAS_MCPU_APPLE_M1)
    add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-mcpu=apple-m1>")
endif()

# Link time optimization in Release, again for helix's own targets only: the
# variable is read when a target is created, and the targets below are the
# first ones made after this point.
option(HELIX_ENABLE_LTO "Build Release with link time optimization" ON)
if(HELIX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HELIX_IPO_SUPPORTED OUTPUT HELIX_IPO_OUTPUT LANGUAGES CXX)
    if(HELIX_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "Link time optimization is not available: ${HELIX_IPO_OUTPUT}")
    endif()
endif()

# The engine is a self contained library with no knowledge of the ui or the
# operator plugins. The ui executable and the plugins both link against it, so
# the whole process shares one engine and one set of its singletons. The engine
# loads the plugins at runtime rather than linking them.
add_library(helixEngine SHARED ${ENGINE_SOURCES})
target_link_libraries(helixEngine PUBLIC glm::glm Eigen3::Eigen TBB::tbb Boost::filesystem Boost::system manifold::manifold libDaScript yaml-cpp::yaml-cpp)
target_include_directories(helixEngine PUBLIC ${ENGINE_INCLUDE_DIRECTORIES})
target_compile_definitions(helixEngine PRIVATE
    HELIX_DEV_LIB_DIR="${HELIX_DEV_LIB_DIR}"
    HELIX_DEV_NODES_DIR="${HELIX_DEV_NODES_DIR}"
    HELIX_DEV_STATIC_DIR="${CMAKE_SOURCE_DIR}/static"
)



# Compiles the viewport shaders into one Metal library at build time.
#
# The library is staged in the Resources folder of the application bundle of the build tree,
# which the app and the tests, both of which sit inside that bundle, find beside their
# executable, and the install copies it into the installed bundle.
#
# The offline Metal compiler is a component of Xcode. Xcode 26 and later install it apart:
#
#   xcodebuild -downloadComponent MetalToolchain
#
# A machine without it can still run a development build, which compiles the shader
# sources when the program starts. Pass -DHELIX_ALLOW_RUNTIME_SHADERS=ON to configure
# without the compiler; the packages are never built that way.
option(HELIX_ALLOW_RUNTIME_SHADERS "Configure without the offline Metal compiler" OFF)
set(HELIX_METAL_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/Graphics/Shaders")
set(HELIX_METAL_SHADERS CameraPrim Grid Mesh Points)
set(HELIX_METALLIB "${HELIX_DEV_STAGE_DIR}/Resources/helix.metallib")

execute_process(
    COMMAND xcrun -sdk macosx metal --version
    RESULT_VARIABLE HELIX_METAL_COMPILER_RESULT
    OUTPUT_QUIET
    ERROR_VARIABLE HELIX_METAL_COMPILER_ERROR
)
if(HELIX_METAL_COMPILER_RESULT EQUAL 0)
    set(HELIX_HAS_METAL_COMPILER TRUE)
elseif(HELIX_ALLOW_RUNTIME_SHADERS)
    set(HELIX_HAS_METAL_COMPILER FALSE)
    message(WARNING "The Metal compiler is not available, so the shaders are compiled at startup: ${HELIX_METAL_COMPILER_ERROR}")
else()
    message(FATAL_ERROR
        "The Metal compiler is not available: ${HELIX_METAL_COMPILER_ERROR}\n"
        "Install it with: xcodebuild -downloadComponent MetalToolchain")
endif()

if(HELIX_HAS_METAL_COMPILER)
    set(HELIX_AIR_FILES)
    foreach(shaderName IN LISTS HELIX_METAL_SHADERS)
        set(shaderSource "${HELIX_METAL_SHADER_DIR}/${shaderName}.metal")
        set(shaderAir "${CMAKE_BINARY_DIR}/shaders/${shaderName}.air")
        add_custom_command(
            OUTPUT ${shaderAir}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND xcrun -sdk macosx metal -c
                    -std=metal3.0
                    -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
                    -I "${HELIX_METAL_SHADER_DIR}"
                    -o ${shaderAir} ${shaderSource}
            DEPENDS ${shaderSource} "${HELIX_METAL_SHADER_DIR}/ShaderTypes.h"
            COMMENT "Compiling ${shaderName}.metal"
            VERBATIM
        )
        list(APPEND HELIX_AIR_FILES ${shaderAir})
    endforeach()

    add_custom_command(
        OUTPUT ${HELIX_METALLIB}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HELIX_DEV_STAGE_DIR}/Resources"
        COMMAND xcrun -sdk macosx metallib ${HELIX_AIR_FILES} -o ${HELIX_METALLIB}
        DEPENDS ${HELIX_AIR_FILES}
        COMMENT "Linking helix.metallib"
        VERBATIM
    )
    add_custom_target(helixMetalLib ALL DEPENDS ${HELIX_METALLIB})
endif()

# Renders the viewport without depending on Qt.
add_library(helixGraphics STATIC
    src/Graphics/DisplayGeometry.cpp
    src/Graphics/GraphicsDevice.cpp
    # Holds the implementation of metal-cpp, which exactly one file of the program may define.
    src/Graphics/MetalImplementation.cpp
    src/Graphics/ViewportCamera.cpp
    src/Graphics/ViewportRenderer.cpp
    src/Graphics/Passes/CameraPrimPass.cpp
    src/Graphics/Passes/GridPass.cpp
    src/Graphics/Passes/MeshPass.cpp
    src/Graphics/Passes/PassSetup.cpp
    src/Graphics/Passes/PointPass.cpp
)
find_library(HELIX_FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(HELIX_METAL_FRAMEWORK Metal REQUIRED)
find_library(HELIX_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
target_link_libraries(helixGraphics PUBLIC helixEngine
    ${HELIX_FOUNDATION_FRAMEWORK} ${HELIX_METAL_FRAMEWORK} ${HELIX_QUARTZCORE_FRAMEWORK})
target_include_directories(helixGraphics PUBLIC src)
# metal-cpp is a third party header only library, whose warnings are not ours to fix.
target_include_directories(helixGraphics SYSTEM PUBLIC ${HELIX_METAL_CPP_INCLUDE_DIR})
# Where a development build looks for the shaders when no library sits in the bundle.
target_compile_definitions(helixGraphics PRIVATE
    HELIX_DEV_METALLIB="${HELIX_METALLIB}"
    HELIX_DEV_SHADER_DIR="${HELIX_METAL_SHADER_DIR}"
)
if(HELIX_HAS_METAL_COMPILER)
    add_dependencies(helixGraphics helixMetalLib)
endif()


# QML application
qt_add_executable(${AppExec}
    src/Gui/main.cpp
    src/Gui/Style/ThemeLoader.cpp
    src/Gui/Style/ThemeStore.cpp
    src/Gui/Style/ThemeViewModel.cpp
    src/Gui/Spreadsheet/AttributeTableModel.cpp
    src/Gui/Spreadsheet/PrimitiveTreeModel.cpp
    src/Gui/Spreadsheet/SpreadsheetViewModel.cpp
    src/Gui/Network/NodeListModel.cpp
    src/Gui/Network/NodeLinkListModel.cpp
    src/Gui/Network/NetworkViewModel.cpp
    src/Gui/Parameters/ParameterItem.cpp
    src/Gui/Parameters/ParametersViewModel.cpp
    src/Gui/Timeline/TimelineViewModel.cpp
    src/Gui/Controllers/SceneController.cpp
    src/Gui/Viewport/ViewportViewModel.cpp
    src/Gui/Viewport/QtGraphicsBridge.mm
    src/Gui/Viewport/ViewportNode.cpp
)

# Register Constants.qml and ParameterClipboard.qml as singletons
set_source_files_properties(src/Gui/Style/Constants.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(src/Gui/Components/ParameterClipboard.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(${AppExec}
    URI Helix
    VERSION 1.0
    # The prefix the qml engine searches by default, so an installed run finds
    # the module compiled into the binary.
    RESOURCE_PREFIX /qt/qml
    SOURCES
        src/Gui/Style/Theme.cpp
        src/Gui/Components/IconCursor.cpp
        src/Gui/Network/NodeLinkLayer.cpp
        src/Gui/Parameters/RampCurveItem.cpp
        src/Gui/Viewport/ViewportItem.cpp
    QML_FILES
        src/Gui/App.qml
        src/Gui/Utils.js
        src/Gui/Style/Constants.qml
        src/Gui/Components/Icon.qml
        src/Gui/Components/IconButton.qml
        src/Gui/Components/Panel.qml
        src/Gui/Components/ParameterLabel.qml
        src/Gui/Components/ScrollArea.qml
        src/Gui/Components/Slider.qml
        src/Gui/Components/PopupList.qml
        src/Gui/Components/Dropdown.qml
        src/Gui/Components/ExpressionEditor.qml
        src/Gui/Components/FocusReclaimer.qml
        src/Gui/Components/EditDismisser.qml
        src/Gui/Components/Stepper.qml
        src/Gui/Components/TextButton.qml
        src/Gui/Components/Tooltip.qml
        src/Gui/Components/NodePathLabel.qml
        src/Gui/Components/PlaceholderDialog.qml
        src/Gui/Components/ParameterClipboard.qml
        src/Gui/Components/ParameterContextMenu.qml
        src/Gui/Components/ParameterContextMenuAction.qml
        src/Gui/Components/ParameterContextMenuRow.qml
        src/Gui/Components/ParameterContextMenuTrigger.qml
        src/Gui/Components/PanelSurface.qml
        src/Gui/Components/SectionLabel.qml
        src/Gui/Components/SplitterHandle.qml
        src/Gui/Components/StatusBar.qml
        src/Gui/Components/TypeBadge.qml
        src/Gui/MenuBar/AppMenuBar.qml
        src/Gui/MenuBar/MenuBar.qml
        src/Gui/MenuBar/MenuPopup.qml
        src/Gui/Parameters/ParametersPanel.qml
        src/Gui/Parameters/ParameterRow.qml
        src/Gui/Parameters/ParameterList.qml
        src/Gui/Parameters/FloatSlider.qml
        src/Gui/Parameters/IntSlider.qml
        src/Gui/Parameters/Toggle.qml
        src/Gui/Parameters/VectorParameter.qml
        src/Gui/Parameters/RangeDial.qml
        src/Gui/Parameters/StringParameter.qml
        src/Gui/Parameters/CodeParameter.qml
        src/Gui/Parameters/AttributeParameter.qml
        src/Gui/Parameters/DropdownParameter.qml
        src/Gui/Parameters/Group.qml
        src/Gui/Parameters/Divider.qml
        src/Gui/Parameters/RampEditor.qml
        src/Gui/Network/Network.qml
        src/Gui/Network/NetworkBackground.qml
        src/Gui/Network/NetworkEmptyHint.qml
        src/Gui/Network/NetworkNodes.qml
        src/Gui/Network/Node.qml
        src/Gui/Network/NetworkViewTransform.qml
        src/Gui/Network/NodeDropController.qml
        src/Gui/Network/LinkCutController.qml
        src/Gui/Network/LinkRedirectController.qml
        src/Gui/Network/NetworkMouse.qml
        src/Gui/Network/NetworkKeys.qml
        src/Gui/Network/NodeLinkController.qml
        src/Gui/Network/SelectionBox.qml
        src/Gui/Network/TabMenu.qml
        src/Gui/Spreadsheet/Spreadsheet.qml
        src/Gui/Spreadsheet/SpreadsheetHeader.qml
        src/Gui/Spreadsheet/AttributeTable.qml
        src/Gui/Spreadsheet/PrimitivesTree.qml
        src/Gui/Spreadsheet/ModeControl.qml
        src/Gui/Timeline/Timeline.qml
        src/Gui/Timeline/TimelineRuler.qml
        src/Gui/Settings/SettingsWindow.qml
        src/Gui/Settings/SettingsPlaceholder.qml
        src/Gui/Settings/ThemePage.qml
        src/Gui/Settings/ThemeSelector.qml
        src/Gui/Settings/ThemeSwatch.qml
        src/Gui/Settings/PaletteRow.qml
        src/Gui/Settings/NumberTrack.qml
        src/Gui/Settings/ColorPicker.qml
        src/Gui/Settings/ColorCompare.qml
        src/Gui/Settings/SwitchThemePrompt.qml
        src/Gui/Settings/KeymapPage.qml
        src/Gui/Viewport/Viewport.qml
        src/Gui/Viewport/ViewportHud.qml
        src/Gui/Viewport/ViewportMouse.qml
    RESOURCES
        # Declares Constants.qml a singleton for the directory import.
        src/Gui/Style/qmldir
        # Declares ParameterClipboard.qml a singleton for the directory import.
        src/Gui/Components/qmldir
)

qt6_add_shaders(${AppExec} "helixShaders"
    PREFIX
    "/"
    BASE
    "src/Gui/Shaders"
    FILES
    "src/Gui/Shaders/NetworkDots.frag"
)

# The themes the program ships with. They are compiled in so no missing or
# unreadable file can leave the application unstyled.
qt_add_resources(${AppExec} "helixTheme"
    PREFIX
    "/"
    BASE
    "static"
    FILES
    "static/theme/default.yml"
    "static/theme/dark.yml"
    "static/theme/grey.yml"
    "static/theme/tokyo-night.yml"
    "static/theme/catppuccin-mocha.yml"
)

# A run from a build directory loads QML from here, so edits reload without a
# rebuild. An installed run loads the module compiled into the binary instead.
target_compile_definitions(${AppExec} PRIVATE
    HELIX_QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/Gui"
)

target_compile_definitions(${AppExec} PRIVATE HELIX_VERSION="${PROJECT_VERSION}")

# A macOS build produces a real application bundle, which is what the system
# expects to find when the application is launched from the desktop or from a
# bundle aware installer.
#
# The values of the bundle description are filled in here rather than left to
# the bundle target, which leaves the placeholders of a description it has
# already read empty.
set(MACOSX_BUNDLE_EXECUTABLE_NAME ${AppExec})
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.helix3d.Helix")
set(MACOSX_BUNDLE_BUNDLE_NAME "Helix")
set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    "${CMAKE_BINARY_DIR}/Info.plist"
    @ONLY
)
set_target_properties(${AppExec} PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.helix3d.Helix"
    MACOSX_BUNDLE_BUNDLE_NAME "Helix"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(${AppExec} PRIVATE helixEngine helixGraphics argparse::argparse Qt6::Quick Qt6::QuickControls2)
if(HELIX_HAS_METAL_COMPILER)
    add_dependencies(${AppExec} helixMetalLib)
endif()
target_include_directories(${AppExec} PRIVATE
    ${ENGINE_INCLUDE_DIRECTORIES}
    # Folders holding QML_ELEMENT types, whose headers the generated type
    # registration includes by bare file name.
    src/Gui/Style
    src/Gui/Components
    src/Gui/Network
    src/Gui/Parameters
    src/Gui/Viewport
)


# A macOS install is a bundle, so its content sits inside the bundle.
set(HELIX_APP_DIR "${CMAKE_PROJECT_NAME}.app/Contents/")

# The layout every install rule writes into.
set(HELIX_LIB_DIR ${HELIX_APP_DIR}lib CACHE STRING "Library install directory")
set(HELIX_NODES_DIR ${HELIX_APP_DIR}nodes CACHE STRING "Node folder install directory")

# A bundle carries its own executable, so the binary directory is the one the
# bundle keeps it in and the bundle itself lands at the root of the install.
set(HELIX_BIN_DIR "${CMAKE_PROJECT_NAME}.app/Contents/MacOS")
set(HELIX_BUNDLE_DIR "." CACHE STRING "Folder the application bundle is installed into")

# compile nodes
add_subdirectory(nodes)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake)




# tests
option(HELIX_BUILD_TESTS "Build tests and benchmarks" ON)
if(HELIX_BUILD_TESTS)
    Include(FetchContent)

    FetchContent_Declare(
      Catch2
      URL      ${HELIX_CATCH2_URL}
      URL_HASH SHA256=${HELIX_CATCH2_SHA256}
    )

    FetchContent_MakeAvailable(Catch2)

    add_executable(${TestExec}
        tests/main-tests.cpp
        tests/ExpressionContextTests.cpp
        tests/ExpressionEngineTests.cpp
        tests/PointScriptTests.cpp
        tests/MeshAttributeReaderTests.cpp
        tests/ScriptContextTests.cpp
        tests/GeometryFunctionsTests.cpp
        tests/ScriptNodeTests.cpp
        tests/ScriptEngineTests.cpp
        tests/CompiledProgramTests.cpp
        tests/ParameterTests.cpp
        tests/NetworkGraphTests.cpp
        tests/RampTests.cpp
        tests/SerializerTests.cpp
        tests/UndoTests.cpp
        tests/AttributeTests.cpp
        tests/NetworkTests.cpp
        tests/TimeTests.cpp
        tests/SelectionTests.cpp
        tests/MeshTests.cpp
        tests/BooleanUtilsTests.cpp
        tests/PrimitiveTests.cpp
        tests/AttributeTransferTests.cpp
        tests/PlaneCutTests.cpp
        tests/CellFractureNodeTests.cpp
        tests/PathTests.cpp
        tests/NetworkPathTests.cpp
        tests/NodeManifestTests.cpp
        tests/NodeTypeTests.cpp
        tests/NodeLoaderTests.cpp
        tests/GeometryImportTests.cpp
        tests/ModelRatNodeTests.cpp
        tests/TransformNodeTests.cpp
        tests/AttributeCreateNodeTests.cpp
        tests/CylinderNodeTests.cpp
        tests/SphereNodeTests.cpp
        tests/TorusNodeTests.cpp
        tests/LineNodeTests.cpp
        tests/SpiralNodeTests.cpp
        tests/DuplicateNodeTests.cpp
        tests/CopyToPointsNodeTests.cpp
        tests/TransformTests.cpp
        tests/ParameterItemTests.cpp
        tests/StyleTests.cpp
        tests/AttributeNoiseNodeTests.cpp
        tests/MountainNodeTests.cpp
        tests/GraphicsDeviceTests.cpp
        tests/ViewportCameraTests.cpp
        tests/DisplayGeometryTests.cpp
        tests/ViewportRendererTests.cpp
        tests/AsyncCookTests.cpp
        tests/CookPlanTests.cpp
        tests/CopyOnWriteTests.cpp
        tests/SimulationTests.cpp
        src/Gui/Parameters/ParameterItem.cpp
    )
    set_target_properties(${TestExec} PROPERTIES AUTOMOC ON)
    target_link_libraries(${TestExec} PRIVATE helixEngine helixGraphics Qt6::Core Catch2::Catch2WithMain)
    target_include_directories(${TestExec} PRIVATE src)
    if(HELIX_HAS_METAL_COMPILER)
        add_dependencies(${TestExec} helixMetalLib)
    endif()

    # Stage the test binary next to daslib so daslang resolves its standard
    # library the same way the app does.
    set_target_properties(${TestExec} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HELIX_DEV_STAGE_DIR}/bin"
    )

    # benchmarks
    add_executable(${BenchExec}
        tests/Benchmarks.cpp
    )
    target_link_libraries(${BenchExec} PRIVATE helixEngine Catch2::Catch2WithMain)

    # GUI unit tests, Qt and yaml-cpp only, no engine
    add_executable(helixGuiTests
        tests/gui/ThemeLoaderTests.cpp
        src/Gui/Style/ThemeLoader.cpp
    )
    target_link_libraries(helixGuiTests PRIVATE Qt6::Gui yaml-cpp::yaml-cpp Catch2::Catch2WithMain)
    target_include_directories(helixGuiTests PRIVATE src)
    target_compile_definitions(helixGuiTests PRIVATE
        HELIX_DEV_STATIC_DIR="${CMAKE_SOURCE_DIR}/static"
    )
endif()

