import CxxStdlib
import Foundation
import HelixCore

/// What each tool of the server does, through the facade.
@MainActor
enum MCPHandlers {
    /// The most elements read_attribute returns in one call.
    static let maxAttributeCount: Int64 = 10_000
    /// The biggest side of a render_view picture.
    static let maxImageSide: Int64 = 2048

    // MARK: Facade values as JSON

    static func check(_ status: helix.core.Status) throws {
        guard status.ok else { throw MCPToolError(String(engine: status.error)) }
    }

    static func nodeList() -> [helix.core.NodeInfo] {
        Array(helix.core.listNodes())
    }

    /// The identity of a node, which every result gives the same way.
    static func identity(_ info: helix.core.NodeInfo) -> JSONValue {
        [
            "id": .init(count: info.id),
            "name": .string(String(engine: info.name)),
            "path": .string(String(engine: info.path)),
            "type": .string(String(engine: info.type)),
        ]
    }

    static func describe(_ id: helix.core.NodeId) -> JSONValue {
        nodeList().first { $0.id == id }.map(identity) ?? ["id": .init(count: id)]
    }

    /// Finds a node by name, by path or by id.
    static func resolve(_ reference: JSONValue?, argument: String = "node") throws -> helix.core.NodeInfo {
        guard let reference, !reference.isNull else { throw MCPToolError("The argument '\(argument)' is required.") }
        let nodes = nodeList()

        if case .int(let id) = reference {
            if let found = nodes.first(where: { Int64(clamping: $0.id) == id }) { return found }
            throw MCPToolError("There is no node with the id \(id).")
        }
        guard let text = reference.string else {
            throw MCPToolError("The argument '\(argument)' must be a name, a path or an id.")
        }
        if let found = nodes.first(where: { String(engine: $0.path) == text }) { return found }
        if let found = nodes.first(where: { String(engine: $0.name) == text }) { return found }
        if let id = UInt64(text), let found = nodes.first(where: { $0.id == id }) { return found }

        let names = nodes.map { String(engine: $0.name) }.sorted().prefix(20).joined(separator: ", ")
        let known = nodes.isEmpty ? "The scene has no node." : "The nodes are: \(names)."
        throw MCPToolError("There is no node '\(text)'. \(known)")
    }

    private static func ownerName(_ owner: helix.core.AttributeOwner) -> String {
        switch owner {
        case .Point: "point"
        case .Vertex: "vertex"
        case .Face: "face"
        case .Primitive: "primitive"
        @unknown default: "unknown"
        }
    }

    private static func typeName(_ type: helix.core.AttributeValueType) -> String {
        switch type {
        case .Int: "int"
        case .Float: "float"
        case .Vector3: "vector3"
        case .Bool: "bool"
        case .Matrix4: "matrix4"
        @unknown default: "unknown"
        }
    }

    private static func vector(_ x: Float, _ y: Float, _ z: Float) -> JSONValue {
        [.init(x), .init(y), .init(z)]
    }

    static func timeJSON() -> JSONValue {
        let time = helix.core.timeInfo()
        return [
            "frame": .init(time.frame),
            "start_frame": .int(Int64(time.startFrame)),
            "end_frame": .int(Int64(time.endFrame)),
            "fps": .init(time.fps),
        ]
    }

    /// What the cook cache holds and how it has been used.
    static func cookCacheJSON() -> JSONValue {
        let info = helix.core.cookCacheInfo()
        func count(_ value: UInt64) -> JSONValue { .int(Int64(clamping: value)) }
        return [
            "bytes_used": count(info.bytesUsed),
            "budget": count(info.budget),
            "entries": count(info.entries),
            "frame_entries": count(info.frameEntries),
            "state_entries": count(info.stateEntries),
            "hits": count(info.hits),
            "misses": count(info.misses),
            "state_hits": count(info.stateHits),
        ]
    }

    private static func sceneJSON() -> JSONValue {
        let nodes = nodeList()
        let names = Dictionary(uniqueKeysWithValues: nodes.map { ($0.id, String(engine: $0.name)) })

        let nodeEntries: [JSONValue] = nodes.map { info in
            var entry = identity(info)
            entry = .object((entry.object ?? [:]).merging(["x": .init(info.x), "y": .init(info.y)]) { $1 })
            // A node says why its last cook failed, and what it warned about, when it has
            // something to say. A node nothing has cooked yet has no verdict.
            if !info.error.empty(), var fields = entry.object {
                fields["error"] = .string(String(engine: info.error))
                entry = .object(fields)
            }
            if !info.warnings.empty(), var fields = entry.object {
                fields["warnings"] = .array(info.warnings.map { .string(String(engine: $0)) })
                entry = .object(fields)
            }
            return entry
        }
        let linkEntries: [JSONValue] = Array(helix.core.listLinks()).map { link in
            [
                "from": .string(names[link.sourceNode] ?? ""),
                "from_id": .init(count: link.sourceNode),
                "output": .int(Int64(link.sourceOutput)),
                "to": .string(names[link.targetNode] ?? ""),
                "to_id": .init(count: link.targetNode),
                "input": .int(Int64(link.targetInput)),
            ]
        }
        let display = helix.core.displayNode()
        return [
            "nodes": .array(nodeEntries),
            "links": .array(linkEntries),
            "display_node": display == 0 ? .null : describe(display),
            "animated_nodes": .array(Array(helix.core.animatedNodes()).sorted().map { describe($0) }),
            "time": timeJSON(),
            "cook_cache": cookCacheJSON(),
            "modified": .bool(helix.core.isSceneModified()),
        ]
    }

    private static func expanded(_ path: String) -> String {
        (path as NSString).expandingTildeInPath
    }

    // MARK: The scene

    static func sceneInfo(_ arguments: Arguments) throws -> MCPToolOutput {
        .data(sceneJSON())
    }

    static func clearCookCache(_ arguments: Arguments) throws -> MCPToolOutput {
        helix.core.clearCookCache()
        return .data(["cook_cache": cookCacheJSON()])
    }

    /// Refuses to throw away work the agent did not mean to lose.
    private static func requireDiscardable(_ arguments: Arguments, doing action: String) throws {
        guard helix.core.isSceneModified() else { return }
        if try arguments.bool("discard_changes") == true { return }
        throw MCPToolError(
            "The scene has unsaved changes, and \(action) would lose them. Call save_scene first, or pass discard_changes: true."
        )
    }

    static func newScene(_ arguments: Arguments) throws -> MCPToolOutput {
        try requireDiscardable(arguments, doing: "new_scene")
        try check(helix.core.newScene())
        return .data(sceneJSON())
    }

    static func openScene(_ arguments: Arguments) throws -> MCPToolOutput {
        let path = expanded(try arguments.requiredString("path"))
        try requireDiscardable(arguments, doing: "open_scene")
        try check(helix.core.openScene(std.string(path)))
        return .data(sceneJSON())
    }

    static func saveScene(_ arguments: Arguments) throws -> MCPToolOutput {
        let path = expanded(try arguments.requiredString("path"))
        try check(helix.core.saveScene(std.string(path)))
        return .data(["saved": .string(path), "node_count": .int(Int64(nodeList().count))])
    }

    // MARK: The graph

    static func listNodeTypes(_ arguments: Arguments) throws -> MCPToolOutput {
        let types = helix.core.listNodeTypeInfos().map { info -> JSONValue in
            .object([
                "name": .string(String(engine: info.name)),
                "label": .string(String(engine: info.label)),
                "tags": .array(info.tags.map { .string(String(engine: $0)) }),
                // The inputs are numbered from zero in this order, and a multi input port takes
                // every input from its own position onward.
                "inputs": .array(info.inputs.map { port in
                    .object([
                        "label": .string(String(engine: port.label)),
                        "multi_input": .bool(port.multiInput),
                        "optional": .bool(port.optional),
                    ])
                }),
                "output_count": .int(Int64(info.outputCount)),
            ])
        }
        return .data(["types": .array(types)])
    }

    static func createNode(_ arguments: Arguments) throws -> MCPToolOutput {
        let type = try arguments.requiredString("type")
        let name = try arguments.string("name") ?? ""
        let x = try arguments.number("x") ?? 0
        let y = try arguments.number("y") ?? 0
        let result = helix.core.createNode(std.string(type), std.string(name), Float(x), Float(y))
        try check(result.status)
        return .data(describe(result.id))
    }

    static func deleteNode(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let removed = identity(node)
        try check(helix.core.deleteNode(node.id))
        return .data(["deleted": removed])
    }

    private static func ports(_ arguments: Arguments) throws -> (from: helix.core.NodeInfo, to: helix.core.NodeInfo, output: UInt32, input: UInt32) {
        let from = try resolve(arguments.values["from"], argument: "from")
        let to = try resolve(arguments.values["to"], argument: "to")
        let output = try arguments.integer("output") ?? 0
        let input = try arguments.integer("input") ?? 0
        guard output >= 0, output <= Int64(UInt32.max), input >= 0, input <= Int64(UInt32.max) else {
            throw MCPToolError("The ports 'output' and 'input' are small non-negative integers.")
        }
        return (from, to, UInt32(output), UInt32(input))
    }

    static func connectNodes(_ arguments: Arguments) throws -> MCPToolOutput {
        let link = try ports(arguments)
        try check(helix.core.connectNodes(link.from.id, link.output, link.to.id, link.input))
        return .data([
            "from": identity(link.from), "output": .int(Int64(link.output)),
            "to": identity(link.to), "input": .int(Int64(link.input)),
        ])
    }

    static func disconnectNodes(_ arguments: Arguments) throws -> MCPToolOutput {
        let link = try ports(arguments)
        try check(helix.core.disconnectNodes(link.from.id, link.output, link.to.id, link.input))
        return .data([
            "from": identity(link.from), "output": .int(Int64(link.output)),
            "to": identity(link.to), "input": .int(Int64(link.input)),
        ])
    }

    static func setDisplayNode(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        try check(helix.core.setDisplayNode(node.id))
        return .data(["display_node": identity(node)])
    }

    // MARK: The parameters

    private static func typeName(_ type: helix.core.ParameterType) -> String {
        switch type {
        case .Float: "float"
        case .Int: "int"
        case .String: "string"
        case .Toggle: "toggle"
        case .Dropdown: "dropdown"
        case .Ramp: "ramp"
        case .Other: "other"
        @unknown default: "other"
        }
    }

    /// The current value of a parameter: a scalar, or an array for several components, or null
    /// for a parameter that holds no plain value.
    private static func readParameter(_ node: helix.core.NodeId, _ info: helix.core.ParameterInfo) -> JSONValue {
        let name = info.name
        let count = Int(info.componentCount)
        var values: [JSONValue] = []
        for component in 0..<count {
            let index = UInt32(component)
            switch info.type {
            case .Float:
                let result = helix.core.getFloat(node, name, index)
                guard result.status.ok else { return .null }
                values.append(.init(result.value))
            case .Int:
                let result = helix.core.getInt(node, name, index)
                guard result.status.ok else { return .null }
                values.append(.int(result.value))
            case .Toggle:
                let result = helix.core.getInt(node, name, index)
                guard result.status.ok else { return .null }
                values.append(.bool(result.value != 0))
            case .String, .Dropdown:
                let result = helix.core.getString(node, name, index)
                guard result.status.ok else { return .null }
                values.append(.string(String(engine: result.value)))
            default:
                return .null
            }
        }
        if values.isEmpty { return .null }
        return values.count == 1 ? values[0] : .array(values)
    }

    /// A per component list as one JSON value: the element for one component, or an array.
    private static func perComponent(_ values: [JSONValue]) -> JSONValue {
        values.count == 1 ? values[0] : .array(values)
    }

    private static func describeParameter(_ node: helix.core.NodeId, _ info: helix.core.ParameterInfo) -> JSONValue {
        var entry: [String: JSONValue] = [
            "name": .string(String(engine: info.name)),
            "label": .string(String(engine: info.label)),
            "type": .string(typeName(info.type)),
            "components": .int(Int64(info.componentCount)),
            "value": readParameter(node, info),
            "hidden": .bool(info.hidden),
            "enabled": .bool(info.enabled),
        ]
        if !info.tooltip.empty() { entry["tooltip"] = .string(String(engine: info.tooltip)) }
        if !info.style.empty() { entry["style"] = .string(String(engine: info.style)) }
        if !info.group.empty() { entry["group"] = .string(String(engine: info.group)) }
        if info.hasRange {
            entry["range"] = [
                "min": .init(info.rangeMin), "max": .init(info.rangeMax),
                "min_locked": .bool(info.minLocked), "max_locked": .bool(info.maxLocked),
            ]
        }
        if !info.options.empty() {
            entry["options"] = .array(info.options.map {
                ["value": .string(String(engine: $0.value)), "label": .string(String(engine: $0.label))]
            })
        }
        switch info.type {
        case .Float: entry["default"] = perComponent(info.defaultFloats.map { .init($0) })
        case .Int: entry["default"] = perComponent(info.defaultInts.map { .int($0) })
        case .Toggle: entry["default"] = perComponent(info.defaultInts.map { .bool($0 != 0) })
        case .String, .Dropdown: entry["default"] = perComponent(info.defaultStrings.map { .string(String(engine: $0)) })
        default: break
        }
        // An expression is the text that drives a component, null where there is none. Its error
        // says why it reads as zero.
        let expressions = info.expressions.map { String(engine: $0) }
        if expressions.contains(where: { !$0.isEmpty }) {
            entry["expression"] = perComponent(expressions.map { $0.isEmpty ? .null : .string($0) })
            let errors = info.expressionErrors.map { String(engine: $0) }
            if errors.contains(where: { !$0.isEmpty }) {
                entry["expression_error"] = perComponent(errors.map { $0.isEmpty ? .null : .string($0) })
            }
        }
        if !info.animatedComponents.empty() {
            entry["animated"] = .array(info.animatedComponents.map { .int(Int64($0)) })
            // The components that have a key on the frame the scene sits on.
            entry["keyed"] = .array(info.keyedComponents.map { .int(Int64($0)) })
        }
        if info.type == .Ramp {
            let ramp = helix.core.getRamp(node, info.name)
            entry["points"] = .array(ramp.points.map {
                [
                    "position": .init($0.position), "value": .init($0.value),
                    "interpolation": .string(String(engine: $0.interpolation)),
                ]
            })
            entry["interpolations"] = .array(ramp.interpolations.map { .string(String(engine: $0.value)) })
        }
        return .object(entry)
    }

    static func listParameters(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let entries = Array(helix.core.listParameters(node.id)).map { describeParameter(node.id, $0) }
        // The order the parameters are shown in, with the groups and headings between them.
        let layout: [JSONValue] = Array(helix.core.listParameterLayout(node.id)).map { item in
            var fields: [String: JSONValue] = ["name": .string(String(engine: item.name))]
            switch item.kind {
            case .Parameter: fields["kind"] = "parameter"
            case .GroupBegin: fields["kind"] = "group_begin"
            case .GroupEnd: fields["kind"] = "group_end"
            case .Divider: fields["kind"] = "divider"
            @unknown default: fields["kind"] = "parameter"
            }
            if !item.label.empty() { fields["label"] = .string(String(engine: item.label)) }
            fields["hidden"] = .bool(item.hidden)
            return .object(fields)
        }
        return .data(["node": identity(node), "parameters": .array(entries), "layout": .array(layout)])
    }

    static func findParameterInfo(_ node: helix.core.NodeInfo, _ name: String) throws -> helix.core.ParameterInfo {
        let parameters = Array(helix.core.listParameters(node.id))
        guard let info = parameters.first(where: { String(engine: $0.name) == name }) else {
            let known = parameters.map { String(engine: $0.name) }.joined(separator: ", ")
            throw MCPToolError("The node '\(String(engine: node.name))' has no parameter '\(name)'. Its parameters: \(known).")
        }
        return info
    }

    /// What a parameter reads back after it was written: its value, and its expression if it has one.
    static func parameterResult(_ node: helix.core.NodeInfo, _ name: String) throws -> MCPToolOutput {
        let info = try findParameterInfo(node, name)
        var result = describeParameter(node.id, info).object ?? [:]
        // The identity and the name are given the way every parameter result gives them.
        for key in ["label", "components", "hidden", "enabled", "tooltip", "style", "group", "range", "options", "default"] {
            result[key] = nil
        }
        result["node"] = identity(node)
        result["parameter"] = .string(name)
        result["name"] = nil
        return .data(.object(result))
    }

    private static func ramp(from value: JSONValue, parameter name: String) throws -> [helix.core.RampPoint] {
        guard let list = value.array, list.allSatisfy({ $0.object != nil }) else {
            throw MCPToolError(
                "The ramp '\(name)' takes an array of points, each {position, value, interpolation}, with the interpolation constant, linear or bspline."
            )
        }
        return try list.map { item in
            guard let fields = item.object, let position = fields["position"]?.number, let number = fields["value"]?.number else {
                throw MCPToolError("Every point of the ramp '\(name)' needs a numeric position and value.")
            }
            var point = helix.core.RampPoint()
            point.position = Float(position)
            point.value = Float(number)
            if let interpolation = fields["interpolation"] {
                guard let text = interpolation.string else { throw MCPToolError("The interpolation of a ramp point is a string.") }
                point.interpolation = std.string(text)
            }
            return point
        }
    }

    static func setParameter(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        let component = try arguments.integer("component")
        let info = try findParameterInfo(node, name)
        let count = Int(info.componentCount)

        if let expression = try arguments.string("expression") {
            guard !arguments.has("value") else { throw MCPToolError("Give either a value or an expression, not both.") }
            let index = component ?? (count == 1 ? 0 : nil)
            guard let index else {
                throw MCPToolError("The parameter '\(name)' holds \(count) values: give the component the expression drives.")
            }
            guard index >= 0, index < Int64(count) else {
                throw MCPToolError("Component \(index) is out of range, the parameter holds \(count).")
            }
            try check(helix.core.setExpression(node.id, std.string(name), UInt32(index), std.string(expression)))
            return try parameterResult(node, name)
        }

        guard let value = arguments.value("value") else { throw MCPToolError("Give a 'value' or an 'expression'.") }

        if info.type == .Ramp {
            var points = helix.core.RampPointList()
            for point in try ramp(from: value, parameter: name) { points.push_back(point) }
            try check(helix.core.setRamp(node.id, std.string(name), points))
            return try parameterResult(node, name)
        }

        // One value per component, in order: an array gives them all, a single value gives one.
        var assignments: [(component: Int, value: JSONValue)] = []
        if let list = value.array {
            guard component == nil else { throw MCPToolError("Give either an array or a component, not both.") }
            guard list.count == count else {
                throw MCPToolError("The parameter '\(name)' holds \(count) value(s), the array has \(list.count).")
            }
            assignments = list.enumerated().map { ($0.offset, $0.element) }
        } else {
            let index = component ?? (count == 1 ? 0 : nil)
            guard let index else {
                throw MCPToolError("The parameter '\(name)' holds \(count) values: give an array of \(count), or a component.")
            }
            guard index >= 0, index < Int64(count) else {
                throw MCPToolError("Component \(index) is out of range, the parameter holds \(count).")
            }
            assignments = [(Int(index), value)]
        }

        switch info.type {
        case .Float:
            let numbers = try assignments.map { assignment -> Float in
                guard let number = assignment.value.number else { throw MCPToolError("The parameter '\(name)' takes numbers.") }
                return Float(number)
            }
            if value.array != nil, count == 3 {
                try check(helix.core.setVector3(node.id, std.string(name), numbers[0], numbers[1], numbers[2]))
            } else {
                for (assignment, number) in zip(assignments, numbers) {
                    try check(helix.core.setFloat(node.id, std.string(name), UInt32(assignment.component), number))
                }
            }
        case .Int, .Toggle:
            let integers = try assignments.map { assignment -> Int64 in
                if let flag = assignment.value.bool { return flag ? 1 : 0 }
                guard let integer = assignment.value.integer else {
                    throw MCPToolError("The parameter '\(name)' takes whole numbers (or true and false for a toggle).")
                }
                return integer
            }
            for (assignment, integer) in zip(assignments, integers) {
                try check(helix.core.setInt(node.id, std.string(name), UInt32(assignment.component), integer))
            }
        case .String, .Dropdown:
            let texts = try assignments.map { assignment -> String in
                guard let text = assignment.value.string else { throw MCPToolError("The parameter '\(name)' takes a string.") }
                return text
            }
            for (assignment, text) in zip(assignments, texts) {
                try check(helix.core.setString(node.id, std.string(name), UInt32(assignment.component), std.string(text)))
            }
        default:
            throw MCPToolError("The parameter '\(name)' is a \(typeName(info.type)), which cannot be set from here.")
        }

        return try parameterResult(node, name)
    }

    static func resetParameter(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        _ = try findParameterInfo(node, name)
        try check(helix.core.resetParameter(node.id, std.string(name)))
        return try parameterResult(node, name)
    }

    // MARK: Import and time

    static func importFile(_ arguments: Arguments) throws -> MCPToolOutput {
        let path = expanded(try arguments.requiredString("path"))
        let extensions = helix.core.supportedImportExtensions().map { String(engine: $0) }
        let result = helix.core.importFile(std.string(path))
        guard result.status.ok else {
            throw MCPToolError(
                "\(String(engine: result.status.error)) Helix imports: \(extensions.joined(separator: " "))."
            )
        }
        return .data([
            "node": describe(result.id),
            "summary": .string(String(engine: helix.core.lastImportSummary())),
            "supported_extensions": .array(extensions.map { .string($0) }),
        ])
    }

    static func setFrame(_ arguments: Arguments) throws -> MCPToolOutput {
        let frame = try arguments.requiredNumber("frame")
        helix.core.setFrame(Float(frame))
        return .data(["time": timeJSON()])
    }

    // MARK: The results

    /// The node asked for, or the display node.
    private static func target(_ arguments: Arguments) throws -> helix.core.NodeInfo {
        if arguments.has("node") { return try resolve(arguments.values["node"]) }
        let display = helix.core.displayNode()
        guard display != 0, let found = nodeList().first(where: { $0.id == display }) else {
            throw MCPToolError("No node is displayed. Pass a node, or call set_display_node first.")
        }
        return found
    }

    static func geometrySummary(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try target(arguments)
        let report = helix.core.cookNodeReport(node.id)
        try check(report.status)

        var bounds: JSONValue = .null
        if report.bounds.valid {
            let b = report.bounds
            bounds = [
                "min": vector(b.minX, b.minY, b.minZ),
                "max": vector(b.maxX, b.maxY, b.maxZ),
                "center": vector((b.minX + b.maxX) / 2, (b.minY + b.maxY) / 2, (b.minZ + b.maxZ) / 2),
                "size": vector(b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ),
            ]
        }
        let attributes: [JSONValue] = Array(report.attributes).map { attribute in
            var entry: [String: JSONValue] = [
                "owner": .string(ownerName(attribute.owner)),
                "name": .string(String(engine: attribute.name)),
                "type": .string(typeName(attribute.type)),
                "components": .int(Int64(attribute.componentCount)),
                "count": .init(count: attribute.elementCount),
            ]
            // read_attribute refuses such an attribute; type and components are the first primitive's.
            if attribute.mixedTypes { entry["mixed_types"] = true }
            return .object(entry)
        }
        // One entry per material, shared ones once. A face's material_index points into the table
        // of its own mesh, so this is the list of what the geometry is made of, not that table.
        let materials: [JSONValue] = Array(report.materials).map { material in
            let color = material.baseColor
            var entry: [String: JSONValue] = [
                "name": .string(String(engine: material.name)),
                "base_color": [.init(color.0), .init(color.1), .init(color.2), .init(color.3)],
                "has_texture": .bool(material.hasTexture),
            ]
            if material.hasTexture {
                entry["texture"] = [
                    "width": .int(Int64(material.textureWidth)),
                    "height": .int(Int64(material.textureHeight)),
                    "source": .string(String(engine: material.textureSource)),
                ]
            }
            if !material.missingTexture.isEmpty {
                entry["missing_texture"] = .string(String(engine: material.missingTexture))
            }
            return .object(entry)
        }
        var summary: [String: JSONValue] = [
            "node": identity(node),
            "point_count": .init(count: report.pointCount),
            "primitive_count": .init(count: report.primitiveCount),
            "vertex_count": .init(count: report.vertexCount),
            "face_count": .init(count: report.faceCount),
            "triangle_count": .init(count: report.triangleCount),
            "bounds": bounds,
            "attributes": .array(attributes),
            "materials": .array(materials),
        ]
        // A skeleton is reported only when the node produced one: its joints, its roots, and the
        // clips a Skeleton Pose node can play (by name) with their length in seconds.
        if report.skeleton.present {
            let clips: [JSONValue] = Array(report.skeleton.clips).map { clip in
                [
                    "name": .string(String(engine: clip.name)),
                    "duration": .init(clip.duration),
                    "channels": .init(count: clip.channelCount),
                ]
            }
            summary["skeleton"] = [
                "joints": .init(count: report.skeleton.jointCount),
                "roots": .init(count: report.skeleton.rootCount),
                "clips": .array(clips),
            ]
        }
        return .data(.object(summary))
    }

    private static func owner(_ text: String) throws -> helix.core.AttributeOwner {
        switch text {
        case "point": return .Point
        case "vertex": return .Vertex
        case "face": return .Face
        case "primitive": return .Primitive
        default: throw MCPToolError("The owner is point, vertex, face or primitive, not '\(text)'.")
        }
    }

    static func readAttribute(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try target(arguments)
        let ownerText = try arguments.requiredString("owner")
        let owner = try owner(ownerText)
        let name = try arguments.requiredString("name")
        let start = try arguments.integer("start") ?? 0
        let wanted = try arguments.integer("count") ?? 100
        guard start >= 0 else { throw MCPToolError("The argument 'start' cannot be negative.") }
        guard wanted >= 0 else { throw MCPToolError("The argument 'count' cannot be negative.") }
        let count = min(wanted, maxAttributeCount)

        let result = helix.core.readAttribute(node.id, owner, std.string(name), UInt64(start), UInt64(count))
        try check(result.status)

        let stride = Int(result.componentCount)
        var values: [JSONValue] = []
        switch result.type {
        case .Int:
            values = result.ints.map { .int($0) }
        case .Bool:
            values = result.ints.map { .bool($0 != 0) }
        case .Float:
            values = result.floats.map { .init($0) }
        case .Vector3, .Matrix4:
            let floats = Array(result.floats)
            values = stride > 0
                ? Swift.stride(from: 0, to: floats.count, by: stride).map { first in
                    .array(floats[first..<min(first + stride, floats.count)].map { .init($0) })
                }
                : []
        @unknown default:
            break
        }

        let first = Int64(clamping: result.start)
        let total = Int64(clamping: result.totalCount)
        let end = first + Int64(values.count)
        return .data([
            "node": identity(node),
            "owner": .string(ownerText),
            "name": .string(name),
            "type": .string(typeName(result.type)),
            "components": .int(Int64(stride)),
            "total_count": .int(total),
            "start": .int(first),
            "count": .int(Int64(values.count)),
            "next_start": end < total ? .int(end) : .null,
            "values": .array(values),
        ])
    }

    static func renderView(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try target(arguments)
        let width = try arguments.integer("width") ?? 768
        let height = try arguments.integer("height") ?? 512
        guard width >= 1, height >= 1 else { throw MCPToolError("The width and the height are at least 1 pixel.") }
        let pixelWidth = UInt32(min(width, maxImageSide))
        let pixelHeight = UInt32(min(height, maxImageSide))

        let materialsVisible = try arguments.bool("materials") ?? true
        let frame = try arguments.number("frame")
        let azimuth = try arguments.number("azimuth")
        let elevation = try arguments.number("elevation")
        var wireframe = helix.core.WireframeMode.Auto
        switch try arguments.string("wireframe") ?? "auto" {
        case "auto": wireframe = .Auto
        case "on": wireframe = .On
        case "off": wireframe = .Off
        case let other: throw MCPToolError("The wireframe is auto, on or off, not '\(other)'.")
        }

        // A frame asked for is shown, held to the range, and the scene goes back to the frame it
        // was on afterwards, so a picture of another frame changes nothing the agent set.
        let previousFrame = helix.core.timeInfo().frame
        if let frame { helix.core.setFrame(Float(frame)) }
        defer { if frame != nil { helix.core.setFrame(previousFrame) } }

        // The render draws the display node, so the node asked for takes that place for the
        // length of the call and gives it back after, even when it was none.
        let previous = helix.core.displayNode()
        if previous != node.id { try check(helix.core.setDisplayNode(node.id)) }
        defer {
            if previous != node.id {
                _ = previous == 0 ? helix.core.clearDisplayNode() : helix.core.setDisplayNode(previous)
            }
        }

        let geometry = helix.core.updateViewportGeometry()
        try check(geometry.status)

        // The framing of the first frame of the window, at the angles asked for: the angles come
        // first, since the frame fits the box as seen from them.
        helix.core.resetViewport()
        if azimuth != nil || elevation != nil {
            helix.core.orbitViewportTo(Float(azimuth ?? -135), Float(elevation ?? 20))
        }
        _ = helix.core.frameViewport(Float(pixelWidth) / Float(pixelHeight))

        var options = helix.core.ViewportFrameOptions()
        options.pixelWidth = pixelWidth
        options.pixelHeight = pixelHeight
        options.backgroundRed = ViewportRenderer.background.red
        options.backgroundGreen = ViewportRenderer.background.green
        options.backgroundBlue = ViewportRenderer.background.blue
        options.wireframe = wireframe
        options.materialsVisible = materialsVisible

        let file = FileManager.default.temporaryDirectory.appending(path: "helix-mcp-\(UUID().uuidString).png")
        defer { try? FileManager.default.removeItem(at: file) }
        try check(helix.core.renderToImage(std.string(file.path), options, true))
        guard let png = try? Data(contentsOf: file) else { throw MCPToolError("The picture could not be read back.") }

        let shownFrame = helix.core.timeInfo().frame
        let text = "\(pixelWidth) x \(pixelHeight) PNG of \(String(engine: node.name)) at frame \(shownFrame): "
            + "\(geometry.pointCount) points, \(geometry.triangleCount) triangles."
        return MCPToolOutput(
            content: [
                ["type": "image", "data": .string(png.base64EncodedString()), "mimeType": "image/png"],
                ["type": "text", "text": .string(text)],
            ],
            structured: nil
        )
    }
}
