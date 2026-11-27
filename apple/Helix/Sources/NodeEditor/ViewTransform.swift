import CoreGraphics

/// How the canvas, where nodes have positions, shows in the view, where the pointer is.
///
/// A canvas point maps to `canvas * zoom + offset`. Both spaces have y growing downward, so the
/// view of the editor is flipped and nothing here turns an axis around.
struct ViewTransform: Equatable, Sendable {
    static let minZoom: CGFloat = 0.1
    static let maxZoom: CGFloat = 5

    /// Where the origin of the canvas is, in the view.
    var offset = CGPoint.zero
    var zoom: CGFloat = 1

    func viewPoint(_ canvas: CGPoint) -> CGPoint {
        CGPoint(x: canvas.x * zoom + offset.x, y: canvas.y * zoom + offset.y)
    }

    func canvasPoint(_ view: CGPoint) -> CGPoint {
        CGPoint(x: (view.x - offset.x) / zoom, y: (view.y - offset.y) / zoom)
    }

    func viewRect(_ canvas: CGRect) -> CGRect {
        CGRect(origin: viewPoint(canvas.origin), size: CGSize(width: canvas.width * zoom, height: canvas.height * zoom))
    }

    func canvasRect(_ view: CGRect) -> CGRect {
        CGRect(origin: canvasPoint(view.origin), size: CGSize(width: view.width / zoom, height: view.height / zoom))
    }

    func panned(by delta: CGPoint) -> ViewTransform {
        ViewTransform(offset: CGPoint(x: offset.x + delta.x, y: offset.y + delta.y), zoom: zoom)
    }

    /// Zooms by a factor, keeping the canvas point under `viewPoint` where it is, so the
    /// content grows and shrinks around the pointer. The zoom stays between the two limits.
    func zoomed(by factor: CGFloat, around viewPoint: CGPoint) -> ViewTransform {
        guard factor.isFinite, factor > 0 else { return self }
        let target = min(max(zoom * factor, Self.minZoom), Self.maxZoom)
        let anchor = canvasPoint(viewPoint)
        return ViewTransform(
            offset: CGPoint(x: viewPoint.x - anchor.x * target, y: viewPoint.y - anchor.y * target),
            zoom: target
        )
    }

    /// The transform that shows a rectangle of the canvas whole and centred in a view, with a
    /// margin around it. It does not zoom in past `maxFramedZoom`, so one small node does not
    /// fill the view.
    static func framing(
        _ rect: CGRect,
        in size: CGSize,
        margin: CGFloat = 48,
        maxFramedZoom: CGFloat = 1.5
    ) -> ViewTransform {
        guard size.width > 0, size.height > 0 else { return ViewTransform() }
        let availableWidth = max(size.width - 2 * margin, 1)
        let availableHeight = max(size.height - 2 * margin, 1)
        let fit = min(availableWidth / max(rect.width, 1), availableHeight / max(rect.height, 1))
        let zoom = min(max(fit, minZoom), min(maxFramedZoom, maxZoom))
        return ViewTransform(
            offset: CGPoint(
                x: size.width / 2 - rect.midX * zoom,
                y: size.height / 2 - rect.midY * zoom
            ),
            zoom: zoom
        )
    }
}
