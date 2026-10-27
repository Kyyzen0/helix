# The Metal renderer

The viewport is drawn with Metal directly, through Apple's `metal-cpp` headers, and shown in
the Qt Quick interface through its Metal scene graph. This replaces the DiligentCore, Vulkan and
MoltenVK stack. The engine and the cooking code did not change.

## How a frame flows

`ViewportItem` (GUI thread) turns its properties into a `gfx::FrameState` and hands it to its
`ViewportNode` in `updatePaintNode`. On the render thread:

1. `ViewportNode::sync` makes the colour texture fit the item (`ViewportRenderer::prepareTarget`),
   wraps it for the scene graph, and keeps the frame as the pending one.
2. `QQuickWindow::beforeRendering` fires `ViewportNode::render`. If the pending frame differs from
   the one the texture holds, `ViewportRenderer::encode` records one render pass into the
   `MTLCommandBuffer` the scene graph is recording its own frame into (between
   `beginExternalCommands` and `endExternalCommands`).
3. The scene graph then draws its quads, among them the one sampling the texture. Both sit on one
   command buffer of one queue, so Metal orders the draw before the sampling with no event, fence
   or wait, and the viewport adds no latency.

A frame is: clear colour and depth, then grid, mesh (depth prepass, shaded faces, open faces,
optional wireframe), points, cameras. This is the order and the state of the old passes. Four
samples are used when the GPU offers them and are resolved into the shown texture as the pass
ends; the multisampled colour and depth are memoryless on Apple GPUs.

## Class layout

| Class | Role |
| --- | --- |
| `gfx::GraphicsDevice` | Wraps an `MTL::Device` (the scene graph's, or the system default), loads `helix.metallib`, hands out shader functions and a lazily made command queue. Several can exist. |
| `gfx::ViewportRenderer` | Owns the four passes and the targets. `render` (own queue, waits), `renderToPixels` (adds a read back), `encode` (into a host command buffer), `prepareTarget`. Pipelines are built once; geometry is uploaded only when the frame's geometry pointer changes. Resources it replaces (geometry buffers, targets) are handed to the command buffer of the next recorded frame, see "Lifetime of GPU resources". |
| `GridPass`, `MeshPass`, `PointPass`, `CameraPrimPass` | Same split as before, each building its `MTL::RenderPipelineState`s and drawing with the frame's encoder. `PassSetup` holds the shared helpers. |
| `Shaders/*.metal`, `ShaderTypes.h` | The shaders and the `FrameConstants` struct, shared by C++ and MSL so the layout cannot drift. |
| `MetalImplementation.cpp` | The one file that defines `NS_/MTL_/CA_PRIVATE_IMPLEMENTATION`. |
| `ui::QtGraphicsBridge` (`.mm`) | The Qt specifics: select Metal, get the device and command buffer, wrap an `MTLTexture`. |
| `ui::ViewportNode` | Creates the device and renderer on the scene graph's device, draws in `beforeRendering`, releases on `sceneGraphInvalidated`. |

Kept stable for the GUI: `FrameState`, `ViewportItem`, `ViewportViewModel`, `Viewport.qml` are
unchanged. `ViewportRenderer::render` returns a `RenderedImage` (a Metal texture) instead of a
`VulkanImage`, and `renderToPixels` still returns a `PresentedFrame`.

## Conventions carried over from HLSL

Clip space is `z` in 0..1 with `y` up, which Metal and `glm::perspectiveRH_ZO` both use, and the
framebuffer origin is top left in both, so the matrices are used as they are. The matrix is column
major on both sides, so `viewProjection * v` replaces `mul(viewProjection, v)`. Culling is off,
so winding does not matter. The depth bias of the faces is 1 unit and 1 slope, set on the encoder
(Metal keeps it there, not in the pipeline). The only shader change is that `Mesh` no longer
passes the unused world position to the fragment stage.

## What goes, what stays

Gone: `Vulkan*.h`, `DiligentObjects.h`, the HLSL shaders and their embedding, DiligentCore, MoltenVK,
the Vulkan loader, `install-vulkan-loader.sh`, the single device per process rule, device-lost
bookkeeping (Metal on Apple silicon has no device loss event to react to). Stays: the engine, cooking,
`DisplayGeometry`, `ViewportCamera`.

## Building and shader loading

`xcrun metal` and `xcrun metallib` build `helix.metallib` into `helix.app/Contents/Resources`
(`CMakeLists.txt`); the install copies it into the installed bundle. At run time it is looked for at
the path given to the constructor, `HELIX_METALLIB`, `../Resources` next to the executable, then the
build tree. Without any, a development build compiles the `.metal` sources with the Metal runtime
(`HELIX_METAL_SHADER_DIR`, or the source tree), which is how the renderer tests were run where the
offline compiler was missing. `metal-cpp` is pinned in `cmake/versions.cmake`.

## Lifetime of GPU resources

Qt's Metal backend creates its command buffers with `commandBufferWithUnretainedReferences`, so a
command buffer that `encode` recorded into does not retain the buffers and textures it reads: they
must outlive the GPU work by other means.

- Pipeline and depth states, the grid, the disc corners and the camera frame live as long as the
  renderer and are never replaced.
- What is replaced while a frame may still read it is not released at once. `MeshPass`,
  `PointPass` and `CameraPrimPass` `upload` move each replaced buffer into a `RetiredResources`
  list instead of dropping it (`replaceGpuArray`), and `prepareTarget` does the same with the
  textures of a resized target set.
- At the end of `encode`, the renderer moves that list into a completed handler of the command
  buffer it recorded (`addCompletedHandler`). The handler owns the resources and releases them on
  the thread Metal calls it from, once the GPU has finished that buffer. Queue order makes the
  frames recorded before it finish first, so this is exact, not a count of frames. The handler
  touches no member of the renderer (only a shared counter, `retiredResourceCount`, for tests).
- `render` and `renderToPixels` go through `encode` and then wait, so their handler has run (or
  is about to) when they return.
- The retired resources wait for the next `encode`. A renderer that records no further frame keeps
  them until it is destroyed.
- Not covered: destroying the renderer while a frame recorded with it is still on the GPU. The host
  must have finished its frames first (`ViewportNode` releases on `sceneGraphInvalidated`).

## Assumptions about Qt 6.10 that a Mac must confirm

Qt was not installed where this was written; `QtGraphicsBridge.mm` and `ViewportNode.cpp` are written
from the documentation and were only syntax checked against stand-in headers.

1. `QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal)` (the enum is `Metal`, not the Qt 5
   name `MetalRhi`).
2. `QSGRendererInterface::getResource(window, DeviceResource)` returns the `id<MTLDevice>` and
   `CommandListResource` returns the `id<MTLCommandBuffer>` during `beforeRendering`.
3. `window->beginExternalCommands()` / `endExternalCommands()` are valid there and leave the scene
   graph's own pass untouched.
4. `QNativeInterface::QSGMetalTexture::fromNative(id<MTLTexture>, QQuickWindow*, QSize, options)` from
   `<QtQuick/qsgtexture_platform.h>` (Qt 6.4+). The older spelling is
   `window->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture, &texture, 0, size)`; if
   the first does not compile, swap that one call.
5. The wrapped texture is not retained by Qt, and Qt 6.10.3 creates its command buffers with unretained references, so a command buffer does not keep what it reads alive either; see "Lifetime of GPU resources".
6. Our texture is RGBA8 with straight alpha only if the background alpha is below 1; Qt expects
   premultiplied alpha, so a translucent background may look off.

## Manual checklist (on the Mac)

1. `xcodebuild -downloadComponent MetalToolchain` if `xcrun -sdk macosx metal --version` fails, then
   configure and build as in `build-macos.md`; confirm `helix.app/Contents/Resources/helix.metallib`.
2. Run `helix.app/Contents/bin/tests`: the renderer tests run, none skip,
   with `MTL_DEBUG_LAYER=1` set no Metal validation message appears.
3. Open the app with a scene. The viewport shows the dark background and the grid fading with
   distance. A mesh shows shaded with a darker wireframe (`W` toggles it). Mesh points show as white
   discs; camera primitives as orange frames.
4. Orbit, pan and dolly with the mouse: movement is smooth and the image never lags a frame behind.
5. Resize the window and the viewport panel, drag the splitter, move the window to a display with
   another scale: no stretch, no flash, no black frame; the image is sharp.
6. Pick a camera in the HUD: the view looks through it and its own frame disappears.
7. Quit and reopen several times, and close the window with the viewport shown: the process exits at once
   with status 0 and no crash report. Run under `leaks --atExit` or Instruments: nothing leaks.
8. On a machine without Metal (or a CI runner), the app prints `No Metal device available` and exits 1.
