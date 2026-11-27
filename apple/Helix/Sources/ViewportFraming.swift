import Foundation

/// When the camera of the viewport moves to frame the geometry.
///
/// The camera is the person's: an edit (a slider, a link, a new node) must not take it back, or a
/// change of size could never be seen, since the object would be framed to the same size on
/// screen every time. It frames in four cases: a scene that is opened, a file that is imported or
/// an example that is loaded (those ask for it), the geometry that goes from nothing to something
/// (the first node made, a failure that is fixed), and the Frame command. Everything else leaves
/// the camera where it was.
///
/// A plain value with no engine behind it, so the rule is checked in the tests.
struct FramingPolicy: Equatable {
    /// Whether the viewport showed geometry at the last cook.
    private(set) var hadGeometry = false

    /// Tells whether this cook should frame the geometry, and remembers what it showed.
    /// - Parameters:
    ///   - hasGeometry: whether the cook produced points or triangles to show.
    ///   - requested: whether the edit asked for a frame (an import, an example).
    mutating func shouldFrame(hasGeometry: Bool, requested: Bool) -> Bool {
        defer { hadGeometry = hasGeometry }
        return requested || (hasGeometry && !hadGeometry)
    }
}
