# Helix roadmap

Helix is a procedural modeling tool **and** an animation tool. The two are not
separate products: a procedural model that cannot be animated is a static asset
generator, and an animation system that cannot be authored procedurally is a
keyframe editor with extra steps. The same graph, the same attributes and the
same cook serve both.

This document states where the work goes, and why in that order. It is the
reference the roadmap is measured against.

## Where the project comes from

Helix is a fork of [enzo](https://github.com/parkerbritt/enzo) by
[ParkerBritt](https://github.com/ParkerBritt), MIT licensed. The engine
architecture, the node system, the attribute based data model and the parameter
system are his work and remain the foundation everything below is built on. See
[NOTICE](NOTICE).

---

## The shared foundation

Neither track can go far without these. They are the reason the two tracks are
compatible rather than competing.

### Keyframes and F-curves

The upstream project has no keyframe system at all. The timeline exposes the
current frame, but a parameter cannot hold a curve yet, so a procedural model
cannot be animated at all.

This is the single missing piece that the animation track is built on, and the
modeling track needs it too, because a modeled asset that will be animated
later has to be authored with that in mind.

What it needs:

- a parameter holds keyframes, each with a time, a value and an interpolation
- interpolation modes: stepped, linear, bezier, and the usual easing set
- an editable curve, with handles, in the timeline
- evaluation of the whole graph at a given frame, resolved through the same
  attribute context everything else uses

The engine already exposes time to scripts and expressions: the daslang
builtins `frame()` and `time()` record a time dependency through
`ExpressionContext::recordTimeDependency()`, and the graph carries a time
dependency on top of that. The keyframe system is built on those, not beside
them.

### Cook caching and dirty propagation

Async cooking made the cook interruptible and parallel. The next step is to
stop cooking what did not change.

- hash the inputs of a node, skip the subtree when the hash matches
- propagate dirtiness downstream from the edited node
- keep the last completed result visible while a new one computes

This pays for both tracks at once: it is what makes a heavy procedural scene
feel interactive, and it is what makes scrubbing a timeline cheap.

### Attributes stay the common currency

The upstream idea is a unified context where everything is primitives and
attributes. UV coordinates, textures, simulation outputs and animated
parameters are all attributes, so they all flow through the same spreadsheet,
the same selection and the same evaluation.

A modeling track that adds UVs as a special case, and an animation track that
adds curves as a special case, would each break that idea. Neither should.

---

## Track one, procedural modeling

### 1. UV coordinates, textures and materials

The engine has no notion of any of the three today: there is no `texcoord`,
no `uv` and no texture in `src/Engine/Primitives/` or `src/Engine/Attribute/`.
For a tool aimed at procedural modeling, that is the largest single gap.

- `uv` as an attribute of the mesh, with a per-corner or per-point layout
- a texture and a material model on top of the attribute system
- unwrapping nodes, and a procedural unwrap that follows the graph

### 2. Point clouds as a primitive

The renderer already has a `PointPass`, so points can be drawn. The engine
simply has no point cloud primitive for them to live in.

### 3. Interchange with the VFX pipeline

Helix aims at VFX, where the interchange formats are USD, Alembic and glTF.
Exporting a procedural model is what turns a generator into a pipeline tool.
Importing a USD assembly is what lets a procedural model live inside a shot.

### 4. A graph that can express more

A static graph is limited. What a procedural tool eventually needs:

- **node templates**, a reusable subgraph with its own exposed parameters
- **subgraph instancing**, so a shape built once is reused
- **loops** and **conditionals**, so the graph is a program and not just a
  wiring diagram

### 5. Rendering and shading

Helix renders nothing. A path tracer, or at least a physically based preview
renderer, closes the loop between generating a model and seeing it. A shading
language in the spirit of OSL would make the material programmable the same way
the graph is.

---

## Track two, animation

### 1. F-curves and a curve editor

The keyframe system above is the data. The curve editor is the interface, and
it is where animation actually becomes craft: handles, tangents, ease in and
ease out, a graph view next to the timeline.

### 2. A simulation cache

The simulation nodes integrate over time, so evaluating frame 250 from scratch
means two hundred and fifty steps. A disk cache, in the spirit of Alembic,
turns scrubbing into a lookup and makes a long simulation interactive.

### 3. GPU cooking

TBB is the parallel backend today. Heavy geometry work belongs on the GPU, and
Metal exposes compute shaders, and the renderer already loads a Metal library. Moving the
expensive attribute operations to compute shaders is the single biggest performance lever left.

### 4. A growing simulation family

The first solver nodes are particles and springs. A useful library grows from
there: cloth, soft bodies, fluids, and crowd or particle systems that read the
same attributes the rest of the engine uses.

### 5. A stable scripting API

The engine is already decoupled from the interface, which makes a headless
mode nearly free. A stable API over that, for batch work and automation, is
what turns a tool into a component of a pipeline.

---

## Cross cutting

### ~~Metal directly, instead of Vulkan through MoltenVK~~

Done: the viewport is drawn with Metal through `metal-cpp`, and the Vulkan,
DiligentCore and MoltenVK stack is gone. See `docs/METAL-RENDERER.md`.

### A performance program

`docs/PERFORMANCE.md` already states the targets: first interactive frame
under 2 s, interactive latency under 100 ms, viewport frame time under 16 ms
for 500 k triangles. Those numbers are the acceptance criteria for the work
above, not a separate project.

### Quality of life

- a command palette, searching nodes, parameters and actions at once
- documentation and tutorials, which the project has wanted from the start
- versioned scene files, before scenes exist in the wild
- ~~versioning the node type namespace~~ done: nodes are published as `helix::<name>`, and
  scenes storing the old `enzo::<name>` still load (`nt::normalizeTypeName`, removable once
  old scenes are no longer supported)

---

## Order of work

The foundation first, because both tracks are blocked on it:

1. keyframes and F-curves
2. cook caching and dirty propagation
3. the two tracks in parallel, each taking the items its own order lists
4. ~~Metal directly~~ done
5. interchange and rendering, which are what make the tool usable outside its
   own ecosystem

## Status

In flight at the time of writing: the macOS port, the async cooking scheduler,
the timeline, the first simulation nodes, the sweep node, user node authoring
and the interface redesign. Everything else above is planned.

The project is a work in progress, and so is the application it is based on.
Expect rough edges, and expect the interface to still be moving.
