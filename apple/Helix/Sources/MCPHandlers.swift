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

    private static func check(_ status: helix.core.Status) throws {
        guard status.ok else { throw MCPToolError(String(engine: status.error)) }
    }

    private static func nodeList() -> [helix.core.NodeInfo] {
        Array(helix.core.listNodes())
    }

    /// The identity of a node, which every result gives the same way.
    private static func identity(_ info: helix.core.NodeInfo) -> JSONValue {
        [
            "id": .init(count: info.id),
            "name": .string(String(engine: info.name)),
            "path": .string(String(engine: info.path)),
            "type": .string(String(engine: info.type)),
        ]
    }

    private static func describe(_ id: helix.core.NodeId) -> JSONValue {
        nodeList().first { $0.id == id }.map(identity) ?? ["id": .init(count: id)]
    }

    /// Finds a node by name, by path or by id.
    private static func resolve(_ reference: JSONValue?, argument: String = "node") throws -> helix.core.NodeInfo {
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

    private static func timeJSON() -> JSONValue {
        let time = helix.core.timeInfo()
        return [
            "frame": .init(time.frame),
            "start_frame": .int(Int64(time.startFrame)),
            "end_frame": .int(Int64(time.endFrame)),
            "fps": .init(time.fps),
        ]
    }

    private static func sceneJSON() -> JSONValue {
        let nodes = nodeList()
        let names = Dictionary(uniqueKeysWithValues: nodes.map { ($0.id, String(engine: $0.name)) })

        let nodeEntries: [JSONValue] = nodes.map { info in
            var entry = identity(info)
            entry = .object((entry.object ?? [:]).merging(["x": .init(info.x), "y": .init(info.y)]) { $1 })
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
            "time": timeJSON(),
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
        let types = helix.core.listNodeTypes().map { JSONValue.string(String(engine: $0)) }
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

    static func listParameters(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let entries: [JSONValue] = Array(helix.core.listParameters(node.id)).map { info in
            [
                "name": .string(String(engine: info.name)),
                "label": .string(String(engine: info.label)),
                "type": .string(typeName(info.type)),
                "components": .int(Int64(info.componentCount)),
                "value": readParameter(node.id, info),
            ]
        }
        return .data(["node": identity(node), "parameters": .array(entries)])
    }

    static func setParameter(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        guard let value = arguments.value("value") else { throw MCPToolError("The argument 'value' is required.") }
        let component = try arguments.integer("component")

        let parameters = Array(helix.core.listParameters(node.id))
        guard let info = parameters.first(where: { String(engine: $0.name) == name }) else {
            let known = parameters.map { String(engine: $0.name) }.joined(separator: ", ")
            throw MCPToolError("The node '\(String(engine: node.name))' has no parameter '\(name)'. Its parameters: \(known).")
        }
        let count = Int(info.componentCount)

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

        return .data([
            "node": identity(node),
            "parameter": .string(name),
            "value": readParameter(node.id, info),
        ])
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
        return .data([
            "node": identity(node),
            "point_count": .init(count: report.pointCount),
            "primitive_count": .init(count: report.primitiveCount),
            "vertex_count": .init(count: report.vertexCount),
            "face_count": .init(count: report.faceCount),
            "triangle_count": .init(count: report.triangleCount),
            "bounds": bounds,
            "attributes": .array(attributes),
            "materials": .array(materials),
        ])
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

        let azimuth = try arguments.number("azimuth")
        let elevation = try arguments.number("elevation")
        var wireframe = helix.core.WireframeMode.Auto
        switch try arguments.string("wireframe") ?? "auto" {
        case "auto": wireframe = .Auto
        case "on": wireframe = .On
        case "off": wireframe = .Off
        case let other: throw MCPToolError("The wireframe is auto, on or off, not '\(other)'.")
        }

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

        let file = FileManager.default.temporaryDirectory.appending(path: "helix-mcp-\(UUID().uuidString).png")
        defer { try? FileManager.default.removeItem(at: file) }
        try check(helix.core.renderToImage(std.string(file.path), options, true))
        guard let png = try? Data(contentsOf: file) else { throw MCPToolError("The picture could not be read back.") }

        let text = "\(pixelWidth) x \(pixelHeight) PNG of \(String(engine: node.name)): "
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
