import Foundation

/// One animation of a skeleton, as the inspector lists it.
struct SkeletonClip: Equatable, Hashable, Sendable {
    var name: String
    /// Its length in seconds.
    var duration: Double

    /// "1 s", "2,6 s": the length in the unit and the number format of the language.
    var durationText: String {
        Measurement(value: duration, unit: UnitDuration.seconds)
            .formatted(.measurement(width: .abbreviated, usage: .asProvided, numberFormatStyle: .number.precision(.fractionLength(0...2))))
    }
}

/// The skeleton an import carries, when the file has one.
struct InspectorSkeleton: Equatable, Sendable {
    var clips: [SkeletonClip]
}

/// The menu a pose node's clip is picked from.
enum SkeletonClipChoices {
    /// An option for each clip the skeleton upstream has, and one for "none named", which plays the
    /// first. A clip the node names that the skeleton does not have stays in the list, marked as
    /// missing, so what is stored is shown as it is and not replaced by another clip.
    static func options(clips: [SkeletonClip], current: String) -> [InspectorOption] {
        var options = [InspectorOption(value: "", label: String(localized: "First clip"))]
        for clip in clips {
            options.append(InspectorOption(value: clip.name, label: "\(clip.name) (\(clip.durationText))"))
        }
        if !current.isEmpty, !clips.contains(where: { $0.name == current }) {
            options.append(InspectorOption(value: current, label: String(localized: "\(current) (missing)")))
        }
        return options
    }
}
