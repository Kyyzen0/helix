# Helix vision

Helix is a native Apple application where a person and an AI create 3D, 2D,
animation and simulation with the same tools, on an ordinary Mac.

This document states what Helix is for and the principles every decision is
measured against. [ROADMAP.md](ROADMAP.md) states the order the work is done
in. When the two disagree, this document wins and the roadmap is updated.

---

## What Helix is not trying to be

Helix does not try to match Blender feature for feature. Blender is decades of
work by hundreds of people, and a tool that chases it on every front loses on
every front.

Helix aims to be better on the axes where general purpose 3D tools are weak:
an AI that can create as well as a person, one procedural graph for every kind
of work, stability by design, and full use of the Mac it runs on. Everything
Helix does not do itself, it exchanges with other tools through open formats.

---

## The five pillars

### 1. AI native

Every action a person can take in Helix is a call on one API, and an AI can
make the same calls. The interface is one client of that API among others,
not the owner of the data.

- **One API for people and machines.** The `HelixCore` facade is the boundary.
  The Swift interface, the scripting layer and the AI all go through it. A
  feature that exists only in the interface is a defect.
- **A built in MCP server.** Helix exposes the facade through the Model Context
  Protocol, so an AI agent can create, inspect and change a scene without
  screen scraping or fragile scripts.
- **A scene is a readable program.** A procedural graph is a recipe, not the
  frozen result of thousands of clicks. An AI can write it, read it back,
  change one step and explain what changed. Scene files stay a readable,
  diffable format.
- **The AI can check its own work.** Cooking is deterministic and the result
  can be queried as data (attributes, counts, bounds) and as images (viewport
  captures, renders), so an agent verifies what it made instead of guessing.
- **Safe to hand over.** An AI's changes are grouped into undoable steps with
  a stated intent, scripts run sandboxed, and nothing an agent does can lose
  the person's work.

### 2. Everything is a graph

3D modeling, 2D (curves, vector shapes, text), animation, simulation and image
compositing are all nodes in one graph, flowing through one data model.

- **Attributes are the common currency.** Positions, UVs, skin weights,
  colours, simulation state and animated values are all attributes on
  geometry, so every node works on every kind of data.
- **Time is a parameter of the graph.** Animation is the graph evaluated at a
  frame, and every evaluation is a pure function of that frame: scrubbing,
  jumping and rendering out of order give the same result as playing forward.
- **Skeletons are geometry.** Joints are points and bones are lines, so the
  same nodes that shape a mesh also pose a character.
- **2D is first class.** Curves, shapes and text are geometry like any other,
  and image operations run on the GPU as nodes of the same graph.

### 3. Stable by design

A creative tool that loses work is worse than a slow one. Stability comes from
the architecture, not from fixing crashes one by one.

- **The work is never lost.** Saves are atomic, documents autosave and keep
  versions, and a reopened scene is exactly the scene that was saved.
- **A failing node does not take the app down.** Cooking is isolated from the
  interface; a node that crashes or hangs is reported on that node, and the
  rest of the scene keeps working.
- **Determinism is tested.** The engine is covered by tests that run on every
  change, including the GPU paths, and a result that depends on the order of
  evaluation is a bug.
- **No silent failure.** An operation that can fail reports why, at the point
  where a person or an agent can act on it.

### 4. Made for Apple silicon

Helix is designed for the Mac a person actually owns. A Mac mini M4 is the
reference machine: everything Helix offers must run well on it.

- **Metal everywhere.** The viewport, heavy geometry work (compute shaders) and
  rendering (hardware ray tracing on chips that have it) all use Metal
  directly.
- **Unified memory without copies.** The CPU and the GPU share memory on Apple
  silicon; data moves between them by reference, not by copy.
- **A memory budget, not a memory wish.** Helix computes on demand, caches to
  disk, streams large data, and stays responsive within the memory of a
  base model machine.
- **The right cores for the right work.** Interactive work runs on the
  performance cores at a high quality of service; background work yields to
  it.
- **Measured, not assumed.** Performance targets are numbers checked on the
  reference machine, and a change that regresses them does not ship.

### 5. Native to the Mac

Helix behaves like a Mac application because it is one.

- **Swift and SwiftUI for the interface**, AppKit where SwiftUI falls short,
  over the C++ engine.
- **Documents, menus, shortcuts, the trackpad, dark mode, full screen and
  window tabs** work the way every Mac user expects.
- **Distributed like a Mac app**: signed, notarized, opened by double clicking
  a scene.

---

## Openness

- **Exchange before reinvention.** glTF and USD in and out come early, so Helix
  is useful next to Blender and other tools long before it replaces any of
  them.
- **Open source.** Helix is MIT licensed, a continuation of
  [enzo](https://github.com/parkerbritt/enzo) by ParkerBritt, whose engine,
  node system and attribute model remain the foundation. See [NOTICE](NOTICE).

---

## How a decision is checked

Before a feature is built, it answers five questions:

1. Can an AI do it through the API, and check the result?
2. Is it nodes and attributes, or a special case beside the graph?
3. What happens to the person's work if it fails?
4. Does it run well on a Mac mini M4?
5. Does it behave like a Mac application?

A feature that cannot answer one of them is redesigned before it is built.
