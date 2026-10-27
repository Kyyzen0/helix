# Pins every dependency helix builds from source, one version, url and hash each.

# Qt is downloaded as an official prebuilt, so it carries a version but no
# archive.
set(HELIX_QT_VERSION 6.10.3)
set(HELIX_AQTINSTALL_VERSION 3.3.0)

set(HELIX_TBB_VERSION 2022.3.0)
set(HELIX_TBB_URL
    https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v${HELIX_TBB_VERSION}.tar.gz)
set(HELIX_TBB_SHA256
    01598a46c1162c27253a0de0236f520fd8ee8166e9ebb84a4243574f88e6e50a)

# The cmake archive, which carries every library's sources already checked out.
# The plain boost archive expects the b2 bootstrap instead.
set(HELIX_BOOST_VERSION 1.88.0)
set(HELIX_BOOST_URL
    https://github.com/boostorg/boost/releases/download/boost-${HELIX_BOOST_VERSION}/boost-${HELIX_BOOST_VERSION}-cmake.tar.xz)
set(HELIX_BOOST_SHA256
    f48b48390380cfb94a629872346e3a81370dc498896f16019ade727ab72eb1ec)

set(HELIX_EIGEN_VERSION 3.4.0)
set(HELIX_EIGEN_URL
    https://gitlab.com/libeigen/eigen/-/archive/${HELIX_EIGEN_VERSION}/eigen-${HELIX_EIGEN_VERSION}.tar.gz)
set(HELIX_EIGEN_SHA256
    8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72)

set(HELIX_GLM_VERSION 1.0.3)
set(HELIX_GLM_URL
    https://github.com/g-truc/glm/archive/refs/tags/${HELIX_GLM_VERSION}.tar.gz)
set(HELIX_GLM_SHA256
    6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001)

set(HELIX_ARGPARSE_VERSION 3.2)
set(HELIX_ARGPARSE_URL
    https://github.com/p-ranav/argparse/archive/refs/tags/v${HELIX_ARGPARSE_VERSION}.tar.gz)
set(HELIX_ARGPARSE_SHA256
    9dcb3d8ce0a41b2a48ac8baa54b51a9f1b6a2c52dd374e28cc713bab0568ec98)

set(HELIX_MANIFOLD_VERSION 3.5.2)
set(HELIX_MANIFOLD_URL
    https://github.com/elalish/manifold/archive/refs/tags/v${HELIX_MANIFOLD_VERSION}.tar.gz)
set(HELIX_MANIFOLD_SHA256
    35cb5e0d78882f461ec39b17d8f09c2aceca761356f3ce948e3f3908289b8f2e)

set(HELIX_FASTNOISE2_VERSION 1.1.1)
set(HELIX_FASTNOISE2_URL
    https://github.com/Auburn/FastNoise2/archive/refs/tags/v${HELIX_FASTNOISE2_VERSION}.tar.gz)
set(HELIX_FASTNOISE2_SHA256
    e460592c32e9b1a2cf6e6f6aea5e16c9fe23c68bd185adb315202a5a993656b9)

# Apple's metal-cpp, the header only C++ interface to Metal the viewport renderer draws
# with. Apple publishes it as a zip on its developer site, which carries the headers of
# the macOS release in its name (26 is macOS 26 and iOS 26) and a single metal-cpp folder.
# The hash was computed on the file as downloaded from that url, twice.
set(HELIX_METAL_CPP_VERSION 26)
set(HELIX_METAL_CPP_URL
    https://developer.apple.com/metal/cpp/files/metal-cpp_${HELIX_METAL_CPP_VERSION}.zip)
set(HELIX_METAL_CPP_SHA256
    4df3c078b9aadcb516212e9cb03004cbc5ce9a3e9c068fa3144d021db585a3a4)

set(HELIX_YAML_CPP_VERSION 0.8.0)
set(HELIX_YAML_CPP_URL
    https://github.com/jbeder/yaml-cpp/archive/refs/tags/${HELIX_YAML_CPP_VERSION}.tar.gz)
set(HELIX_YAML_CPP_SHA256
    fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16)

set(HELIX_CATCH2_VERSION 3.8.1)
set(HELIX_CATCH2_URL
    https://github.com/catchorg/Catch2/archive/refs/tags/v${HELIX_CATCH2_VERSION}.tar.gz)
set(HELIX_CATCH2_SHA256
    18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79)

# daslang, cereal and icecream-cpp are vendored under extern, so their versions
# are the submodule commits rather than entries here.
