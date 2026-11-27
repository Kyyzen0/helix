# Helix roadmap

This document states where the work goes, and in what order. The principles
it serves are in [VISION.md](VISION.md): AI native, everything is a graph,
stable by design, made for Apple silicon, native to the Mac. When the roadmap
and the vision disagree, the vision wins and this document is updated.

Helix is a fork of [enzo](https://github.com/parkerbritt/enzo) by
[ParkerBritt](https://github.com/ParkerBritt), MIT licensed. The engine
architecture, the node system, the attribute based data model and the
parameter system are his work and remain the foundation everything below is
built on. See [NOTICE](NOTICE).

---

## Where Helix stands

Done in the tree:

- **macOS arm64 port**: a native Apple silicon build, an `.app` bundle and a CI
  job on `macos-15`.
- **Metal viewport** through `metal-cpp`, in place of Vulkan through MoltenVK.
  See [docs/METAL-RENDERER.md](docs/METAL-RENDERER.md).
- **Async cooking** on a worker pool, with cancellation and coalescing.
- **Timeline** with playback, scrubbing, looping and a frame range.
- **Keyframes, first version.** A parameter component holds a track of
  keyframes with step, linear or smooth interpolation, and every track is a
  pure function of the frame (`src/Engine/Simulation/Keyframe.h`,
  `KeyframeStore.h`). Tangents and a curve editor are still to come.
- **Simulation nodes**: particles and springs, keyframable from the timeline.
- **Sweep node**, **user node authoring** from script or expression source,
  and copy on write attributes.
- **M0, the engine without Qt.** `HELIX_BUILD_GUI=OFF` builds the engine, the
  nodes, the Metal graphics and the tests without Qt, and the `HelixCore`
  facade gives a Swift application a plain C++ API over the engine. Swift
  imports it through C++ interoperability (`apple/HelixCoreSmoke`). See
  [build-macos.md](build-macos.md).
- **M1, the walking skeleton.** `apple/Helix` is a Swift app that opens a `.kut` document
  and draws the cooked display geometry in a Metal viewport, with trackpad orbit, pan and
  zoom. The scene file now stores its display node. The facade drives the viewport renderer
  and can render the display node to a PNG (`renderToImage`), which is how the app and an AI
  check what was drawn without a window. See [build-macos.md](build-macos.md).

- **Import of the principal 3D formats.** `helix::geometryImport` reads a file by its
  extension: glTF and GLB (with `KHR_draco_mesh_compression`), FBX, STL, PLY, Collada (`.dae`),
  3DS, 3MF, DXF, X3D and X through Assimp; OBJ through the engine's own reader; USD, USDA,
  USDC and USDZ through Apple's Model I/O; and `.blend` through an installed Blender run in the
  background (converted to GLB with the modifiers applied, and kept in the caches of the user).
  The node applies the transforms of the hierarchy, merges the meshes (or not), corrects a
  Z up file, and keeps the result in memory until the file or a setting changes. The app opens a
  3D file as a new untitled scene (File > Open, double click, the Dock) and imports one into the
  open scene (File > Import…, drag and drop). What comes through: points, faces, normals, `uv`,
  vertex colours (`Cd`) and a `material_index` per face. **Left out for now**: skeletons and
  animations (counted in the summary of the import, and next in track three), materials and
  textures (the viewport shows none yet), cameras and lights. The geometry is the bind pose of a
  skinned mesh, and the current pose of a `.blend`. Model I/O hands USD quads over as triangles.
  See [build-macos.md](build-macos.md).

Known gaps found on the way, to fix as the work below touches them:

- **The engine is a singleton: one scene at a time.** The native app refuses a second
  document with an alert. Several scenes at once need a multi-instance `NetworkManager`.
- The node loader does not find node types in nested folders, so the focused
  simulation nodes (`nodes/simulation/particles`, `springs`) are skipped by
  their tests.
- The engine prints debug output (`ic|`) in release builds.
- **daslang is built with RTTI now, and `libhelixEngine.dylib` grew by 7.9 MB (48.6 to 56.5 MB,
  +16%) because of it.** daslang adds `-fno-rtti` to its release flags, and a file built that way
  that throws or catches a standard exception emits a private copy of the typeinfo of
  `std::exception`, so `catch (const std::exception&)` missed what another image threw (the
  engine, the node library and the tests are separate images). `CMakeLists.txt` adds `-frtti` to
  every daslang target, and `tests/ExceptionBoundaryTests.cpp` fails if an image defines such a
  copy again. Getting the size back means building only the daslang files that never touch an
  exception without RTTI, which needs the daslang build files to say which.
- The `bison` shipped by the toolchain script crashes on recent macOS; the
  build uses the parsers committed in daslang instead.

---

## Track one, the native Mac application

The interface moves from Qt and QML to Swift and SwiftUI, over the C++ engine
and the `HelixCore` facade. Each milestone is a usable step, and the Qt
interface keeps working until the last one removes it.

| Milestone | What it delivers |
| --- | --- |
| ~~M0, the base~~ | Done: the engine builds and is tested without Qt, and the `HelixCore` facade is importable from Swift. |
| ~~M1, the walking skeleton~~ | Done, trackpad navigation still to be validated on hardware: a Swift app that opens a `.kut` document and shows the cooked display geometry in a Metal viewport (`MTKView`), with trackpad orbit, pan and zoom. Every layer is crossed once, end to end. |
| **M1.5, Helix MCP** | A Model Context Protocol server over the facade, so an AI agent can create, inspect and change a scene, and check the result as data and as a viewport capture. |
| **M2, the node editor** | Show, select, move and connect nodes; create them from a search palette. |
| **M3, the inspector** | Every parameter type, with undo through the macOS `UndoManager`. |
| **M4, timeline and spreadsheet** | Playback, keyframes from the interface, and the attribute spreadsheet as a native table. |
| **M5, finish** | Settings, icon, document types, packaging, signing and notarization, then the Qt interface is removed. |

What the native application gets from the platform rather than reimplementing:
documents with autosave and versions, menus and standard shortcuts, the
trackpad, dark mode, full screen and window tabs.

---

## Track two, AI native

The facade is the one API for people and machines. An AI can do anything the
interface can, and can check what it did.

1. **MCP server** over the facade (M1.5 above), shipped inside the app and
   usable headlessly.
2. **Results as data**: attribute queries, counts and bounds on any node's
   output, so an agent reads what the graph produced.
3. **Results as images**: viewport captures and renders on request, from any
   camera, at any frame.
4. **Intent grouped undo**: the changes an agent makes are one undoable step
   that names what it meant to do.
5. **Sandboxed scripts**: code an agent writes runs without access to the rest
   of the machine.
6. **A readable scene format**: scene files stay diffable, so a person can
   review what an agent changed.

---

## Track three, everything is a graph

The data model grows so that 3D, 2D, animation and simulation all live in the
same graph.

1. **Interchange**: ~~glTF import~~ and the other formats (meshes, normals, UVs, material
   indices) are done. To do: skins and animation clips, materials with their textures, then
   glTF and USD export. Helix is useful next to other tools before it replaces any of them.
2. **UVs, textures and materials**: `uv` as a vertex attribute, a material
   model on top of the attribute system, and textured shading in the viewport.
3. **Skeletal animation**: skeletons as geometry (joints are points, bones are
   lines, transforms are attributes), four skin influences per point as
   attributes, a deform node, and glTF clips sampled at the current frame as
   a pure function of time.
4. **Keyframes, second version**: bezier tangents, a curve editor next to the
   timeline, and keyframed transforms for skeletons.
5. **2D**: curves, vector shapes and text as geometry, and image operations as
   GPU nodes of the same graph.
6. **A graph that can express more**: node templates with exposed parameters,
   subgraph instancing, loops and conditionals.
7. **A growing simulation family**: cloth, soft bodies, fluids and crowds, all
   reading and writing the same attributes.

---

## Track four, stable and fast on Apple silicon

The reference machine is a Mac mini M4. Everything above must run well on it.

1. **Stability**
   - Atomic saves (done in the facade), autosave and versions through the
     document model.
   - Cooking isolated from the interface, then in a separate process (XPC), so
     a node that crashes or hangs is reported on that node and the app keeps
     running.
   - The GPU tests run on every change instead of being skipped when no Metal
     device is found.
2. **Cook caching and dirty propagation**: hash the inputs of a node, skip the
   subtree when nothing changed, and keep the last result visible while a new
   one computes. This makes heavy scenes interactive and scrubbing cheap.
3. **One scheduler, the right cores**: one worker pool instead of the cook
   threads, TBB and manifold competing, sized on the performance cores, with
   quality of service set for interactive and background work.
4. **GPU cooking**: the expensive attribute operations move to Metal compute
   shaders, sharing unified memory with the CPU without copies.
5. **A memory budget**: on demand evaluation, a disk cache for simulations, and
   streaming for large data, within the memory of a base model machine.
6. **Rendering**: a physically based renderer on Metal, using hardware ray
   tracing on the chips that have it.
7. **Measured targets** ([docs/PERFORMANCE.md](docs/PERFORMANCE.md)): first
   interactive frame under 2 s, interactive latency under 100 ms, viewport
   frame time under 16 ms for 500 k triangles, checked on the reference
   machine.

---

## Order of work

1. ~~M0, the engine without Qt and the facade~~ done
2. ~~M1, the walking skeleton of the native app~~ done
3. M1.5, Helix MCP, so the AI track is exercised from the start
4. ~~glTF import with UVs~~ done; textured viewport shading next
5. M2 to M4, the native interface, with cook caching and the scheduler work
   alongside
6. Skeletal animation and keyframes, second version
7. M5, then the stability and rendering items that remain

Each step ships with tests, and is checked on the reference machine before the
next one starts.
