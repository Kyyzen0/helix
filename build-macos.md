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
| Blender (optional) | Only to open `.blend` files, and only at run time. Not needed to build or to run the tests, whose `.blend` test is skipped without it. |

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

## Building the engine without Qt

The engine (`helixEngine`), the nodes (`helixOps`), the Metal renderer
(`helixGraphics`), the `helixCore` facade and the `tests` target need no Qt.
`-DHELIX_BUILD_GUI=OFF` leaves the Qt interface out: no Qt is searched for or
linked, the `helix` application, `helixGuiTests` and the one engine test that
goes through a Qt type (`ParameterItemTests.cpp`) are not built. Objective-C++ is enabled in
both configurations, since the USD reader of the engine uses Model I/O. The default is `ON`,
which is the build described above.

With the dependencies of an earlier superbuild already in `build-super/deps`,
nothing needs to be built again but Helix itself. A prefix built before Assimp joined the
superbuild lacks it: build that one dependency into the prefix, from the source the superbuild
holds (or let `cmake --build build-super --target assimp` fetch it), with the options of
`superbuild/deps/assimp.cmake`. This configures a build folder
`build-core` straight against them, with the sources of the fetched libraries
that the superbuild already holds (no network is needed):

```sh
source toolchain/env.sh
D="$PWD/build-super"
cmake -S . -B build-core -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHELIX_SUPERBUILD=OFF \
    -DHELIX_BUILD_GUI=OFF \
    -DCMAKE_PREFIX_PATH="$D/deps" \
    -DHELIX_DEPS_PREFIX="$D/deps" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_YAML-CPP="$D/helix-build/_deps/yaml-cpp-src" \
    -DFETCHCONTENT_SOURCE_DIR_CATCH2="$D/helix-build/_deps/catch2-src" \
    -DFETCHCONTENT_SOURCE_DIR_METALCPP="$D/helix-build/_deps/metalcpp-src"
cmake --build build-core --target tests helixCore helixOps
build-core/helix.app/Contents/bin/tests
```

Without an earlier superbuild, `cmake -S . -B build-super -G Ninja
-DHELIX_SUPERBUILD=ON -DHELIX_BUILD_GUI=OFF` builds the dependencies as
usual but skips the Qt download. Leave `-DHELIX_SUPERBUILD=OFF` and the
fetch settings out of the command above, and give `CMAKE_PREFIX_PATH` the
dependency prefix of that build (`build-super/deps`).

If the bison of the toolchain crashes on the host (`toolchain/bison/shim/bison
--version` ends with a segmentation fault), add `-DDAS_FLEX_BISON_DISABLED=ON`
to the configure command. daslang then builds from the parsers committed in
`extern/daslang/src/parser`, the ones the version check of
[The parser tools](#the-parser-tools) is there to keep up to date.

The tests that draw with Metal skip themselves, with the message "No Metal
device available", where the process cannot reach a GPU, as in a sandbox.

### The Swift check of the facade

`src/HelixCore/HelixCore.h` is the header a native Swift application imports
(`src/HelixCore/module.modulemap`, with C++ interoperability). The small tool in
`apple/HelixCoreSmoke` proves it: it starts the engine, creates a cube, cooks it
and prints the number of points.

```sh
cmake --build build-core --target helixCore helixOps
apple/HelixCoreSmoke/build.sh
```

### The native application

`apple/Helix` is a standard Xcode project: a SwiftUI document app (`.kut` scenes) with a Metal
viewport over the `HelixCore` facade. It needs the `build-core` of the section above, and
macOS 15, Xcode 26 or newer and the Metal toolchain.

```sh
cmake --build build-core --target tests helixCore helixOps   # once, and after engine changes
xcodebuild -project apple/Helix/Helix.xcodeproj -scheme Helix -configuration Debug build
```

The project builds the engine again before it compiles (a build phase that runs `cmake --build
build-core --target helixCore helixOps`, a no-op when nothing changed; set `HELIX_BUILD_CORE` to
use another folder), links the static libraries, and a second phase makes the bundle stand on
its own:

```
Helix.app/Contents/
├── MacOS/Helix
├── Frameworks/    libhelixEngine, libhelixOps (the nodes), tbb and boost, with run paths
│                  rewritten to sit next to each other
└── Resources/     nodes/, daslib/, helix.metallib (the engine's shaders), default.metallib
                   (the one shader that puts a frame on screen), the icon and the strings
```

The signature is ad hoc ("Sign to Run Locally"), with no team.

The engine holds one scene, so the app opens one document at a time and refuses a second with
an alert. The scene file type is `com.kyyzen.helix.scene` (`.kut`, JSON text).

The viewport draws the edges of the mesh over its shading according to the **View > Wireframe**
setting (Auto, On, Off; Shift+W cycles them while the viewport has the keys). Auto leaves the wireframe out above 50 000
triangles and fades it out as the edges come within a few points of each other on screen, since a
wireframe on a dense mesh covers the shading entirely. The lights follow the camera.

`Helix --render-snapshot <in.kut | --new-scene> <out.png> [width height]` opens a scene, frames
its geometry, draws it off screen and writes a PNG, with no window. It is the check of the whole
chain without a screen. `apple/Helix/Samples` holds two scenes to try it on, written by
`apple/Helix/Samples/make-samples.sh`.

### The node editor

The window of a document is split in two, the viewport over the node editor, with a bar
between them to resize. Click the editor to give it the keys: the shortcuts below apply to the view
that has them, and the viewport's `F`, `Shift+W` and `Shift+M` apply while the viewport has them. They
are handled by the two views and not by menu items with a key, because a menu shortcut without the
command key also fires while a text field has the keys, and an "M" typed in the search palette must
not hide the materials. The menu items stay, without keys.

- **Look.** A dotted grid, cards of 148 by 46 centred on the position of the node (the geometry of the
  Qt editor, so a scene laid out there looks the same), the name and the type label on the card,
  inputs on the top edge and outputs on the bottom edge (a bar for a multi input port), links as
  vertical curves. The display node has a bar on its left edge and a flag; selected nodes and links
  have the accent colour.
- **Navigate.** Two-finger scroll pans, pinch zooms around the pointer, Command and scroll (or a
  mouse wheel) zoom, the middle button pans. Zoom is held between 0.1 and 5. `H` frames the selected
  nodes, or all of them when none is selected (Node > Frame Nodes does the same).
- **Select.** Click a node; Shift or Command click toggles; drag on empty space for a marquee (a
  node is taken when the box touches its card); click empty space clears; `Command A` selects all;
  click a link to select it.
- **Move.** Drag selected nodes; they follow the pointer, and the positions reach the engine when
  the button goes up, as one step.
- **Connect.** Drag from an output port to an input port. The link snaps to the nearest input within
  a few points, or to the input of the card nearest in x when the pointer is over a card, and the
  port's label shows. Dropping on an occupied single input rewires it; dropping on a multi input
  bar adds the next input. Drag from a connected input to take its link off: dropped on nothing it
  is disconnected, dropped on another input it is moved. A refusal of the engine (a link that would
  close a loop, a bad port) shows as a message over the editor for a few seconds.
- **Delete.** Backspace or Delete removes the selected nodes and links (with the links of the nodes).
- **Display.** Double-click a node, or right-click it and choose Set Display Node.
- **Add.** `Tab` in the editor, Node > Add Node… (`Command K`), the + button of the toolbar, or
  right-click on empty space opens the search palette. Type to filter (a prefix, a part or the
  letters in order of the label or the name, `attrn` for Attribute Noise; the tags count too),
  Up and Down choose, Return makes the node, Escape closes. The node goes at the pointer. When
  exactly one node is selected and the new type has an input, it goes 120 below that node (or to the
  right when that spot is taken) and its first input is wired from the first output of the selected
  node. The new node is selected. When the scene showed nothing, the first node made is displayed.
- **Undo.** Every edit is one step on the undo manager of the window (Edit > Undo, Command Z): "Move
  Nodes", "Connect", "Delete Nodes", "Add Cube", "Set Display Node", "Import" and so on. A drag is one
  step, and none when nothing moved. Registering a step marks the document as edited. A step keeps
  the whole scene as the engine writes it (`captureScene` and `restoreScene` of the facade, the
  text of a `.kut` file), so everything a file keeps comes back. A restore gives the nodes new ids;
  the editor keeps its selection by path.

Selection, pan and zoom are not saved in the file.

`Helix --render-node-editor <in.kut | --new-scene> <out.png> [width height] [--select <name>]...
[--dark]` draws the editor of a scene, every node framed, to a PNG with no window (SwiftUI's
`ImageRenderer`). The tests of the editor's logic (view transform, geometry, hit testing, search,
placement, and the gestures through a model with no engine; and the inspector's number text, slider
mapping, scrub, tree, drag-as-one-step state machine and model) are in a bundle that needs no host app:

```sh
xcodebuild test -project apple/Helix/Helix.xcodeproj -scheme HelixNodeEditorTests -destination 'platform=macOS'
```

### The inspector

The window shows a column on its right, the inspector (View > Show Inspector, `⌥⌘I`, or the
toolbar button; shown by default, and remembered). It shows the parameters of the node selected in
the node editor: one node at a time ("N nodes selected" and "No node selected" otherwise). The
header gives the node name and type, a button for the display flag, and the error or warnings of
the node's last cook.

**Layout.** The rows come in the order the node declares them, through `listParameterLayout`:
dividers are headings, a horizontal group is one row (its label, then its members side by side),
a vertical group opens and closes. A parameter hidden by its hide condition is not drawn, and a
disabled one is dimmed and cannot be edited; both follow the values of the other parameters as
they change. A tooltip is the row's help text.

**Widgets.** A float is a slider over the range of the node and a field; an unlocked end of the
range is only where the slider stops (type past it), a locked end is a limit. An integer adds a
stepper. A vector is X, Y and Z fields, and dragging over the letter scrubs the value (Option for
fine control). `rangeCircle` is a small arc dial with two fields. A toggle is a checkbox, a switch
(`boolSwitch`) or an icon button (`boolIcon`). A dropdown is a menu of the option labels. A string
is a field, committed on Return or when it loses focus; the `attribute` style adds a button that
lists the attributes of the geometry wired into the first input; the `code` style is a monospaced
editor with an Apply button (`⌘Return`). A ramp shows its curve and a list of points (position,
value, interpolation, remove) with Add Point; the curve of a b spline segment is a drawing aid.

**Expressions and reset.** The context menu of a row (or of one component of a vector) has Edit
Expression…, Clear Expression and Reset to Default. A component with an expression shows its text
in monospaced accent colour instead of its value. An expression that does not compile is kept, as
the person is still typing it, shown in red with its first line of error under the row, and reads
as zero. A component that holds keyframes shows a diamond, which is filled when the frame the scene
sits on has a key of it and hollow when it does not, and is edited like any other (see Keyframes).

**Undo.** Every edit is one step of the window's undo manager, named "Change <label>", "Edit
Expression", "Clear Expression" or "Reset <label>", through the same scene snapshots as the node
editor. A drag (a slider, a scrub) updates the scene live and registers one step when it ends, and
nothing when it comes back to where it started. While a drag goes on, a scene whose cook costs
more than 30 ms per change gets only the newest value (at most one change per cost). Text fields
and steppers commit one step each. Typing in a field never reaches the single-key shortcuts of
the viewport or the node editor, which only act while they have the keys.

`Helix --render-inspector <in.kut | --new-scene> <node name or path> <out.png> [width [height]]
[--dark]` draws the inspector of one node to a PNG with no window (the panel is laid out in an
off screen window and its pixels read back), as `--render-node-editor` does for the editor.

### Timeline

A bar at the bottom of the window (View > Show Timeline, `⌥⌘T`; shown by default, and remembered)
holds the playback controls, the frame, and a ruler. Left to right: go to start, step back, play or
pause, step forward, go to end, a loop toggle (on by default), then the frame, the first and the last
frame of the range, and the frame rate. The ruler numbers the frames (the step between two labels
adapts to the width: 1, 2, 5, 10, 20...), shades the range, and carries the playhead. A click or a
drag on the ruler moves the playhead and stops the playback. The diamonds on the ruler are the keys of
the node in front, one diamond for every frame that has a key, however many parameters it holds (see
Keyframes). The node in front is the one selected in the node editor, or the display node when there
is no selection or several. A thin blue line along the bottom of the ruler marks the frames the cook
cache holds for the display node, which show without a cook (see Cook cache).

**Keys.** `Space` plays or pauses, `←` and `→` step one frame, `⌥←` and `⌥→` go to the start and the end.
They act only while the viewport, the node editor or the ruler has the keys, never while a text field
does, for the reason the viewport keys do. The Timeline menu has the same commands without keys, the
two settings (Loop, and Play Every Frame), and Clear Cook Cache.

**Playback.** The frames follow the clock: a timer ticks at 120 Hz, and each tick moves the playhead
by the time that went by times the frame rate, keeping the part of a frame that did not make a whole
one, so 24 frames a second is 24 on average whatever the ticks do. A cook that takes longer than a
frame skips the frames it missed (**real time**, the default). Play Every Frame shows every frame, one
after the other, at no more than the frame rate, so a slow scene plays slower and skips nothing. At
the end of the range the playhead goes back to the start, or stops when Loop is off. While playing,
the bar says what is really happening, such as "24 fps · 18 shown": the frame rate of the scene, and
the frames drawn in the last second.

Each frame is `setFrame` then the cook of the display node, on the main thread. **It is synchronous**:
the engine's background cooking is not safe to drive from the interface yet (the simulation nodes read
the live network while they cook, and the keyframe store is not locked), so a cook that takes longer
than a frame stalls the interface for as long as it takes. The viewport keeps the camera where the
person put it (a frame of playback never reframes the geometry), and the node editor is read again
when the playback stops, not at every frame. A simulation node replays from its start frame at every
cook, which makes a frame as pure a function of the frame number as any other. Started from nothing
that cost grows with the frame, so the cook cache (below) saves the state of the simulation every
eight frames and resumes from the nearest one. Measured on the Mac mini (Release build, the cook and
the buffers of the viewport without the GPU), for a frame that was not cooked before: about 0.3 ms a
frame for `Waves.kut`, 0.05 ms for a keyed transform on the textured quad, and for a
`helix::simulation` node 0.2, 1.7 and 8.3 ms at frames 10, 100 and 500 when the cache holds nothing
(1.1, 4.3 and 19 ms with 20 000 particles), and 0.1 ms (0.3 ms with 20 000) at every frame when the
playhead goes forward one frame at a time. `build-core/helix.app/Contents/bin/tests "[.bench]"`
measures it again, with and without the cache.

**An edit stops the playback.** Any edit (the node editor, the inspector, a key, the range) pauses
it first, so that what the person changes lands on the frame they were looking at.

**Range and rate** are part of the scene: changing them is a step of undo ("Change Frame Range",
"Change Frame Rate") and is saved in the file. The frame (the playhead) is not: moving it is never a
step of undo, and an undo or a redo leaves the playhead where it was (the captured scene holds the
frame it was captured on, and the document puts the playhead back after restoring it). A change of
the frame rate dirties the nodes that read the time, since a simulation steps by one over the rate.

`Helix --render-timeline <in.kut> <out.png> [width] [--select <node>] [--frame <n>] [--dark]` draws
the bar to a PNG with no window, with the diamonds of one node.

### Keyframes

A float component can hold keys: values pinned to frames, with a way to move from one to the next
(step: hold, then jump; linear; smooth: eased in and out). Its value is a pure function of the frame,
and the first and last key are held past the ends. Only floats can be keyed; the keys are saved in the
`.kut` file. They are made, moved and removed from the interface, from the facade and from the MCP
server alike (`setKeyframe`, `setKeyframeAtCurrentValue`, `removeKeyframe`, `moveKeyframe`,
`setKeyframeInterpolation`, `clearKeyframes`, `listKeyframes`).

- **Set Keyframe.** The context menu of a float row (or of one component of a vector) has Set
  Keyframe, which keys the value the component has now on the current frame, Remove Keyframe (enabled
  when the current frame has a key) and Clear Keyframes, which takes every key off and puts back the
  value and the expression the component held before it was keyed. A parameter with keys shows a
  diamond by its label: filled when the current frame has a key, hollow when it does not.
- **Auto key.** Editing a component that already has keys (typing, a slider, a scrub) sets the key
  of the current frame, or replaces its value, instead of being undone at the next frame. A new key
  takes the interpolation of the key before it, or smooth when it is the first. A drag registers one
  step of undo. A change to a component with no key is a plain edit, and does not make one. Reset to
  Default is refused for a keyed parameter (clear its keys first), and so is an expression.
- **On the ruler.** Drag a diamond to move all its keys by whole frames (one step of undo, refused
  with a message when it would land on a frame where a key of the same component already is, and held
  inside the range). The context menu of a diamond has Interpolation (Step, Linear, Smooth) and Delete
  Keyframe. The tooltip lists each key with its value and interpolation.
- **MCP.** `set_keyframe`, `remove_keyframe`, `clear_keyframes`, `list_keyframes`, `set_time_range`;
  `render_view` takes a `frame` (it draws that frame, then puts the scene back on the one it was on);
  `scene_info` lists the animated nodes; `set_parameter` on a keyed component keys the current frame.

Not yet: bezier tangents, a curve editor and a dope sheet, keys on integers, toggles and strings, and
keys moved or copied across several nodes at once (the ruler shows one node's keys).

### Cook cache

A node that moves with the scene time keeps what it produced at every frame it has cooked. Going back
to a frame (scrubbing, looping, stepping back, replaying) then costs a lookup instead of a cook, and
the packets are the very ones the first visit made, so nothing is copied. The facade is `cookCacheInfo`
(bytes used, the budget, the number of entries, hits and misses), `cachedFrames(node)` (the whole
frames a node has a cook for), `setCookCacheBudget(bytes)` and `clearCookCache()`. MCP has
`clear_cook_cache`, and `scene_info` carries a `cook_cache` object.

**What is cached.** A node *moves with the time* when its cook reads the frame or the time (the
simulation), when one of its parameters does (a keyframe, or an expression with `frame()`), or when
something wired into it does. Only those nodes are filed; what does not move with the time is already
kept by the node itself, and filing every version of it while a slider is dragged would push out the
frames that matter. A node whose `node.yaml` says `cache: false` is never filed, and nor is what is
wired after it while it moves with the time. `helix::geometryImport` says so: it reads files on
disk, which the cache key does not see, so a keyed import parameter goes through the import cache's
own check of the file and its dependencies at every frame. Nothing is kept of a cook that failed, and a cook on a
worker thread of the background cook, which reads a snapshot of the network, does not use the cache.

**The key.** A cook is filed under a 64 bit hash of everything it reads: the type of the node; the
identity of the packet that arrives at each input (every packet a node publishes gets a number from
one counter, and a cook answered from the cache publishes the packet it published the first time, so a
node downstream of a cached node is a hit at the same frames); the value of every parameter as the
cook reads it, an expression evaluated (a ramp as its control points); the keyframes of the node, since
a simulation reads a keyed setting at the frames before the current one; the frame rate; and the frame
and the time. A cached frame is the same, bit for bit, as a cook from nothing, because the cook is a
pure function of what the key holds (`tests/CookCacheTests.cpp` compares the two for a keyed transform,
an expression of `frame()`, a simulation and what is wired after it, going forward, back and jumping).

**What drops it.** Anything that dirties a node for a reason other than the frame moving: an edit of a
parameter, a link made or removed, a keyframe edited, the node deleted, and the same for anything
upstream of it, since the dependents of a dirtied node are dirtied too (a key written by the playhead
reaching a frame is not an edit). A change of the frame rate, a new scene and a restore empty the whole
cache. The key is the guard behind that: an edit that nothing dirties (a key moved far from the
playhead, which leaves the value at the playhead where it was) still cannot be answered with an old
cook, since the keys and the inputs are in the key.

**The budget.** One budget for the whole process, 1 GiB by default (`setCookCacheBudget`), counts the
size of the attributes of what is held, and the entry used longest ago goes first, whatever node it is
of. A store that two frames share (a grid that a transform moves) is counted once for each of them, so
the estimate errs on the high side; a texture is not counted. Zero turns the cache off, which is also
how a cook from nothing is made for a comparison.

**The simulation.** A solver is worked out from its first frame, which cost the frame. It now saves
its state (the particles, or the points of the sheet and where they were) every eight frames in the
same cache, under the key of the node without the frame, and a cook starts from the latest state at or
before its frame and steps the frames after it (at most seven, plus the part of a frame). The state is
all the next step reads and the steps are the same, so the result is the same bit for bit; a change of
a setting, a key or an input loses the states. A keyed setting is read through its keys, so its value
at the frame is left out of the key of the states and a simulation with a keyed setting resumes too.

**The ruler.** The blue line under the ticks marks the whole frames the cache holds for the display
node. It is read when the playhead is moved by hand, every twelve frames of playback and when the
playback stops, and after an edit. Timeline > Clear Cook Cache empties the cache (the scene is not
changed, so it is not a step of undo).

**Limits.** The background cook does not use it, and the interface does not use the background cook
yet, so a frame that was not cooked still stalls the interface for its cook. There is no disk cache,
so it is empty when the document is closed, and the budget is a count of attributes, not a measure of
the memory the process uses.

### Spreadsheet

The bottom of the window has two views, switched by a control above it: **Network** (the node editor)
and **Spreadsheet** (View > Show Spreadsheet, `⌥⌘S`). The spreadsheet is a native `NSTableView`: a
table with 100 000 or a million rows costs what a screenful costs, since the table asks only for the
cells in view and the pages (512 rows of one attribute) those cells are in are read from the engine on
demand, then kept. It follows the node in front (the one selected, or the display node), and shows its
error in place of the table when the node does not cook.

A picker above the table chooses which elements are the rows: Points, Vertices, Faces or Primitives,
each with its count. The columns are the row number (`#`), the primitive the row belongs to (`prim`,
when the output has several), then one per component of every attribute of that owner: `P.x P.y P.z`
for the position (the header tooltip says `Position`), `uv.x uv.y uv.z`, `m.0` to `m.15` for a matrix,
and the tooltip gives the type. Numbers are monospaced and right aligned, written with six significant
digits and no trailing zeros; integers are integers, booleans are `true` and `false`. Select rows and
`⌘C` copies them as tab separated text with the header (up to 100 000 rows). An attribute that only
some primitives carry cannot be lined up with the rows, so its column shows "–" (listed as a gap), and
one whose type differs between primitives is shown as "mixed", as the facade refuses to read it.

**How the data gets there.** `cookNodeReport` gives the counts, the columns and the primitive ranges,
and `readAttribute` gives a page. Both cook a node only when it is dirty, so a clean node costs a
read. A counter bumped at every cook of a node (`nodeCookGeneration`, one counter for all nodes, so
two cooks never share a number) comes back with each report and page: when it is not the one the
cache holds, every cached page is dropped. The report of the last node is also kept by that number,
which saves counting the triangles again. The table is read again at each frame while it is shown and
the playhead moves; a hidden table does not cook anything.

`Helix --render-spreadsheet <in.kut> <node> <out.png> [width height] [--owner points|vertices|faces|primitives] [--dark]`
draws the table of one node to a PNG with no window.

### Node errors

A node that fails to cook says why. `throwError` records the message on the node, and so does an
exception an implementation throws (nothing escapes the cook: the node's outputs stay empty and the
rest of the graph cooks with an empty input where it was). The facade gives it as `NodeInfo.error`
and `warnings`; cooking a node whose upstream failed is a failed status naming the root cause,
`"/noise: The attribute needs a name."`, which the viewport shows in its message label and
`geometry_summary` and `render_view` return as `isError`. In the node editor a failed card has a red
outline and a red badge, a warned one a yellow badge, and the message is the tooltip. A node
nothing has cooked yet (not in the chain of the display node) has no verdict. A change to a node
clears its verdict until it cooks again.

## Helix MCP

`Helix --mcp` runs a [Model Context Protocol](https://modelcontextprotocol.io) server on standard
input and output, so an AI agent can create, inspect and change a scene and check the result as
data and as a picture. It is the app binary itself, with no window: newline delimited JSON-RPC
2.0, one message per line, over the same `HelixCore` facade the interface uses
(`apple/Helix/Sources/MCP*.swift`). Standard output carries the protocol only. The engine prints
debug output, so the server points it at standard error before the engine starts.

Register it with Claude Code, with the path of a built app:

```sh
claude mcp add helix -- /path/to/Helix.app/Contents/MacOS/Helix --mcp
```

or with any other MCP client, in its JSON configuration:

```json
{
  "mcpServers": {
    "helix": {
      "command": "/path/to/Helix.app/Contents/MacOS/Helix",
      "args": ["--mcp"]
    }
  }
}
```

The tools:

- `scene_info`: the nodes, links, display node, time, the cook cache and whether there are unsaved
  changes.
- `clear_cook_cache`: forgets what the cook cache holds (the scene is not changed).
- `new_scene`, `open_scene`, `save_scene`: scene files. The first two refuse while the scene has
  unsaved changes, unless `discard_changes` is true.
- `list_node_types`: the type names `create_node` accepts.
- `create_node`, `delete_node`, `connect_nodes`, `disconnect_nodes`, `set_display_node`: the graph.
- `list_parameters`, `set_parameter`: the parameters of a node, and one to write (a number, an
  integer, a boolean, a string, or an array such as `[1, 2, 3]`), read back after the write.
- `import_file`: imports a 3D file as a node (see Importing 3D files).
- `set_frame`, `set_time_range`: move the scene in time, and set the range and the frame rate.
- `set_keyframe`, `remove_keyframe`, `clear_keyframes`, `list_keyframes`: keyframes (see Keyframes).
- `geometry_summary`: counts, bounds, attributes and materials (base colour, texture size and
  source) of the output of any node.
- `read_attribute`: the values of an attribute, up to 10 000 elements a call, with the total to page by.
- `render_view`: a PNG of a node, framed like the viewport, optionally orbited (`azimuth`,
  `elevation`) and at a given `frame`, up to 2048 pixels a side.

A node is named by its name (`cube1`), its path (`/cube1`) or its id. A call that fails (a node
that does not exist, a value out of range) returns a result with `isError` and a message, so the
agent can read it and retry; only a malformed message or an unknown tool is a protocol error.

`apple/Helix/Scripts/mcp-smoke.py` checks the server end to end against a built app (Python 3,
standard library only):

```sh
python3 -I apple/Helix/Scripts/mcp-smoke.py /path/to/Helix.app/Contents/MacOS/Helix
```

`apple/Helix/Scripts/launch-smoke.py` launches the built app five times (plain, with a `.kut`, with a
`.glb`, with a `.gltf` that has a `.bin` and a picture beside it, in French), kills it each time, and fails if it dies within 6 seconds or if the opened file
did not become a scene that imports the file where it is, not a copy, with its textures (it reads the line `HELIX_DEBUG_LAUNCH=1` makes the app write to standard
error). It opens a window each time, so it cannot run in a sandbox:

```sh
python3 -I apple/Helix/Scripts/launch-smoke.py /path/to/Helix.app/Contents/MacOS/Helix
```

Limits for now: the server runs its own engine in its own process, so it works on its own scene,
not on the one open in a window of the app; its changes are not grouped into undoable steps with
an intent; and the scripts a node runs are not sandboxed. The three go with track two of the
[roadmap](ROADMAP.md).

## Themes

The app has five themes, chosen in the Settings window (Helix > Settings, or the command key and comma), in the Appearance tab. A click on a tile applies at once, in every open window, and the choice is kept in the user defaults under `theme`.

| Theme | Appearance | Accent |
| --- | --- | --- |
| System (default) | Follows macOS, light or dark | The system accent colour |
| Light | Forced light | The system accent colour |
| Dark | Forced dark | The system accent colour |
| Helix | Dark, deep indigo from the logo | Between violet and magenta (`#C063E0`) |
| Ember | Dark, warm charcoal | Ember orange (`#FF7A1A`) |

The palettes are in `apple/Helix/Sources/Theme/Theme.swift` (`ThemePalette`, a pure value with no HelixCore import, checked by `Tests/ThemeTests.swift` for WCAG contrast). `ThemeColors.swift` turns a palette into SwiftUI colours and puts it in the environment (`@Environment(\.theme)`).

What follows the theme: the appearance of the whole app (`NSApplication.appearance`, so title bars and AppKit controls too), the colour behind the windows, the tint of the controls, the viewport (background, geometry colour and the ground grid, through `ViewportFrameOptions`, redrawn when the theme changes), the node editor (canvas, dots, cards, ports, links, selection, display flag), the timeline (bar, ruler, range, playhead, cached frames, keys), the inspector accents and the spreadsheet background and stripes.

What follows the system, not the theme: the semantic reds and yellows of errors and warnings, the red, green and blue of the X, Y and Z fields, and AppKit's own drawing: the fill of text fields, the selection highlight of the spreadsheet table (the system accent colour) and menus. System, Light and Dark keep the semantic colours of macOS everywhere, so they look as before; the viewport is the same dark slate in all three, as it always was.

Every `--render-*` command takes `--theme <system|light|dark|helix|ember>` and `--appearance <light|dark>` (for the system theme), and `--render-settings <out.png> [--tab <appearance|general>]` draws a tab of the Settings window.

## Languages

The app is in English (the development language) and French, and follows the language of macOS: Foundation picks `en` or `fr` from the preferred languages of the system, and the bundle carries both (`en.lproj`, `fr.lproj`, with `fr` in the `knownRegions` of the project). Another system language gets English.

**The setting.** The Settings window has a General tab with **Language: System / English / Français**. The choice is the `AppleLanguages` override in the user defaults of the app, which is what Foundation reads as the app starts, so it applies at the next launch; the tab says so (« La langue sera appliquée au prochain lancement de Helix. ») and offers **Relaunch Now**, which opens a second instance and quits the first. *System* removes the override. The code is in `Sources/Localization/LanguageSetting.swift` (`AppLanguage`), checked by `Tests/LocalizationTests.swift`.

**`--language <en|fr>`.** Every command of the app takes it, the `--render-*` pictures and `--mcp` included, to run in a language for that run only, whatever the system and the Settings say. The program starts again with `-AppleLanguages (xx)` in front of its arguments (Foundation reads the language once), so nothing is written to the user defaults. `Helix --render-inspector Waves.kut wave1 out.png --language fr` draws the inspector in French; `--render-settings out.png --tab general` draws the language tab (with `--choice <system|en|fr>` to preview a choice that is not applied yet) and `--render-palette <query> out.png` draws the node palette with a query typed in it.

**Where the texts are.** Three string catalogs in `apple/Helix/Resources`, each with `en` and `fr`:

| Catalog | Holds | Maintained |
| --- | --- | --- |
| `Localizable.xcstrings` | The interface: menus, buttons, tooltips, alerts, panels, the names of edits in the Edit menu, errors the Swift side raises. The key is the English text (`%@` and `%lld` are the arguments). Counts use plural variations (« 1 nœud sélectionné » / « 3 nœuds sélectionnés »). | By hand |
| `Nodes.xcstrings` | What the engine gives a node in English, from its `node.yaml`: `node.<type>.label`, `node.<type>.input.<n>`, `param.<type>.<name>.label` (a parameter, group or divider), `.tooltip` and `.caption`, `option.<type>.<parameter>.<value>` and `tag.<tag>`. | `Scripts/extract-node-strings.py` writes the English; French is added in the catalog |
| `InfoPlist.xcstrings` | The texts of `Info.plist` a person sees: the names of the document types and of the imported formats, the copyright line. The key is the English text, or `NSHumanReadableCopyright`. | By hand |

The Swift side looks a node text up with `NodeText` (`String(localized: key, table: "Nodes")` underneath) and **falls back to the engine's English** when the key is missing, so a node that is added, or defined by a script, shows in English until it is translated. The palette searches both the translated and the English label and tags, and ignores accents (`camera` finds *Caméra*). Names a person gives (`cube1`) and the internal names of parameters are never translated.

**Adding or changing a translation.**

1. A new interface text: write it in Swift as `Text("…")`, `Button("…")`, `.help("…")` or `String(localized: "…")`, and add its key to `Localizable.xcstrings` with the French. A text that is chosen by a condition must be a `LocalizedStringKey("…")` on each side, since `Text(flag ? "a" : "b")` is a `String` and is never looked up.
2. A new node or parameter: run `python3 -I apple/Helix/Scripts/extract-node-strings.py` (standard library only; `--check` changes nothing and fails when the catalog is out of date or a key has no French), then write the French for the keys it lists. A key whose English changed keeps its French and is marked `needs_review`.
3. Run `python3 -I apple/Helix/Scripts/audit-strings.py`. It reads every Swift file and lists literals that would reach a person without a lookup (`missing-key`, `not-looked-up`, `ternary-string`, `suspicious`); `// l10n-ok` on the line accepts one that is meant to stay as written (« Français » in the language menu). It skips the MCP server and the `--render-*` commands. With `--stringsdata <DerivedData>` it compares the catalog with the keys the compiler extracted (build once with `SWIFT_EMIT_LOC_STRINGS=YES`).
4. `xcodebuild test -scheme HelixNodeEditorTests` runs `LocalizationTests`: every key of the three catalogs has a French value, with the same arguments as the English (same count, same order, or positional), French punctuation is preceded by a non-breaking space, `Info.plist` has an entry for every text it shows, and `Nodes.xcstrings` matches the node files.

**Conventions of the French.** The words are those of the Mac and of the 3D tools: *nœud*, *attribut*, *image clé*, *chronologie*, *tableur*, *inspecteur*, *vue 3D* (never « viewport »), *éditeur de nœuds*, *fil de fer*, *cadence d’images*. A non-breaking space (U+00A0) goes before `:`, `;`, `?`, `!` and inside « », the apostrophe is ’ and the ellipsis is …. Numbers the user types keep the dot (`0.5`), as expressions and the engine read them; counts in sentences use the locale (« 3 600 points »).

**What stays English.** The messages of the engine (the errors a node reports when it cooks, the warnings, the messages of a refused edit that the facade writes) are English: they also go to agents and to the logs, and they appear in the inspector's red banner and the card tooltips, framed by a translated « Erreur : … ». The MCP server (tool descriptions, results, errors) stays English, since it speaks to agents. The command line usage and the output of the `--render-*` commands are English. The `documentation` field of a parameter, which the interface does not show, is not translated.

## Importing 3D files

`helix::geometryImport` is the one node that reads other tools' files, and picks its reader by
extension:

| Extensions | Reader | Notes |
| --- | --- | --- |
| `.glb` `.gltf` | Assimp 6.0.6 | `KHR_draco_mesh_compression` through the Draco that Assimp carries. |
| `.fbx` `.stl` `.ply` `.dae` `.3ds` `.3mf` `.dxf` `.x3d` `.x` | Assimp | STL, 3MF and DXF are Z up by the convention of the format. |
| `.obj` | the engine's own reader | Points, faces and texture coordinates (`uv`). Normals are computed, as they always were. |
| `.usd` `.usda` `.usdc` `.usdz` | Model I/O | Quads come through as triangles. |
| `.blend` | Blender, in the background | Converted to GLB (modifiers applied except the armature one, with the skin and the actions, no pictures), and kept under `~/Library/Caches/<bundle id>/blend` (the app) or `HelixBlendCache` in the temporary folder, keyed by path, size, date and the version of Blender. The Blender is the one in the node's `Blender` setting, `/Applications/Blender.app`, or one Spotlight finds. |

Assimp is a static library built by the superbuild into `build-super/deps` with only those
importers and no exporters (`superbuild/deps/assimp.cmake`), and linked into `libhelixEngine`, so
the app bundle holds no extra library. The tests read the small files in `tests/data/import`,
which `tests/data/import/make-test-files.sh` writes again (Python 3, and Blender for the Draco,
FBX and BLEND files).

**Opening a 3D file.** File > Open, a double click and the Dock open a 3D file as a new untitled
scene. SwiftUI gives the document the bytes of the file, and gives the window its real URL; the
window imports from that URL once it is shown, so the `filePath` the scene saves is the original
path, and a `.gltf` finds the `.bin` and the pictures beside it. This is part of opening: it is no
step of undo and does not mark the scene as edited. The bytes are also copied to a temporary
folder, as a fallback for the rare file SwiftUI gives no URL for: then the copy is imported and
kept, the scene points to it, and the beside-files of a `.gltf` are lost. When the real URL is
used, the copy is deleted.

### Materials and textures

What comes through for each material: its name, its base colour factor (linear RGBA) and its base
colour texture (glTF `baseColorTexture`, the diffuse texture of the other formats, a USD
`UsdPreviewSurface` diffuse colour), decoded with ImageIO to 8 bit sRGB RGBA pixels, rows top to
bottom. The picture is embedded in the file (GLB, FBX, USDZ) or next to it (found relative to the
model, then by its file name). A picture over 8192 pixels on a side is scaled down. A picture that
is missing or damaged does not fail the import: the material keeps its factor, and the summary and
`geometry_summary` say which picture and why. The other maps (normal, roughness, metallic,
emissive, occlusion) are not read yet.

**In the viewport**: each face is shaded with its material's base colour factor times its base
colour texture (sampled as an sRGB texture, so in linear light, with mip levels, trilinear
filtering, anisotropy 8 and repeat wrapping), under the same lights as before. The faces are drawn
one batch per material, and a texture is made on the GPU once per picture and kept while the
geometry uses it. Toggle it in **View > Materials** (Shift+M, remembered between launches) or with
`render_view`'s `materials` argument; hidden, every face is the default grey and no texture is
made. The limits: base colour only, **opaque** (alpha is ignored), no normal, roughness or metallic
maps, no vertex colour, no texture transform. In Auto mode the wireframe is left out on a mesh that
has a texture, since the edges would cover the picture (On still draws them).

The mesh holds its materials as a table of shared, immutable `geo::Material` (see
`src/Engine/Primitives/Material.h`), and the face attribute `material_index` points into it, `-1`
meaning none. The table follows the mesh through the nodes, and `Mesh::merge` joins the tables of
two meshes and renumbers the faces it appends. The `uv` attribute has its origin at the bottom
left (v up), whatever the format; the picture is read from its top row, so the texel of `(u, v)`
is at row `(1 - v) * height`. `.kut` files do not store materials: the import node reads them again.
`geometry_summary` (and `GeometryReport::materials` in the facade) lists the materials once each.

`Helix --render-snapshot <file.glb> out.png` imports a 3D file into a new scene and prints how
long that took and the peak memory of the process. It reads the same settings as the window, and a
launch argument overrides one: `Helix -materialsVisible NO --render-snapshot ...` draws without
materials.

### Skeletal animation

A rigged file (glTF, GLB, and the other Assimp formats that carry a skin) comes in with its skeleton,
its skin weights and its clips. Skeletons are geometry: the joints are points, the bones are lines,
and the transforms are attributes, so the nodes that shape a mesh work on a skeleton too, and the
spreadsheet and `read_attribute` show it.

**What the import node gives.** `helix::geometryImport` has two outputs:

- **Output 0**, the mesh, with the skin on every point: the int attributes `skin_joint0` to
  `skin_joint3` (the index of a joint, which is a point of the skeleton) and the float attributes
  `skin_weight0` to `skin_weight3`. The weights of a point sum to 1; a vertex pulled by more than
  four joints keeps its four strongest and the rest is shared between them (the summary counts those
  vertices); an unused influence is joint 0 with weight 0; a point with no weight at all (a mesh of
  the file that is not skinned, merged with one that is) is not moved by the skin. The mesh is the
  bind pose, as it was.
- **Output 1**, the skeleton: a mesh with one point per joint (at its rest position, in the space of
  the mesh), one open two point face per bone (parent to child, so it draws as a line), and the point
  attributes `joint_parent` (int, `-1` for a root), `rest_local` (matrix, the rest transform relative
  to the parent), `inverse_bind` (matrix, the mesh space to the joint space) and `pose` (matrix, the
  current world transform of the joint, the rest one until a pose node sets it). It is an empty mesh
  when the file has no rig. The names of the joints and the clips are not attributes: they sit in a
  table the mesh shares by pointer, like the materials, which is not saved in a `.kut` file (the import
  node reads it again, as it does the materials).

The joints are collected once for the whole file, by node, and ordered depth first, so a parent
always comes before its children and every mesh of a merge indexes the same list. A node that is not
a bone but sits between two bones is kept as a joint, so the hierarchy is not cut; the nodes above
the first bone (the armature object of a Blender export) are not joints, and their transform is kept
as the base of the root, unless a clip animates one of them: that node, and those between it and the
bones, are then joints with no weight, so the clip moves the whole rig. Two meshes that skin to the
same node with different inverse bind matrices get one joint of that node per matrix (same name,
parent and rest pose, the clip tracks of the node on each), so each mesh is bound as its file says.
A file with several unrelated skins gives one skeleton with several roots.
The summary says the joints, the roots, the skinned meshes and every clip with its name and length.

**Conventions.** `inverse_bind` is what the file gives (the inverse bind matrix of a glTF, the
offset matrix of Assimp's bones) with the correction of the up axis undone, and `pose` has it applied,
so the skin matrix of a joint is `pose * inverse_bind` and is the identity when the skeleton stands in
the pose the mesh was bound in. The node that holds a skinned mesh does not place it: glTF says the
skeleton does, and Assimp folds the mesh transform into the offset matrices of the formats that have
one, so a skinned mesh is not baked with the transform of its node (an unskinned mesh is, as before).
A skeleton in a file whose node pose is not its bind pose shows the mesh deformed in the rest pose, as
the file says. The import's `Size` scales the joints with the mesh and the translations of the clips
and the root with them.

**`helix::skeletonPose`** takes a skeleton and gives it back posed, which is what the deform reads.

| Parameter | |
| --- | --- |
| `clip` | The clip to play, by name. Empty plays the first one. A name the skeleton does not have is a node error that lists the clips it has. |
| `speed` | A multiple of the scene time, default 1. 0 holds the pose of `offset`, a negative speed plays backward. |
| `offset` | Seconds added to the clip time, default 0. |
| `loop` | `loop` starts the clip over, `hold` stays on the first or last pose, `once` plays it one time and then returns to the rest pose. |

The clip time is `offset + time * speed`, with the scene time `(frame - 1) / fps` (see Timeline), so
the pose is a pure function of the frame, whatever order the frames are visited in. A joint the clip
does not animate keeps its rest transform, and a track that animates only some of translation,
rotation and scale replaces only those. Rotations take the shortest arc between keys, and a track is
linear or stepped as the file says. The output has the positions of the joints and the `pose`
attribute set. A skeleton with no clip passes through in its rest pose with a warning, and an empty
input (a file with no rig) passes through with a warning.

**`helix::skinDeform`** takes the skinned mesh (input 0) and the posed skeleton (input 1) and does
linear blend skinning: `P' = sum(w_i * pose_i * inverse_bind_i) * P` over the four influences, in
parallel over the points (about 0.8 ms for the 74 000 points of the dragon). The normals of the
points and vertices turn with the same blend and are brought back to unit length (the `Deform
Normals` toggle), and every other attribute and the material table stay. A mesh with no skin attribute
is a node error, a joint index outside the skeleton is a node error that names the point, and a mesh
of a packet that has no skin passes through with a warning.

**Wiring.** `geometryImport` output 0 to `skinDeform` input 0, `geometryImport` output 1 to
`skeletonPose`, `skeletonPose` to `skinDeform` input 1, and display the deform. To see the bones,
display the pose node. Through MCP, `connect_nodes` takes `output: 1` for the skeleton, and
`geometry_summary` of any node that produced a skeleton reports `skeleton: {joints, roots, clips:
[{name, duration, channels}]}`, which is how to find the names `clip` takes.

**Limits.** Cubic spline tracks are played as straight lines between their keys (the import says how
many). No blend shapes (morph targets), no inverse kinematics, no retargeting, and no blending between
clips. The skin is computed on the CPU (the GPU later). The skeleton and the clips are not saved in a
`.kut`. USD (Model I/O) skins are not read, and neither are the animations of an FBX (the skeleton and
skin are). The viewport draws the bones as thin lines in the line colour, which is dark on the
default background. A `Transform` node moves the joints as points but not their matrices, so move a
skeleton with the clips and the root, not with that node.

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
