import SwiftUI

/// What an import with a skeleton offers: its clips, and the button that builds the nodes that
/// play one. Without it the person has to know that the second output of the import is the
/// skeleton, add two nodes, wire three links and type the name of a clip.
struct AnimationSection: View {
    let skeleton: InspectorSkeleton
    @ObservedObject var model: InspectorModel
    @State private var chosen: String?

    /// The clip the button plays: the one picked, or the first when none was.
    private var selected: SkeletonClip? {
        skeleton.clips.first { $0.name == chosen } ?? skeleton.clips.first
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            SectionHeading(label: String(localized: "Animation"), icon: "clock")
            if skeleton.clips.isEmpty {
                Text("This model has a skeleton but no animation.")
                    .uiFont(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                VStack(spacing: 0) {
                    ForEach(skeleton.clips, id: \.name) { clip in
                        ClipRow(clip: clip, isSelected: clip == selected) { chosen = clip.name }
                    }
                }
                .background(RoundedRectangle(cornerRadius: 6).fill(Color.secondary.opacity(0.1)))
                .accessibilityElement(children: .contain)
                .accessibilityLabel(Text("Animation clips"))
                Button("Animate with This Clip") { model.animateSkeleton(clip: selected?.name) }
                    .help("Adds a Skeleton Pose and a Skin Deform node, wires them to this import and shows the result")
            }
        }
    }
}

private struct ClipRow: View {
    let clip: SkeletonClip
    let isSelected: Bool
    let pick: () -> Void
    /// The Helix accent, not the system one: the system accent may be red, which the app keeps
    /// for errors.
    @Environment(\.theme) private var theme

    var body: some View {
        Button(action: pick) {
            HStack {
                Image(systemName: isSelected ? "checkmark.circle.fill" : "circle")
                    .foregroundStyle(isSelected ? theme.accent : Color.secondary)
                Text(clip.name).lineLimit(1)
                Spacer(minLength: 8)
                Text(clip.durationText).foregroundStyle(.secondary).monospacedDigit()
            }
            .padding(.horizontal, 8).padding(.vertical, 4)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(Text("\(clip.name), \(clip.durationText)"))
        .accessibilityAddTraits(isSelected ? .isSelected : [])
    }
}
