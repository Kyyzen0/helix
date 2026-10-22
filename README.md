<div align="center">

# Helix

**A 3D software for procedural modeling and animation, on macOS.**

<img src="https://github.com/user-attachments/assets/7bbae453-31f6-4a3b-a39b-c0efb4488834" width="380" alt="Helix">

</div>

---

## Credits and origin

**Helix is a fork of [enzo](https://github.com/parkerbritt/enzo), by [ParkerBritt](https://github.com/ParkerBritt).**

Enzo is an open source 3D software for procedural modeling in VFX or animation, inspired by
industry standard tools like Houdini. Its engine architecture, node system, data model and
user interface were designed and written by ParkerBritt, who develops it in his free time.

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
| **macOS arm64** | A native Apple silicon build, with a real `.app` bundle, a DMG installer, a CI job on `macos-15`, and a toolchain script that needs no package manager. |
| **Async cooking** | The graph cooks on a worker pool instead of the UI thread. Edits cancel the cook in flight and coalesce, so the viewport keeps showing the last finished result while a new one is computed. |
| **Sweep node** | The sweep node is finished and covered by tests, so a profile can be swept along a path. |
| **Vulkan viewport** | The viewport runs on DiligentCore's Vulkan backend, tuned for Apple silicon, where Vulkan is provided by MoltenVK translating onto Metal. |
| **Timeline** | Playback, scrubbing, looping and a frame range, driving frame dependent evaluation through the engine's existing time mechanism. |
| **Simulation nodes** | A family of solver nodes — particles, springs, soft bodies — that integrate over time and can be keyframed from the timeline. |
| **User node authoring** | Users can define their own node types at runtime from script or expression source, reuse the existing daslang and expression machinery, and save them with the project. |
| **GUI rewrite** | The QML interface is redesigned around one spacing and type scale, a unified panel system, a node editor that reads like a professional tool, and a status bar that shows cook state and the current frame. |

The engine stays decoupled from the interface, as in the original: it can be embedded in
another project, driven headlessly, or given a different interface entirely.

---

## Building

### macOS (Apple silicon)

The build needs no package manager. The bootstrap script downloads a pinned cmake, ninja,
aqtinstall, MoltenVK, bison and flex into `toolchain/`, then the superbuild fetches and
builds every other dependency at its pinned version.

```sh
git clone --recurse-submodules https://github.com/Kyyzen0/helix
cd helix
./scripts/bootstrap-macos.sh
source toolchain/env.sh

cmake -S . -B build-super -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENZO_SUPERBUILD=ON
cmake --build build-super
```

The result is `build-super/helix.app`.

Two things are specific to macOS and are worth knowing about:

- **Vulkan comes from MoltenVK.** macOS has no native Vulkan driver. MoltenVK translates
  Vulkan onto Metal, and the viewport uses it through DiligentCore. The script installs it
  and `toolchain/env.sh` points the loader at it.
- **bison has to be new enough.** daslang regenerates its parser from a bison grammar
  during the build, and the committed parser was produced by bison 3.8.2. macOS ships
  bison 2.3, which generates a parser that does not compile. The bootstrap script
  installs bison 3.8.2 for exactly this reason.

### Linux

The dependencies are the same as the upstream list, plus bison and flex for daslang:

```
cmake ninja-build gcc-c++ boost-devel eigen3-devel tbb-devel cgal-devel
polyclipping2-devel glm-devel qt6-qtbase-devel qt6-qttools-devel
qt6-qtsvg-devel qt6-qtshadertools-devel qt6-qtwayland-devel
manifold-devel argparse-devel flex bison
```

Then the usual two commands:

```sh
cmake -S . -B build-super -G Ninja -DCMAKE_BUILD_TYPE=Release -DENZO_SUPERBUILD=ON
cmake --build build-super
```

---

## Status

Helix is a work in progress, and so is the application it is based on. The macOS port
builds and installs, the CI job runs the superbuild on a `macos-15` runner, and the
features listed above are implemented in the tree. Expect rough edges, and expect the
interface to still be moving.

The 3D view is not presented on macOS yet. The scene graph of Qt runs on Metal there
and cannot adopt the Vulkan device of the graphics engine, so the viewport stays empty
while the rest of the application works. [build-macos.md](build-macos.md) says what the
port still needs.

The internal C++ namespace is still `enzo::`, and the scene format keeps the `.enzo`
extension so that files stay interchangeable with the upstream project. Renaming those
is a separate mechanical refactor.

**[ROADMAP.md](ROADMAP.md)** states where the work goes next, and why in that order.
Helix is a procedural modeling tool *and* an animation tool, so the roadmap has a
shared foundation and two tracks that build on it.

---

## License

MIT, inherited from the original project. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Enzo is free and open source. No expensive licensing, and no getting locked out of your
own work.
