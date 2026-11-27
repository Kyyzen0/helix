import CxxStdlib
import Foundation
import HelixCore

/// Copies a node and its parameters out of the engine into the values the inspector draws.
@MainActor
enum InspectorLoader {
    static func load(node id: UInt64) -> InspectorNode? {
        guard let info = helix.core.listNodes().first(where: { $0.id == id }) else { return nil }
        let typeName = String(engine: info.type)
        let warnings = info.warnings.map { String(engine: $0) }
        let error = String(engine: info.error)

        let parameters = Array(helix.core.listParameters(id)).map { convert($0, of: id) }
        let layout = Array(helix.core.listParameterLayout(id)).map(convert)
        return InspectorNode(
            id: id,
            name: String(engine: info.name),
            path: String(engine: info.path),
            typeLabel: NodeTypeCatalog.byName[typeName]?.label ?? typeName,
            isDisplay: helix.core.displayNode() == id,
            error: error.isEmpty ? nil : error,
            warnings: warnings,
            layout: layout,
            parameters: Dictionary(parameters.map { ($0.name, $0) }, uniquingKeysWith: { first, _ in first })
        )
    }

    private static func convert(_ item: helix.core.ParameterLayoutItem) -> InspectorLayoutItem {
        let kind: InspectorLayoutItem.Kind
        switch item.kind {
        case .Parameter: kind = .parameter
        case .GroupBegin: kind = .groupBegin
        case .GroupEnd: kind = .groupEnd
        case .Divider: kind = .divider
        @unknown default: kind = .parameter
        }
        return InspectorLayoutItem(
            kind: kind, name: String(engine: item.name), label: String(engine: item.label),
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

    private static func convert(_ info: helix.core.ParameterInfo, of node: UInt64) -> InspectorParameter {
        let name = String(engine: info.name)
        var parameter = InspectorParameter(name: name, label: String(engine: info.label))
        parameter.tooltip = String(engine: info.tooltip)
        parameter.documentation = String(engine: info.documentation)
        parameter.kind = kind(of: info.type)
        parameter.style = InspectorStyle(name: String(engine: info.style))
        for option in info.styleOptions {
            parameter.styleOptions[String(engine: option.name)] = String(engine: option.value)
        }
        parameter.componentCount = Int(info.componentCount)
        if info.hasRange {
            parameter.range = InspectorRange(
                min: Double(info.rangeMin), max: Double(info.rangeMax),
                minLocked: info.minLocked, maxLocked: info.maxLocked
            )
        }
        parameter.options = info.options.map {
            InspectorOption(value: String(engine: $0.value), label: String(engine: $0.label))
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
                InspectorOption(value: String(engine: $0.value), label: String(engine: $0.label))
            }
        }
        return parameter
    }
}
