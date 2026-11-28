import CxxStdlib
import Foundation
import HelixCore

/// Copies a node and its parameters out of the engine into the values the inspector draws.
@MainActor
enum InspectorLoader {
    static func load(node id: UInt64) -> InspectorNode? {
        guard let info = helix.core.listNodes().first(where: { $0.id == id }) else { return nil }
        let typeName = String(engine: info.type)
        let warnings = info.warnings.map { String(engineMessage: $0) }
        let error = String(engineMessage: info.error)

        // A node made from an alias shows the entry the person picked, and what it is based on.
        let catalog = NodeTypeCatalog.byName
        let typeLabel = catalog[typeName]?.label ?? typeName
        let aliasLabel = catalog[String(engine: info.aliasType)]?.label

        let parameters = Array(helix.core.listParameters(id)).map { convert($0, of: id, type: typeName) }
        let layout = Array(helix.core.listParameterLayout(id)).map { convert($0, type: typeName) }
        var node = InspectorNode(
            id: id,
            name: String(engine: info.name),
            path: String(engine: info.path),
            type: typeName,
            typeLabel: aliasLabel ?? typeLabel,
            basedOn: aliasLabel == nil ? nil : typeLabel,
            isDisplay: helix.core.displayNode() == id,
            error: error.isEmpty ? nil : error,
            warnings: warnings,
            layout: layout,
            parameters: Dictionary(parameters.map { ($0.name, $0) }, uniquingKeysWith: { first, _ in first })
        )
        readFiles(into: &node)
        readSkeleton(into: &node)
        return node
    }

    /// The clips of an import's skeleton, for its Animation section, and the menu a pose node
    /// picks its clip from: the clips of the skeleton wired into it, or the plain text field when
    /// nothing is wired in yet.
    private static func readSkeleton(into node: inout InspectorNode) {
        switch node.type {
        case SkeletonChain.importType:
            // A file that is not there has nothing to read, and its error is already shown.
            guard node.missingFile == nil, let clips = SkeletonChain.clips(ofImport: node.id) else { return }
            node.skeleton = InspectorSkeleton(clips: clips)
        case SkeletonChain.poseType:
            let result = helix.core.skeletonClipsReachingInput(node.id, 0)
            guard result.status.ok, result.present, var clip = node.parameters["clip"] else { return }
            let clips = result.clips.map { SkeletonClip(name: String(engine: $0.name), duration: Double($0.duration)) }
            clip.kind = .dropdown
            clip.options = SkeletonClipChoices.options(clips: clips, current: clip.strings.first ?? "")
            node.parameters["clip"] = clip
        default:
            break
        }
    }

    /// Whether the file the node reads is there, and which of the pictures it refers to are not.
    private static func readFiles(into node: inout InspectorNode) {
        guard let path = node.filePath else { return }
        guard FileManager.default.fileExists(atPath: path) else {
            node.missingFile = path
            return
        }
        // Only an import has pictures to lose. The report is what the cook found, kept by the
        // engine until the node cooks again, so asking again after an edit that changed nothing
        // here costs nothing.
        guard node.type == "helix::geometryImport" else { return }
        let report = helix.core.cookNodeReport(node.id)
        guard report.status.ok else { return }
        let folder = (path as NSString).deletingLastPathComponent
        var seen = Set<String>()
        for material in report.materials {
            let name = String(engine: material.missingTextureName)
            guard !name.isEmpty, seen.insert(name).inserted else { continue }
            node.missingTextures.append(MissingTexture(
                name: name,
                folder: material.missingTextureNotFound ? folder : nil,
                detail: material.missingTextureNotFound ? nil : String(engineMessage: material.missingTexture)
            ))
        }
    }

    private static func convert(_ item: helix.core.ParameterLayoutItem, type: String) -> InspectorLayoutItem {
        let kind: InspectorLayoutItem.Kind
        switch item.kind {
        case .Parameter: kind = .parameter
        case .GroupBegin: kind = .groupBegin
        case .GroupEnd: kind = .groupEnd
        case .Divider: kind = .divider
        @unknown default: kind = .parameter
        }
        return InspectorLayoutItem(
            kind: kind, name: String(engine: item.name),
            label: NodeText.label(type, parameter: String(engine: item.name), fallback: String(engine: item.label)),
            icon: String(engine: item.icon), horizontal: item.horizontal, labelInline: item.labelInline,
            hidden: item.hidden, enabled: item.enabled
        )
    }

    private static func kind(of type: helix.core.ParameterType) -> InspectorKind {
        switch type {
        case .Float: .float
        case .Int: .int
        case .String: .string
        case .Toggle: .toggle
        case .Dropdown: .dropdown
        case .Ramp: .ramp
        case .Other: .other
        @unknown default: .other
        }
    }

    private static func convert(_ info: helix.core.ParameterInfo, of node: UInt64, type: String) -> InspectorParameter {
        let name = String(engine: info.name)
        var parameter = InspectorParameter(name: name, label: NodeText.label(type, parameter: name, fallback: String(engine: info.label)))
        parameter.tooltip = NodeText.tooltip(type, parameter: name, fallback: String(engine: info.tooltip))
        parameter.documentation = NodeText.documentation(type, parameter: name, fallback: String(engine: info.documentation))
        parameter.kind = kind(of: info.type)
        parameter.style = InspectorStyle(name: String(engine: info.style))
        for option in info.styleOptions {
            let key = String(engine: option.name)
            let value = String(engine: option.value)
            parameter.styleOptions[key] = key == "caption" ? NodeText.caption(type, parameter: name, fallback: value) : value
        }
        parameter.componentCount = Int(info.componentCount)
        if info.hasRange {
            parameter.range = InspectorRange(
                min: Double(info.rangeMin), max: Double(info.rangeMax),
                minLocked: info.minLocked, maxLocked: info.maxLocked
            )
        }
        parameter.options = info.options.map {
            let value = String(engine: $0.value)
            return InspectorOption(
                value: value, label: NodeText.option(type, parameter: name, value: value, fallback: String(engine: $0.label))
            )
        }
        parameter.labelHidden = info.labelHidden
        parameter.hidden = info.hidden
        parameter.enabled = info.enabled
        parameter.group = String(engine: info.group)
        parameter.defaultFloats = info.defaultFloats.map { Double($0) }
        parameter.defaultInts = info.defaultInts.map { Int($0) }
        parameter.defaultStrings = info.defaultStrings.map { String(engine: $0) }
        parameter.expressions = info.expressions.map { String(engine: $0) }
        parameter.expressionErrors = info.expressionErrors.map { String(engine: $0) }
        parameter.animated = Set(info.animatedComponents.map { Int($0) })
        parameter.keyed = Set(info.keyedComponents.map { Int($0) })

        let engineName = std.string(name)
        for component in 0..<parameter.componentCount {
            let index = UInt32(component)
            switch parameter.kind {
            case .float:
                parameter.floats.append(Double(helix.core.getFloat(node, engineName, index).value))
            case .int, .toggle:
                parameter.ints.append(Int(helix.core.getInt(node, engineName, index).value))
            case .string, .dropdown:
                parameter.strings.append(String(engine: helix.core.getString(node, engineName, index).value))
            case .ramp, .other:
                break
            }
        }
        if parameter.kind == .ramp {
            let ramp = helix.core.getRamp(node, engineName)
            parameter.rampPoints = ramp.points.map {
                InspectorRampPoint(position: Double($0.position), value: Double($0.value), interpolation: String(engine: $0.interpolation))
            }
            parameter.rampInterpolations = ramp.interpolations.map {
                let value = String(engine: $0.value)
                return InspectorOption(value: value, label: RampText.interpolation(value, fallback: String(engine: $0.label)))
            }
        }
        return parameter
    }
}
