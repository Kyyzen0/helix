# Building Helix on macOS

Helix builds for macOS on Apple silicon (arm64) only, with the system Apple
clang. There is no Intel, Windows or Linux build. This page collects what a
macOS host needs on top of the ordinary build instructions in the
[README](README.md).

## What a macOS build needs

| Requirement | Notes |
| --- | --- |
| Apple silicon Mac, macOS 13.3 or newer | The deployment target of the whole tree. The bootstrap script and the CMake configure step refuse any other host or platform. |
| Xcode Command Line Tools | `xcode-select --install`. Provides the Apple clang the build uses. |
| Python 3 | The superbuild creates a virtual environment for aqtinstall with it. |
| Git | Fetched by CMake, not by the superbuild. |
| Metal compiler | Part of Xcode. Xcode 26 and later install it apart, with `xcodebuild -downloadComponent MetalToolchain`. The configure step stops with that command when it is missing. |
| Network access | For the dependency archives, the Qt download and the `metal-cpp` archive from developer.apple.com. |

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
| CMake | 3.31.6 | The version the CI pins. |
| Ninja | 1.12.1 | The generator the CI uses. |
| aqtinstall | 3.3.0 | Downloads the pinned Qt. |
| bison, flex | 3.8.2, 2.6.4 | daslang generates parsers with them. |
| m4 | 1.4.21 | bison expands its macros with it. |

`toolchain/env.sh` puts CMake, Ninja, bison and Qt on the search paths.

## Building

```sh
source toolchain/env.sh
cmake -S . -B build-super -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHELIX_SUPERBUILD=ON
cmake --build build-super
```

The superbuild builds every pinned dependency into `build-super/deps` and
then builds Helix itself into `build-super/helix-build`.

## Installing and running

```sh
cmake --install build-super/helix-build --prefix "$PWD/stage"
open stage/helix.app
```

The install produces a normal macOS application bundle:

```
stage/helix.app/Contents/
├── Info.plist
├── MacOS/helix              the executable
├── lib/                    helixEngine, the node library and the boost libraries
├── Frameworks/             the Qt frameworks
├── PlugIns/                the Qt plugins
├── Resources/qml/          the deployed QML modules
├── daslib/                 the daslang standard library
├── nodes/                  the node folders
└── share/                  the icons and other static files
```

A run from the build tree works as well, without an install step:

```sh
build-super/helix-build/helix.app/Contents/MacOS/helix
```

## The parser tools

daslang regenerates its parsers from a grammar during the build, with bison
and flex. macOS ships bison 2.3, which generates a parser daslang cannot
compile, and the build looks for a newer one. The committed parsers were
produced by bison 3.8.2, and the build that was verified here used bison
3.8.2 for all three grammars of daslang (`src/parser/ds_parser.ypp`,
`src/parser/ds2_parser.ypp` and `utils/dasFormatter/ds_parser.ypp`) and flex
2.6.4 for the two lexers.

The toolchain therefore carries bison, flex and m4, all from the conda-forge
channel, and the build runs them through a wrapper in
`toolchain/bison/shim/bison`. The wrapper:

- runs bison without the C locale, which the conda build of bison does not
  survive. CMake asks a tool for its version in the C locale, so without this
  the build cannot even find bison.
- points bison at `BISON_PKGDATADIR`, when the data files of bison are in the
  toolchain, because the conda build has the folder of the machine that built
  it baked in, and at the `M4` of the toolchain. The macro processor of macOS
  is too old for bison and writes its temporary file into a fixed folder,
  which a restricted host may not allow it to remove.

`toolchain/env.sh` exports `BISON_EXECUTABLE` and `FLEX_EXECUTABLE` so that
CMake finds the wrapper, and the superbuild passes both on to the build of
Helix itself. It also puts `toolchain/bison/bin` on the `PATH`, so the bison
of the toolchain comes before the one of the system.

A bison that was built from the sources into `toolchain/bison` works as well:
the bootstrap keeps a bison that is already in that folder instead of
replacing it with the package, and the wrapper only points bison at the data
files of the toolchain when they are there.

The superbuild checks the version of bison while it configures, and stops
with a message that names the toolchain, when the bison it would find is
older than 3.8. Without that check the build goes on and regenerates the
parsers of daslang with the bison of the system, which fails much later and
far from the cause.

## The noise the tools of the system print

Every tool of Apple that goes through `xcrun` prints this on the error
stream, before it does its work:

```
error: couldn't rename cache file '/var/folders/.../T/xcrun_db-XXXX' to '/var/folders/.../T/xcrun_db' (errno=Operation not permitted)
```

It is noise. The tool still runs and still ends with the exit code it should
have, and the build is not affected by it. The cache of `xcrun` is a file it
renames, and a host that refuses the rename makes it complain. Moving the
temporary folder or setting `XCRUN_CACHE_PATH` does not silence it.

The build prints the same complaint when `strip` and `install_name_tool` run,
since they go through the same wrapper. A check of the build that reads the
error stream of a tool has to know about it, or it will read the noise for a
failure.

## The Metal renderer

The viewport is drawn with Metal directly, through Apple's `metal-cpp` headers. The
design, and what to check on a Mac, is in
[docs/METAL-RENDERER.md](docs/METAL-RENDERER.md). Three things matter to a build:

- **The shaders are compiled at build time.** `src/Graphics/Shaders/*.metal` become
  `helix.metallib` in `Contents/Resources` of the bundle, with `xcrun metal` and
  `xcrun metallib`. A configure step without the Metal compiler stops and prints the
  command that installs it. `-DHELIX_ALLOW_RUNTIME_SHADERS=ON` configures without it, for a
  development build that compiles the shader sources when it starts.
- **`metal-cpp` is pinned.** `cmake/versions.cmake` holds its url and SHA-256. Give
  `-DHELIX_METAL_CPP_DIR=<folder with Metal/Metal.hpp>` to build without the download.
- **A machine without a Metal device cannot show the viewport.** A virtualised runner may
  have none. The application then prints `No Metal device available` and exits with a
  non-zero status, before it builds any part of the interface. The smoke test of the CI
  accepts that message and no other early exit, since the libraries of the bundle are loaded
  before the check and it still proves the bundle is whole. The tests skip the renderer
  tests on such a machine.

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
application, which include the compiled shaders, are bound to the signature of its
executable.

A signature that cannot be made is reported, but it does not stop the install.
The bundle is still worth having without one, and a host that forbids the
operation is not the host the bundle is built for.

An ad hoc signature is enough for the bundle to run on the machine that built
it. A bundle that travels to another machine needs a signature of its own,
which is described under the known limitations below.

## Known limitations

- **The Qt side of the viewport has not been run.** The renderer is tested against a real
  Metal device, but the code that hands its texture to the Qt Quick scene graph was written
  from the Qt documentation, without Qt installed on the machine it was written on. The
  checklist in [docs/METAL-RENDERER.md](docs/METAL-RENDERER.md) says what to confirm.
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
- **Only the tarball package is built.** The CI job builds a tarball of the
  bundle, and no disk image or installer.
- **The Qt download is universal.** The Qt Company publishes the macOS desktop
  build of Qt as one package holding both an arm64 and an x86_64 slice, and
  no arm64-only package. Helix and every dependency it builds are arm64 only
  (`CMAKE_OSX_ARCHITECTURES=arm64`), so the x86_64 slice of Qt is carried
  unused.
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
- **Here documents fail.** A shell that writes a here document has to create a
  temporary file for it, and a host that refuses that write stops the shell
  with `cannot create temp file for here document: Operation not permitted`.
  Every `configure` script of autoconf uses here documents inside, so a
  dependency that is configured that way fails on such a host. `zsh` manages
  the same script, and the toolchain of the repository builds its own
  dependencies through CMake, which does not need a here document.
- **The removal of a file can be refused.** A host that routes `rm` through a
  helper refuses it for many paths, and autoconf cleans its `conftest.*`
  files that way. The failure is usually harmless, but when the file it
  removed was the one the compiler check wrote, the script reports `C
  compiler cannot create executables`, which has nothing to do with the
  compiler.

Because of the last two points, a dependency that offers a CMake path is
built through it rather than through autoconf. Every dependency of this
project has such a path, and the build on a normal macOS machine is the same
either way.
