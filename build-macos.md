# Building Helix on macOS

Helix builds on macOS arm64 with the system Apple clang. The upstream project
builds on Rocky Linux and Windows, so this page collects what a macOS host
needs on top of the ordinary build instructions in the [README](../README.md).

## What a macOS build needs

| Requirement | Notes |
| --- | --- |
| macOS 13.3 or newer | The deployment target of the whole tree. |
| Xcode Command Line Tools | `xcode-select --install`. Provides the Apple clang the build uses. |
| Python 3 | The superbuild creates a virtual environment for aqtinstall with it. |
| Git | Fetched by CMake, not by the superbuild. |
| Network access | For the dependency archives, the Qt download and the DiligentCore clone. |

Homebrew is not needed and is deliberately not used. Every tool the build
requires is downloaded into `toolchain/` by
[`scripts/bootstrap-macos.sh`](scripts/bootstrap-macos.sh), so the build has
no dependency on a package manager.

## Bootstrapping the toolchain

```sh
./scripts/bootstrap-macos.sh
source toolchain/env.sh
```

The script installs:

| Tool | Version | Why |
| --- | --- | --- |
| CMake | 3.31.6 | The version the other builds use. |
| Ninja | 1.12.1 | The generator the other builds use. |
| aqtinstall | 3.3.0 | Downloads the pinned Qt. |
| MoltenVK | 1.4.2 | The only Vulkan driver macOS has. |
| Vulkan loader | 1.4.363 | The loader the Diligent engine asks for the portability extension. |
| bison, flex | 3.8.2, 2.6.4 | daslang generates parsers with them. |
| m4 | 1.4.21 | bison expands its macros with it. |

`toolchain/env.sh` puts CMake and Ninja on `PATH` and points the Vulkan
loader at the MoltenVK driver.

## Building

```sh
source toolchain/env.sh
cmake -S . -B build-super -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENZO_SUPERBUILD=ON
cmake --build build-super
```

The superbuild builds every pinned dependency into `build-super/deps` and
then builds Helix itself into `build-super/enzo-build`.

## Installing and running

```sh
cmake --install build-super/enzo-build --prefix "$PWD/stage"
open stage/helix.app
```

The install produces a normal macOS application bundle:

```
stage/helix.app/Contents/
├── Info.plist
├── MacOS/helix              the executable
├── lib/                    enzoEngine, the node library and the boost libraries
├── Frameworks/             the Qt frameworks
├── PlugIns/                the Qt plugins
├── Resources/qml/          the deployed QML modules
├── daslib/                 the daslang standard library
├── nodes/                  the node folders
└── share/                  the icons and other static files
```

A run from the build tree works as well, without an install step:

```sh
build-super/enzo-build/helix.app/Contents/MacOS/helix
```

## The parser tools

daslang regenerates its parsers from a grammar during the build, with bison
and flex. macOS ships bison 2.3, which generates a parser daslang cannot
compile, and the build looks for a newer one.

The toolchain therefore carries bison, flex and m4, all from the conda-forge
channel, and the build runs them through a wrapper in
`toolchain/bison/shim/bison`. The wrapper:

- runs bison without the C locale, which the conda build of bison does not
  survive. CMake asks a tool for its version in the C locale, so without this
  the build cannot even find bison.
- points bison at `BISON_PKGDATADIR`, because the conda build has the folder
  of the machine that built it baked in, and at the `M4` of the toolchain.
  The macro processor of macOS is too old for bison and writes its temporary
  file into a fixed folder, which a restricted host may not allow it to
  remove.

`toolchain/env.sh` exports `BISON_EXECUTABLE` and `FLEX_EXECUTABLE` so that
CMake finds the wrapper, and the superbuild passes both on to the build of
Helix itself.

## The Vulkan driver

macOS has no Vulkan driver of its own. MoltenVK translates Vulkan onto Metal
and stands in for a driver, and the toolchain downloads it.

Two things are worth knowing about how Helix reaches it:

- **The Vulkan loader is needed as well.** The Diligent engine asks the loader
  for the portability enumeration extension on macOS, which the driver of
  MoltenVK does not offer, and refuses to create a device without it. The
  loader has the extension, so the toolchain carries it too and puts it ahead
  of MoltenVK on `DYLD_LIBRARY_PATH`. The loader then finds MoltenVK through
  its manifest. The loader of the toolchain is built for macOS 15 and newer.
- **The driver is found through the environment.** `toolchain/env.sh` points
  `VK_ICD_FILENAMES` at the manifest of MoltenVK. A run that does not source
  that file cannot reach a Vulkan device.

The viewport needs a Metal capable device. A machine without one, such as a
virtualised runner, cannot create a Vulkan device and the application will
report that it has no adapter. Such a host still shows that the bundle is
whole: the application loads every library of the bundle and creates a Vulkan
instance before it asks for a device.

## The signature of the bundle

The Qt deployment rewrites the frameworks and the plugins it copies, so that
they find each other inside the bundle rather than in the folder Qt was
installed in. Rewriting a file breaks the signature it carries, and macOS
refuses to load a library whose signature no longer matches. The application
is killed before it opens a window, with a signature error from the system and
no message of its own.

The install therefore signs the bundle again, with an ad hoc signature, in
[`cmake/SignBundle.cmake`](cmake/SignBundle.cmake). Every library in it is
signed on its own, and the bundle itself last, so that the resources of the
application are bound to the signature of its executable.
[`scripts/install-vulkan-loader.sh`](scripts/install-vulkan-loader.sh) signs it
once more, since the loader it copies into the bundle is not part of the
signature the install made.

A signature that cannot be made is reported, but it does not stop the install.
The bundle is still worth having without one, and a host that forbids the
operation is not the host the bundle is built for.

An ad hoc signature is enough for the bundle to run on the machine that built
it. A bundle that travels to another machine needs a signature of its own,
which is described under the known limitations below.

## Known limitations

- **The viewport stays empty.** The scene graph of Qt draws with the Vulkan
  device of the graphics engine, which needs the Vulkan module of Qt. The
  official binaries of Qt for macOS do not carry that module, and their scene
  graph runs on Metal rather than on Vulkan, so the instance of the device
  cannot be handed to them. The application builds and runs on macOS, with
  every other part of it working, but the 3D view is not presented yet. The
  part of the port that remains is a way of showing the image of the device on
  that platform, such as a copy to the host that the scene graph can take.
- **The bundle is not signed or notarised.** The install gives the bundle an ad
  hoc signature, which is enough for it to run on the machine that built it.
  macOS refuses to open an unsigned application that was downloaded, so a
  bundle that travels to another machine needs a Developer ID signature and a
  notarisation before it opens.
- **The deployment target is macOS 13.3.** The whole tree is built for it, so
  the bundle claims the same in its `Info.plist`. The floor is not a choice:
  the Qt build needs 13.0, and the standard library of the current Xcode pulls
  its formatting header in through the stream header, and the conversion of a
  floating point number that header uses was only added to the system in 13.3.
- **The Vulkan loader of the toolchain needs macOS 15.** The loader is not
  part of the bundle but a component of the host, the way a driver is, so the
  bundle itself still claims macOS 13.3.
- **Only the tarball package is built.** The Linux job builds RPM and DEB
  packages and the Windows job builds an installer. The macOS job builds a
  tarball of the bundle.
- **The Qt deployment follows the bundle layout.** The frameworks land in
  `Contents/Frameworks` and the QML modules in `Contents/Resources/qml`,
  which is where macdeployqt puts them, while Helix's own libraries stay in
  `Contents/lib` where the runtime looks for them.

## Notes on a sandboxed host

A sandbox that restricts what a build may write needs a little care. The
points that came up while porting the build:

- **The build tree needs a folder that allows removals and renames.** CMake
  removes its own scratch files while it checks a compiler, and it renames the
  cache file it writes. A build tree in a folder that refuses removals fails
  at the first compiler check. A symbolic link to a folder elsewhere works.
- **The temporary folder needs to allow removals as well.** The macro
  processor of the toolchain writes a temporary file and removes it again, and
  a folder that refuses removals makes the parser generation fail. `TMPDIR`
  has to point at a folder where that works.
- **`TMPDIR` has to stay short.** aqtinstall hands work between its
  processes over a socket whose path is built from the temporary folder, and
  a long folder makes that path too long for the system to accept. The
  superbuild falls back to `/tmp` when the inherited folder is long.
- **daslang writes its libraries into its own source tree.** The archive
  tools remove the previous library before they write the new one, so a build
  that keeps them there cannot replace a library from an earlier build. The
  top level `CMakeLists.txt` points them at the build tree instead.
- **Some harnesses protect a few file names.** `.gitmodules` and the contents of
  `.vscode` cannot be created in a sandbox that treats them as configuration. A
  few dependency archives carry such a file, and the usual extraction then
  fails on the first of them. The source of the dependencies is therefore
  prepared ahead of time, with
  [`scripts/prepare-sources.sh`](scripts/prepare-sources.sh), which leaves
  those names out. None of them takes part in the build, so the difference is
  only in the metadata that never reaches the tree. Git itself is not
  affected: it reads the submodule list from its index, so
  `git submodule update --init` still works without the file on disk.
