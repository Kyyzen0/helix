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
- **Timeline** with playback, scrubbing, looping and a frame range, in the native
  interface (M4 below).
- **Keyframes, first version.** A parameter component holds a track of
  keyframes with step, linear or smooth interpolation, and every track is a
  pure function of the frame (`src/Engine/Simulation/Keyframe.h`,
  `KeyframeStore.h`). Set, move, delete and change the interpolation of keys
  from the interface and from MCP. Tangents and a curve editor are still to come.
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
  vertex colours (`Cd`), the materials (name, base colour factor and base colour texture, decoded
  with ImageIO to 8 bit sRGB pixels, shared by pointer between copies of a mesh) and a
  `material_index` per face that points into the mesh's table of materials. The facade
  (`GeometryReport::materials`) and the MCP `geometry_summary` report them. A texture that cannot
  be loaded keeps the material's factor and is named in the summary. **Left out for now**:
  the other texture maps (normal, roughness, metallic, emissive, occlusion), cameras and lights. The
  viewport draws the base colour factor and base colour texture of the materials (View > Materials,
  `render_view.materials`). A skinned mesh comes in as its bind pose with its skin, and its
  skeleton on the second output (see skeletal animation below); a `.blend` is exported with its
  skin and actions. Model I/O hands USD quads over as triangles and reads no skin.
  See [build-macos.md](build-macos.md).

- **M1.5, Helix MCP.** `Helix --mcp` runs a Model Context Protocol server on standard input and
  output, with no window, over the same facade as the interface. An agent creates, wires,
  parameterizes and imports nodes, sets the display node and the frame, saves and opens scenes,
  then checks the result as data (`geometry_summary`, `read_attribute`: counts, bounds, attributes
  and values of any node) and as a picture (`render_view`, from any angle). Its own engine runs
  in its own process, so it works on its own scene and not on the one open in a window; its changes
  are not grouped into undoable steps yet, and scripts are not sandboxed (track two, items 4 and
  5). See [build-macos.md](build-macos.md).

- **M2, the node editor.** The document window is the viewport over a node editor (SwiftUI `Canvas`,
  events in AppKit). Nodes are shown, selected, moved, connected and deleted, the display node is set
  by a double click, and nodes are created from a search palette that chains a new node under the
  selected one. Every edit is a step of the window's undo manager that keeps a snapshot of the scene
  (`captureScene` and `restoreScene` in the facade), and import is undoable too. `list_node_types`
  of the MCP server now gives the ports of each type. Single-key shortcuts (F, Shift+W, Shift+M)
  moved from menu items to the views, so they no longer fire while a text field has the keys.

- **M3, the inspector.** A column beside the viewport shows the parameters of the selected node:
  every type (float, int, string, toggle, dropdown, ramp), the styles (xyz, rangeCircle, boolIcon,
  boolSwitch, attribute, code), groups and dividers in the order of the node, ranges and locks,
  hide and disable conditions, expressions with their errors, reset, and ramps edited as points.
  Every edit is a step of the undo manager (a drag registers one step). A node that fails to cook
  now says why: the error and warnings are on the node, shown as a badge and tooltip on its card, in
  the inspector, in the viewport message (the root cause) and in the MCP tools (`list_parameters`
  gives ranges, options, defaults and expressions; `set_parameter` takes an expression;
  `reset_parameter`). See [build-macos.md](build-macos.md).

- **M4, the timeline, keyframes and the spreadsheet.** A bar under the viewport plays the scene
  (play, pause, step, loop, a range and a frame rate, real-time playback that skips the frames a slow
  cook misses, or every frame), with a ruler to scrub and the keys of the selected node as diamonds
  that drag, change interpolation and delete. The inspector keys any float: Set Keyframe, Remove
  Keyframe and Clear Keyframes in the row menu, a diamond that is filled on a key and hollow between,
  and editing a keyed value sets the key of the current frame. The bottom of the window switches
  between the node editor and a spreadsheet, a native `NSTableView` of the points, vertices, faces
  or primitives of the node in front, read a page at a time. The facade gained the range and rate
  (`setFrameRange`, `setFps`), the keyframes, and the cook generation of a node; MCP gained
  `set_keyframe`, `remove_keyframe`, `clear_keyframes`, `list_keyframes` and `set_time_range`, and
  `render_view` takes a `frame`. An undo never moves the playhead. A change of the frame rate now
  dirties the simulations, and a key is worth its own value on its own frame after a step (both were
  bugs). See [build-macos.md](build-macos.md).

- **The cook cache (track four, item 2).** A node that moves with the scene time keeps what it
  produced at every frame it cooked, so going back to a frame costs a lookup instead of a cook, and
  scrubbing, looping and replaying are cheap. The simulation node resumes from a state it saved
  every eight frames instead of replaying from its start frame, so playing it costs about the same
  at every frame. One budget (1 GiB, `setCookCacheBudget`) bounds all of it, and the entry used
  longest ago goes first. The ruler shows the cached frames of the display node as a blue line
  (Timeline > Clear Cook Cache empties it), the facade has `cookCacheInfo`, `cachedFrames` and
  `clearCookCache`, and MCP has `clear_cook_cache` and the `cook_cache` of `scene_info`. A cached
  frame is the same, bit for bit, as a cook from nothing (tests compare them). Measured on the Mac
  mini (Release build, the cook alone): playing a simulation with 20 000 particles from frame 1 to 500
  went from 9.3 ms a frame on average (17 ms at the end) to 0.34 ms at every frame, going back to
  a cooked frame from 3.8 ms to 0.04 ms, and a keyed transform on 100 000 points from 0.18 to
  0.04 ms a frame the second time through. See [build-macos.md](build-macos.md).

- **Skeletal animation.** `helix::geometryImport` has a second output, the skeleton of a rigged
  file as geometry: a point per joint, an open face per bone, and the attributes `joint_parent`,
  `rest_local`, `inverse_bind` and `pose`; the mesh on the first output carries four influences per
  point (`skin_joint0..3`, `skin_weight0..3`). `helix::skeletonPose` samples a glTF clip at the
  current time (`clip`, `speed`, `offset`, `loop`) as a pure function of the frame, and
  `helix::skinDeform` does linear blend skinning in parallel (about 0.8 ms for the 74 000 points of
  a rigged dragon, 3.7 ms with the display buffers). `geometry_summary` and the facade report the
  joints, roots and clips. **Gaps**: cubic spline tracks play as lines, no blend shapes, inverse
  kinematics, retargeting or blending between clips, CPU skinning, the skeleton and clips are not
  saved in a `.kut`, and USD skins are not read. See [build-macos.md](build-macos.md).

Known gaps found on the way, to fix as the work below touches them:

- **Playback gaps.** Playback cooks synchronously on the main thread, since the engine's background
  cooking is not safe to drive from the interface yet (the simulation nodes read the live network
  while they cook, and the keyframe store is not locked): a cook slower than a frame stalls the
  window for that long. A frame that was not cooked yet still costs a cook (a simulation resumes from
  the nearest state it saved, at most seven frames before). Keys are floats only. There is no dope sheet
  and no curve editor: the ruler shows the keys of one node as diamonds, and tangents are not
  editable (step, linear and smooth only). An edit stops the playback.
- **Spreadsheet gaps.** Read only: no editing of a value, no sorting or filtering, no selection of
  which attributes are shown, and no spreadsheet of a node's second output. An attribute that some
  primitives carry and others do not shows "–" in its column, since its values cannot be lined up
  with the rows (reading it per primitive is to do). Copy is capped at 100 000 rows.
- **Inspector gaps.** No file chooser for path strings, no editing of several nodes at once,
  no dragging of ramp points on the curve (a b spline segment of the curve is drawn
  approximately), and the combo box of an attribute name is a field with a list. A node nothing has
  cooked (outside the chain of the display node) shows no error until it is cooked. The background
  cook leaves `CookStatus::Failed` unset (the per-node errors are recorded, the status is not).
  The facade holds a literal value to the locked ends of a range; the engine itself only does so
  for an expression result.
- **Node editor gaps.** No rename of a node. Selection, pan and zoom are not saved in the file. No subnetworks (the editor
  shows the root of the scene), and ports are untyped, so any output connects to any input. A
  restore (undo) gives the nodes new ids, which only matters to a client that holds ids across it.
- **A node that crashes takes the process down.** Cooking runs in the process of the app or of
  `Helix --mcp`, so a bug in one node ends the session, as the null read `attributeNoise` made on a
  mesh with no points did (fixed). Cooking isolated in its own process (track four) is the cure.

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
- The viewport frames the projected bounding box, not the shape: a diagonal model (the dragon)
  still leaves empty corners of its box unused. Framing the cooked points would fit it tighter.
- The viewport draws the base colour of a material (factor times texture) with the lighting it
  had, and nothing else of it: no alpha or cut-out materials (alpha is ignored, a leaf or a fence
  is solid), no normal, roughness, metallic, emissive or occlusion maps, no vertex colour (`Cd`),
  no texture transform, no second uv set; a face with no material, or a mesh built by a node, is
  grey. The adaptive wireframe leaves a mesh that has a texture alone. The mesh lights follow the
  camera, so a model seen from below is lit from below, as in a modelling tool.
- The `bison` shipped by the toolchain script crashes on recent macOS; the
  build uses the parsers committed in daslang instead.
- The import cache watches the files Assimp opens (a glTF's `.bin`, for example), not the files
  a USD layer references or a `.blend` links. Changing one of those without touching the main
  file serves the old geometry until the cache is cleared. A texture file next to a glTF or an
  FBX is watched; a texture of a USD file is not.
- Materials are not saved in `.kut` files: the import node reads them again from its file. OBJ
  ignores its `.mtl` (the engine's own reader), so an OBJ has no material.
- Faces a node adds to a mesh that has a material table (the sides and caps of extrude and sweep,
  the caps of the plane cut) take the default of the face attribute, `material_index` 0, so they
  show the first material rather than none. Extrude and sweep work in place and keep the table;
  the generators (sphere, grid...) make meshes with no table. The merge node, `Mesh::merge`, the
  boolean (both inputs' tables, B renumbered), copy to points, cell fracture and the plane cut
  keep or combine the tables. `triangulateMesh` (internal to the boolean) does not.
- A texture is decoded at full size up to 8192 pixels a side (bigger ones are scaled down), whatever
  the model needs: a file with many large pictures costs 4 bytes a pixel each.

---

## Track one, the native Mac application

The interface moves from Qt and QML to Swift and SwiftUI, over the C++ engine
and the `HelixCore` facade. Each milestone is a usable step, and the Qt
interface keeps working until the last one removes it.

| Milestone | What it delivers |
| --- | --- |
| ~~M0, the base~~ | Done: the engine builds and is tested without Qt, and the `HelixCore` facade is importable from Swift. |
| ~~M1, the walking skeleton~~ | Done, trackpad navigation still to be validated on hardware: a Swift app that opens a `.kut` document and shows the cooked display geometry in a Metal viewport (`MTKView`), with trackpad orbit, pan and zoom. Every layer is crossed once, end to end. |
| ~~M1.5, Helix MCP~~ | Done: `Helix --mcp` is a Model Context Protocol server over the facade, so an AI agent can create, inspect and change a scene, and check the result as data and as a viewport capture. It runs its own engine, with no undo grouping and no script sandbox yet (track two). |
| ~~M2, the node editor~~ | Done: the window shows the viewport over a node editor. Show, select, move, connect, disconnect and delete nodes, set the display node, and create nodes from a search palette; every edit is one undoable step. See [build-macos.md](build-macos.md). |
| ~~M3, the inspector~~ | Done: every parameter type in a column beside the viewport, with undo through the macOS `UndoManager`, and per-node cook errors. See [build-macos.md](build-macos.md). |
| ~~M4, timeline and spreadsheet~~ | Done: playback, keyframes from the interface and from MCP, and the attribute spreadsheet as a native table. Playback is synchronous on the main thread for now. See [build-macos.md](build-macos.md). |
| **M5, finish** | Settings (started: a Settings window with an Appearance tab and five themes, done), icon, document types, packaging, signing and notarization, then the Qt interface is removed. |

What the native application gets from the platform rather than reimplementing:
documents with autosave and versions, menus and standard shortcuts, the
trackpad, dark mode, full screen and window tabs.

---

## Track two, AI native

The facade is the one API for people and machines. An AI can do anything the
interface can, and can check what it did.

1. ~~**MCP server** over the facade (M1.5 above), shipped inside the app and
   usable headlessly.~~ Done. Attaching an agent to the scene open in a window, rather than to
   its own engine, is still to do.
2. ~~**Results as data**: attribute queries, counts and bounds on any node's
   output, so an agent reads what the graph produced.~~ Done (`cookNodeReport`, `readAttribute`).
3. ~~**Results as images**: viewport captures and renders on request, from any
   camera, at any frame.~~ Done for an orbit camera framed on the geometry, at the current frame
   (`render_view`, `set_frame`); a free camera is still to do.
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
   indices, materials with their base colour textures as data) are done, and so are skins and
   animation clips (item 3). To do: the other texture maps, then glTF and USD export. Helix is useful next to other tools before it replaces any of them.
2. **UVs, textures and materials**: `uv` as a vertex attribute and the material model
   (a table of immutable materials on the mesh, `material_index` per face) are done, and so is
   the base colour in the viewport (factor and texture, one draw per material). To do: alpha and
   cut-out materials, the other maps, then a material node.
3. **Skeletal animation**: ~~skeletons as geometry (joints are points, bones are
   lines, transforms are attributes), four skin influences per point as
   attributes, a deform node, and glTF clips sampled at the current frame as
   a pure function of time~~ is done as far as it goes (`geometryImport` second output,
   `skeletonPose`, `skinDeform`). To do: cubic spline tracks (played as lines for now), blend shapes,
   inverse kinematics, retargeting, blending between clips, the GPU skinning, saving the skeleton
   in the `.kut`, and the skins of USD files (Model I/O).
4. **Keyframes, second version**: ~~set, move, delete and interpolation from the
   interface and MCP~~ is done (M4). To do: bezier tangents, a curve editor and a
   dope sheet next to the timeline, keys on integers and toggles, and keyframed
   transforms for skeletons.
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
   *Done for time*: the frame cache, the incremental simulation, its budget and the
   timeline bar (see the status above). Still to do:
   - **Keep the last result visible while a new one computes** needs cooking off the main
     thread: the background cook exists (`requestCook`, `CookScheduler`) and shows the last
     published result while it runs, but the interface does not use it yet, since the simulation
     nodes and the keyframe store read the live network from a worker, and a cook on a worker
     reads a snapshot that the cache does not use. Both go with the scheduler of item 3.
   - **Only a node that moves with the time is cached.** An edit of a static graph still recooks
     the node and what is downstream of it (the node keeps its own last output, which is what skips
     the subtree that did not change). Keeping the versions a slider went through, so that a
     toggle back is free, was left out on purpose: it would push out the frames.
   - **The budget is an estimate** from the size of the attributes, which counts a store that two
     frames share (copy on write) once for each. A texture of a material is not counted.
   - **No disk cache** (item 5), so the cache is gone when the document closes.
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
3. ~~M1.5, Helix MCP, so the AI track is exercised from the start~~ done
4. ~~glTF import with UVs~~ and ~~textured viewport shading~~ done (base colour only)
5. ~~M2, the node editor~~, ~~M3, the inspector~~ and ~~M4, the timeline and the spreadsheet~~
   done, and so is the frame cache of the cook cache; the scheduler work is next, so playback can
   leave the main thread
6. ~~Skeletal animation~~ done, and keyframes, second version
7. M5, then the stability and rendering items that remain

Each step ships with tests, and is checked on the reference machine before the
next one starts.
