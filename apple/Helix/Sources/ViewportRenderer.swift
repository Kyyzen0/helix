import HelixCore
import AppKit
import MetalKit

/// Presents the frames the engine draws: asks the facade to record a frame into a command buffer
/// of this queue, then copies the texture it returns onto the drawable of the view.
///
/// The copy is a full screen triangle rather than a blit, since the engine draws in RGBA and the
/// drawable is BGRA, which a blit does not convert.
@MainActor
final class ViewportRenderer: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let controller: ViewportController
    private var reportedFailure = false

    /// The palette the viewport draws with: the theme chosen in the Settings window, in the
    /// appearance of the view when the theme follows the system. Read at each frame, so a change
    /// of theme only needs a redraw.
    static func palette(for appearance: NSAppearance? = nil) -> ThemePalette {
        Theme.stored().palette(systemIsDark: (appearance ?? NSApplication.shared.effectiveAppearance).isDark)
    }

    /// The colours of the viewport: the palette of the theme on the background the View menu chose.
    static func colors(for appearance: NSAppearance? = nil) -> ViewportBackground.Colors {
        ViewportBackground.stored().colors(for: palette(for: appearance))
    }

    init?(device: MTLDevice, view: MTKView, controller: ViewportController) {
        guard let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary(),
              let vertex = library.makeFunction(name: "presentVertex"),
              let fragment = library.makeFunction(name: "presentFragment")
        else { return nil }

        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertex
        descriptor.fragmentFunction = fragment
        descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
        guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor) else { return nil }

        self.queue = queue
        self.pipeline = pipeline
        self.controller = controller
        super.init()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        view.needsDisplay = true
    }

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let pass = view.currentRenderPassDescriptor,
              let commandBuffer = queue.makeCommandBuffer()
        else { return }

        let size = view.drawableSize
        if size.width >= 1, size.height >= 1 {
            if controller.takePendingFrame() {
                _ = helix.core.frameViewport(Float(size.width / size.height))
            }
            // The camera may have moved since the last frame: a gesture, a view, a frame.
            controller.refreshCamera()
            if let texture = record(into: commandBuffer, view: view) {
                present(texture, with: pass, on: commandBuffer)
            }
        }

        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    /// Asks the engine to draw into the command buffer and returns the texture it drew into.
    private func record(into commandBuffer: MTLCommandBuffer, view: MTKView) -> MTLTexture? {
        var options = helix.core.ViewportFrameOptions()
        options.pixelWidth = UInt32(view.drawableSize.width)
        options.pixelHeight = UInt32(view.drawableSize.height)
        options.pixelRatio = Float(view.window?.backingScaleFactor ?? 1)
        Self.palette(for: view.effectiveAppearance).apply(to: &options)
        options.wireframe = WireframeSetting.current.facadeMode
        options.materialsVisible = MaterialsSetting.isVisible

        // The facade borrows the command buffer for the call and keeps nothing of it.
        let frame = helix.core.encodeViewport(Unmanaged.passUnretained(commandBuffer as AnyObject).toOpaque(), options)
        guard frame.status.ok, let pointer = frame.colorTexture else {
            if !frame.status.ok { reportFailure(String(engineMessage: frame.status.error)) }
            return nil
        }

        // The facade owns the texture. Taking a strong reference here makes the command buffer
        // hold it until the copy below has run.
        return Unmanaged<AnyObject>.fromOpaque(pointer).takeUnretainedValue() as? MTLTexture
    }

    private func present(_ texture: MTLTexture, with pass: MTLRenderPassDescriptor, on commandBuffer: MTLCommandBuffer) {
        guard let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: pass) else { return }
        encoder.setRenderPipelineState(pipeline)
        encoder.setFragmentTexture(texture, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
    }

    private func reportFailure(_ message: String) {
        guard !reportedFailure else { return }
        reportedFailure = true
        controller.onFailure?(message)
    }
}

extension ThemePalette {
    /// Writes the colours of the viewport into the options of a frame: the background and the
    /// colour of the geometry, and the grid lines. The background is the one of the View menu
    /// unless a command asks for another.
    func apply(to options: inout helix.core.ViewportFrameOptions, background: ViewportBackground = .stored()) {
        let colors = background.colors(for: self)
        options.backgroundRed = Float(colors.background.red)
        options.backgroundGreen = Float(colors.background.green)
        options.backgroundBlue = Float(colors.background.blue)
        options.geometryRed = Float(colors.geometry.red)
        options.geometryGreen = Float(colors.geometry.green)
        options.geometryBlue = Float(colors.geometry.blue)
        options.gridRed = Float(colors.grid.red)
        options.gridGreen = Float(colors.grid.green)
        options.gridBlue = Float(colors.grid.blue)
    }

    /// The colour the Metal view is cleared to.
    var viewportClearColor: MTLClearColor {
        ViewportBackground.stored().colors(for: self).background.clearColor
    }
}

extension RGB {
    var clearColor: MTLClearColor { MTLClearColor(red: red, green: green, blue: blue, alpha: 1) }
}
