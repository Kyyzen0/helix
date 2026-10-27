cmake_minimum_required(VERSION 3.26)

project(helixOps)

find_package(Boost REQUIRED)
# eigen (math)
find_package (Eigen3 REQUIRED NO_MODULE)

# Every builtin node compiles into one library, so startup performs a single
# library load no matter how many nodes ship.
file(GLOB NODE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/src/*.cpp")

set(libName helixOps)
add_library(${libName} SHARED ${NODE_SOURCES})

target_link_libraries(${libName} PRIVATE helixEngine Boost::filesystem Boost::system glm::glm Eigen3::Eigen TBB::tbb libDaScript FastNoise2::FastNoise)

# packaging
install(TARGETS ${libName}
    RUNTIME_DEPENDENCY_SET helixRuntime
    RUNTIME DESTINATION ${HELIX_LIB_DIR}
    LIBRARY DESTINATION ${HELIX_LIB_DIR}
    ARCHIVE DESTINATION ${HELIX_LIB_DIR}
)

# The node folders ship alongside the binaries, with the sources stripped out
# since the library already carries them.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${HELIX_NODES_DIR}
    PATTERN "src" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
)

target_include_directories(${libName} PUBLIC
    ../src
    ../extern/icecream-cpp/include
    ${DASLANG_DIR}/include
)
