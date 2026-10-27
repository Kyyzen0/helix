<div align="center">

# Helix

**A 3D software for procedural modeling and animation, on macOS.**

<img src="https://github.com/user-attachments/assets/7bbae453-31f6-4a3b-a39b-c0efb4488834" width="380" alt="Helix">

</div>

---

## Credits and origin

**Helix is a fork of [enzo](https://github.com/parkerbritt/enzo), by [ParkerBritt](https://github.com/ParkerBritt).**

Helix is an open source 3D software for procedural modeling in VFX or animation, inspired by
industry standard tools like Houdini.

Helix is not a rewrite. It is a continuation: the same MIT licensed codebase, carried to
macOS and extended. All of the original work — the decoupled engine and UI, the attribute
based data model, the powerful node graph, the parameter system — is ParkerBritt's, and
the original project remains the reference for anything Helix has not touched.

- Upstream project: <https://github.com/parkerbritt/enzo>
- Upstream documentation: <https://parkerbritt.github.io/enzo/>
- Upstream website: <https://enzo3d.com>

Both projects are MIT licensed. See [LICENSE](LICENSE) for the terms, and
[NOTICE](NOTICE) for the attribution notice.

---

## What Helix adds

| | |
| --- | --- |
| **macOS arm64** | A native Apple silicon build, with a real `.app` bundle, a tarball package, a CI job on `macos-15`, and a toolchain script that needs no package manager. |
| **Async cooking** | The graph cooks on a worker pool instead of the UI thread. Edits cancel the cook in flight and coalesce, so the viewport keeps showing the last finished result while a new one is computed. |
| **Sweep node** | The sweep node is finished and covered by tests, so a profile can be swept along a path. |
| **Metal viewport** | The viewport is drawn with Metal directly, through Apple's `metal-cpp`, and shown inside the Qt Quick interface through its Metal scene graph. No translation layer sits between the renderer and the GPU. |
| **Timeline** | Playback, scrubbing, looping and a frame range, driving frame dependent evaluation through the engine's existing time mechanism. |
| **Simulation nodes** | A family of solver nodes — particles, springs, soft bodies — that integrate over time and can be keyframed from the timeline. |
| **User node authoring** | Users can define their own node types at runtime from script or expression source, reuse the existing daslang and expression machinery, and save them with the project. |
| **GUI rewrite** | The QML interface is redesigned around one spacing and type scale, a unified panel system, a node editor that reads like a professional tool, and a status bar that shows cook state and the current frame. |

The engine stays decoupled from the interface, as in the original: it can be embedded in
another project, driven headlessly, or given a different interface entirely.

---

## Building

### macOS (Apple silicon)

Helix builds for macOS on Apple silicon (arm64) only. There is no Intel, Windows or Linux
build, and the build stops at configure time on any other platform.

The build needs no package manager. The bootstrap script downloads a pinned cmake, ninja,
aqtinstall, bison and flex into `toolchain/`, then the superbuild fetches and
builds every other dependency at its pinned version.

```sh
git clone --recurse-submodules https://github.com/Kyyzen0/helix
cd helix
./scripts/bootstrap-macos.sh
source toolchain/env.sh

cmake -S . -B build-super -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHELIX_SUPERBUILD=ON
cmake --build build-super
```

The result is `build-super/helix.app`.

Two things are specific to macOS and are worth knowing about:

- **The viewport shaders are compiled by the Metal compiler of Xcode.** The build turns the
  `.metal` files in `src/Graphics/Shaders` into `helix.metallib`. Xcode 26 and later install
  that compiler as a separate component; if the configure step says it is missing, run
  `xcodebuild -downloadComponent MetalToolchain`. The Metal headers for C++ (`metal-cpp`)
  are downloaded at the pinned version and checked against their hash.
- **bison has to be new enough.** daslang regenerates its parser from a bison grammar
  during the build, and the committed parser was produced by bison 3.8.2. macOS ships
  bison 2.3, which generates a parser that does not compile. The bootstrap script
  installs bison 3.8.2 for exactly this reason.

---

## Status

Helix is a work in progress, and so is the application it is based on. The macOS port
builds and installs, the CI job runs the superbuild on a `macos-15` runner, and the
features listed above are implemented in the tree. Expect rough edges, and expect the
interface to still be moving.

The 3D view draws with Metal into a texture that the Qt Quick scene graph samples. It was
written without a Mac that could run Qt, so the Qt side of it has not been run yet:
[docs/METAL-RENDERER.md](docs/METAL-RENDERER.md) lists what to check on a real machine.
[build-macos.md](build-macos.md) says what else the port still needs.

Scenes are saved with the `.kut` extension. The content format is the one of the upstream
project. A scene saved by enzo, including its `enzo::<name>` node type names, opens directly in
Helix once its extension is renamed to `.kut` (the loader does not check the extension, so the
"All files" choice of the open dialog also opens it unrenamed). Helix saves `helix::<name>`
type names, which enzo does not know, so the reverse direction is not supported.

**[ROADMAP.md](ROADMAP.md)** states where the work goes next, and why in that order.
Helix is a procedural modeling tool *and* an animation tool, so the roadmap has a
shared foundation and two tracks that build on it.

---

## License

MIT, inherited from the original project. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Helix is free and open source. No expensive licensing, and no getting locked out of your
own work.
