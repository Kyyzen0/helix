import SwiftUI

/// The smallest a window of a scene gets.
enum WindowSize {
    static let minimum = CGSize(width: 1100, height: 720)
}

extension WindowPlacement {
    /// The size of a new window: 85 percent of the visible part of the screen, never smaller
    /// than the smallest a window gets, and never larger than the screen.
    static func helixSize(in visible: CGRect) -> CGSize {
        CGSize(
            width: min(max((visible.width * 0.85).rounded(), WindowSize.minimum.width), visible.width),
            height: min(max((visible.height * 0.85).rounded(), WindowSize.minimum.height), visible.height)
        )
    }

    /// A window at that size, centred in the visible part of the screen.
    static func helixDefault(in visible: CGRect) -> WindowPlacement {
        let size = helixSize(in: visible)
        return WindowPlacement(
            CGPoint(x: visible.midX - size.width / 2, y: visible.midY - size.height / 2),
            size: size
        )
    }
}
