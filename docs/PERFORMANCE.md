# Performance architecture for the macOS build

This document states what "fast on a weak machine" means for this project,
and which lever addresses which part of it. It is the reference every change
is measured against.

## The target

The reference machine for this work is an Apple M1, but the goal is a build
that stays usable on hardware that is several years older or much smaller:
an Intel Mac with integrated graphics, 8 GB of RAM, and a thermal budget that
forces the clock down within a minute of sustained load.

Concretely, the numbers to hold on a modest machine:

| Measure | Target |
| --- | --- |
| Time to first interactive frame | under 2 s |
| Interactive latency after a parameter change | under 100 ms |
| Viewport frame time, 500 k triangles | under 16 ms |
| Peak resident memory, 1 M point scene | under 600 MB |
| Idle CPU while nothing changes | under 1 % |

A machine that cannot hold the frame budget must stay usable rather than
become unusable: degrading resolution is acceptable, dropping the interface
is not.

## Where the time actually goes

Three costs dominate, and they need different answers.

### 1. The engine recomputes everything

Cooking a network walks the node graph and rebuilds the geometry. Done
synchronously on the UI thread, every parameter tweak freezes the interface
for the whole duration of the cook. This is the single largest source of
perceived slowness, and it is also the one that hurts weak machines hardest,
because their cook takes proportionally longer.

The answer is asynchronous cooking: the network cooks on a worker, the
interface stays live, and the viewport shows the previous result until the new
one is ready. This is owned by engine-async.

### 2. The renderer translates every call

macOS has no native Vulkan driver. Every Vulkan call goes through MoltenVK,
which translates it into Metal. That translation is not free, and it applies
to the draw calls that happen every frame.

The backend itself cannot be replaced. DiligentCore ships a
`GraphicsEngineMetal` directory, but it contains an empty interface and a
readme; there is no implementation behind it, on the pinned commit or on
master. Porting the renderer to Metal directly would mean writing a backend,
which is a project of its own.

What can be done is to stop paying the translation cost per frame:
rebuild the command buffers that are re-recorded unchanged, reuse the
resource bindings between frames, and avoid rebuilding pipeline state when
only the data changed. This is owned by graphics-vulkan.

### 3. The build is compiled for everyone

A default build targets a generic CPU, so the compiler emits code that runs
everywhere, including on machines much slower than the one in use. On Apple
Silicon the difference between a generic build and one tuned for the
installed core is substantial.

The answer is a release build that is tuned for the machine it runs on:
optimisation and link-time optimisation enabled, the architecture of the
target CPU selected at configure time, and no debug information in the
shipped binary. This is owned by build-macos.

## Rules that follow from this

- **Measure before claiming.** A performance change is done when a number
  moved, not when the code looks better.
- **The reference machine is not the target.** Numbers taken on an M1 are the
  ceiling; the floor is what a weak machine manages.
- **Never trade correctness for speed.** Geometry that is wrong is not faster,
  it is broken.
- **Prefer removing work to doing it faster.** A frame that is not drawn
  costs nothing.

## How the work is divided

| Area | Owner | Scope |
| --- | --- | --- |
| Build system, toolchain, release tuning | build-macos | `CMakeLists.txt`, `cmake/`, `superbuild/`, `scripts/`, packaging |
| Async cooking, task scheduling | engine-async | `src/Engine/Network/`, `src/Engine/Core/` |
| Renderer, viewport, GPU resources | graphics-vulkan | `src/Graphics/` |
| Interface, responsiveness | gui-rewrite | `src/Gui/` |
| Timeline, simulation nodes | timeline-sim | `src/Engine/Simulation/`, timeline code |
| Node authoring, user nodes | nodes-authoring | `nodes/`, node loading |

Each owner keeps to its own files, so the branches merge cleanly.

## The state of this build

The macOS toolchain is in place and verified:

- CMake 3.31.6 and Ninja 1.12.1 under `toolchain/`
- Qt 6.10.3, universal binary, arm64 and x86_64 in one package
- MoltenVK 1.4.2 as the Vulkan driver
- Bison 3.8.2 and flex, which the system copies are too old to replace

Two problems of this machine are worth recording, because they look like code
faults and are not:

- The `MacOSX.sdk` that the command line tools point at is missing the
  system libraries, so linking fails with `library 'System' not found`.
  `toolchain/env.sh` points the build at the complete SDK instead.
- The build tree cannot live inside the repository, because CMake has to
  delete a scratch directory while configuring and the sandbox forbids
  removing files. `scripts/configure-macos.sh` puts the build tree outside
  the repository and links it back.

`source toolchain/env.sh` is required before any build.
