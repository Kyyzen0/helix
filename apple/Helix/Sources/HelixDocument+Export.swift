import AppKit
import CxxStdlib
import Foundation
import HelixCore
import SwiftUI
import UniformTypeIdentifiers

/// Taking the work out of the app: a picture of the viewport and the geometry of the display node.
extension HelixDocument {
    /// Whether the viewport has anything to export: a node is displayed and made points.
    var canExport: Bool { isActive && pointCount > 0 && viewportMessage == nil }

    /// File ▸ Export Image…: a PNG of the viewport, from the camera as it is now, in the colours it
    /// is drawn in (the background of the View menu included). The picture is opaque: the engine's
    /// PNG writer leaves the alpha out, so a transparent background is not offered.
    func exportImage() {
        guard canExport else { return }
        let choice = ExportImageChoice()
        let panel = NSSavePanel()
        panel.title = String(localized: "Export Image")
        panel.prompt = String(localized: "Export")
        panel.allowedContentTypes = [.png]
        panel.canCreateDirectories = true
        panel.nameFieldStringValue = ExportNaming.suggestedName(title: displayTitle, fallback: String(localized: "Image"), extension: "png")
        let accessory = NSHostingView(rootView: ExportImageAccessory(choice: choice))
        accessory.frame.size = accessory.fittingSize
        panel.accessoryView = accessory
        runPanel(panel) { [self] url in writeImage(to: url, size: choice.size) }
    }

    /// File ▸ Export Geometry…: the geometry of the display node as a Wavefront obj file.
    func exportGeometry() {
        guard canExport else { return }
        let panel = NSSavePanel()
        panel.title = String(localized: "Export Geometry")
        panel.prompt = String(localized: "Export")
        panel.message = String(localized: "Writes the geometry of the node shown in the viewport as an OBJ file.")
        if let obj = UTType(filenameExtension: "obj") { panel.allowedContentTypes = [obj] }
        panel.canCreateDirectories = true
        panel.nameFieldStringValue = ExportNaming.suggestedName(title: displayTitle, fallback: String(localized: "Geometry"), extension: "obj")
        runPanel(panel) { [self] url in
            let result = helix.core.exportGeometry(std.string(url.path))
            if !result.status.ok { presentedError = String(engineMessage: result.status.error) }
        }
    }

    private func runPanel(_ panel: NSSavePanel, then write: @escaping @MainActor (URL) -> Void) {
        let finish: @MainActor (NSApplication.ModalResponse) -> Void = { response in
            guard response == .OK, let url = panel.url else { return }
            write(url)
        }
        if let window {
            panel.beginSheetModal(for: window) { response in MainActor.assumeIsolated { finish(response) } }
        } else {
            finish(panel.runModal())
        }
    }

    /// Draws the viewport at a size, through the camera of the viewport, into a PNG.
    func writeImage(to url: URL, size choice: ExportImageSize) {
        let view = viewport.view
        let drawable = view?.drawableSize ?? CGSize(width: 1280, height: 720)
        let pixels = choice.pixels(viewport: drawable)
        var options = helix.core.ViewportFrameOptions()
        options.pixelWidth = UInt32(pixels.width)
        options.pixelHeight = UInt32(pixels.height)
        // The lines keep their weight against the picture: a picture twice as wide as the viewport
        // draws them twice as thick.
        let scale = Float(view?.window?.backingScaleFactor ?? 2)
        options.pixelRatio = max(1, Float(pixels.width) / max(Float(drawable.width), 1) * scale)
        ViewportRenderer.palette(for: view?.effectiveAppearance).apply(to: &options)
        options.wireframe = WireframeSetting.current.facadeMode
        options.materialsVisible = MaterialsSetting.isVisible
        let status = helix.core.renderToImage(std.string(url.path), options, true)
        if !status.ok { presentedError = String(engineMessage: status.error) }
    }
}
