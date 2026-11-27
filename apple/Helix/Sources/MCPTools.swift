import Foundation

/// What a tool hands back: the content the agent reads, and the same data as a JSON object.
struct MCPToolOutput {
    var content: [JSONValue]
    var structured: JSONValue?

    /// A result that is data: the object goes out as structured content and, as text, with the
    /// same JSON, for the clients that read only the text.
    static func data(_ value: JSONValue) -> MCPToolOutput {
        MCPToolOutput(content: [["type": "text", "text": .string(value.serialized())]], structured: value)
    }
}

/// A tool that failed in a way the agent can read and correct: a node that does not exist, a
/// value out of range, the engine refusing. It becomes a result with `isError`, not a protocol
/// error.
struct MCPToolError: Error {
    let message: String
    init(_ message: String) { self.message = message }
}

/// One tool of the server: how it is announced, and what it does.
struct MCPTool {
    let name: String
    let summary: String
    let inputSchema: JSONValue
    let annotations: JSONValue
    let handler: @MainActor (JSONValue) throws -> MCPToolOutput

    /// The entry of `tools/list`.
    var listing: JSONValue {
        [
            "name": .string(name),
            "description": .string(summary),
            "inputSchema": inputSchema,
            "annotations": annotations,
        ]
    }
}

/// The tools of the server, by what they act on. The handlers are in `MCPHandlers.swift`.
@MainActor
enum MCPTools {
    static var all: [MCPTool] {
        [
            // The scene.
            tool("scene_info",
                 "Lists the nodes, the links, the display node, the time and whether the scene has unsaved changes.",
                 [:], readOnly: true, MCPHandlers.sceneInfo),
            tool("new_scene",
                 "Replaces the scene with an empty one. Refuses when there are unsaved changes unless discard_changes is true.",
                 ["discard_changes": boolean("Set to true to throw away unsaved changes.")],
                 destructive: true, MCPHandlers.newScene),
            tool("open_scene",
                 "Replaces the scene with the one stored in a .kut file. Refuses when there are unsaved changes unless discard_changes is true.",
                 ["path": string("The .kut file to open."),
                  "discard_changes": boolean("Set to true to throw away unsaved changes.")],
                 required: ["path"], destructive: true, MCPHandlers.openScene),
            tool("save_scene",
                 "Writes the scene to a .kut file, replacing it, and marks the scene as saved.",
                 ["path": string("Where to write the .kut file.")],
                 required: ["path"], destructive: true, idempotent: true, MCPHandlers.saveScene),

            // The graph.
            tool("list_node_types",
                 "Lists the type names create_node accepts, such as helix::cube.",
                 [:], readOnly: true, MCPHandlers.listNodeTypes),
            tool("create_node",
                 "Creates a node of a type, at the root of the scene. It is not displayed until set_display_node.",
                 ["type": string("The full type name, such as helix::cube."),
                  "name": string("The name to give it. Left out, the engine picks one such as cube1. A taken name is an error."),
                  "x": number("Where the node sits in the network editor."),
                  "y": number("Where the node sits in the network editor.")],
                 required: ["type"], MCPHandlers.createNode),
            tool("delete_node",
                 "Deletes a node and the links it has.",
                 ["node": nodeReference()],
                 required: ["node"], destructive: true, MCPHandlers.deleteNode),
            tool("connect_nodes",
                 "Wires an output of one node into an input of another. An input that was wired is rewired.",
                 ["from": nodeReference("The node whose output is read."),
                  "to": nodeReference("The node whose input receives it."),
                  "output": integer("The output port of from, 0 by default."),
                  "input": integer("The input port of to, 0 by default.")],
                 required: ["from", "to"], idempotent: true, MCPHandlers.connectNodes),
            tool("disconnect_nodes",
                 "Removes the link between two ports.",
                 ["from": nodeReference("The node whose output is read."),
                  "to": nodeReference("The node whose input receives it."),
                  "output": integer("The output port of from, 0 by default."),
                  "input": integer("The input port of to, 0 by default.")],
                 required: ["from", "to"], destructive: true, MCPHandlers.disconnectNodes),
            tool("set_display_node",
                 "Makes a node the one whose output is the result of the scene. geometry_summary and render_view use it by default.",
                 ["node": nodeReference()],
                 required: ["node"], idempotent: true, MCPHandlers.setDisplayNode),

            // The parameters.
            tool("list_parameters",
                 "Lists the parameters of a node with their type, number of components and current values.",
                 ["node": nodeReference()],
                 required: ["node"], readOnly: true, MCPHandlers.listParameters),
            tool("set_parameter",
                 "Sets a parameter of a node and returns the value read back, which the engine may have clamped. Give a number, an integer, a boolean or a string for one component, or an array for all of them, such as [1, 2, 3] for a vector.",
                 ["node": nodeReference(),
                  "parameter": string("The name of the parameter, as list_parameters gives it."),
                  "value": ["description": "A number, integer, boolean, string, or an array of them, one per component."],
                  "component": integer("Which component a single value sets, 0 by default. Not used with an array.")],
                 required: ["node", "parameter", "value"], idempotent: true, MCPHandlers.setParameter),

            // Import and time.
            tool("import_file",
                 "Imports a 3D file (glTF, GLB, FBX, OBJ, STL, PLY, USD, USDZ, Collada, .blend and more) as a new node. The node is not displayed until set_display_node.",
                 ["path": string("The file to import.")],
                 required: ["path"], MCPHandlers.importFile),
            tool("set_frame",
                 "Moves the scene to a frame, held to the playback range.",
                 ["frame": number("The frame number.")],
                 required: ["frame"], idempotent: true, MCPHandlers.setFrame),

            // The results.
            tool("geometry_summary",
                 "Cooks a node and reports its counts, its bounds and its attributes, as data.",
                 ["node": nodeReference("The node to cook. The display node when left out.")],
                 readOnly: true, MCPHandlers.geometrySummary),
            tool("read_attribute",
                 "Reads the values of an attribute of a node, up to 10000 elements a call. total_count and next_start let you page. The point positions are the attribute Position on the owner point.",
                 ["node": nodeReference("The node to cook. The display node when left out."),
                  "owner": ["type": "string", "enum": ["point", "vertex", "face", "primitive"],
                            "description": "Which elements own the attribute."],
                  "name": string("The attribute name, as geometry_summary lists it."),
                  "start": integer("The first element, 0 by default."),
                  "count": integer("How many elements, 100 by default and 10000 at most.")],
                 required: ["owner", "name"], readOnly: true, MCPHandlers.readAttribute),
            tool("render_view",
                 "Draws the geometry of a node, framed like the viewport of the app, and returns a PNG image. azimuth and elevation orbit the camera.",
                 ["node": nodeReference("The node to draw. The display node when left out."),
                  "width": integer("Width in pixels, 768 by default and 2048 at most."),
                  "height": integer("Height in pixels, 512 by default and 2048 at most."),
                  "azimuth": number("The angle around the vertical axis in degrees. The default view is at -135."),
                  "elevation": number("The angle above the horizontal plane in degrees, held between -89 and 89. The default view is at 20."),
                  "wireframe": ["type": "string", "enum": ["auto", "on", "off"],
                                "description": "Whether the edges are drawn over the shading, auto by default."]],
                 readOnly: true, MCPHandlers.renderView),
        ]
    }

    // MARK: Building a tool

    private static func tool(
        _ name: String,
        _ summary: String,
        _ properties: [String: JSONValue],
        required: [String] = [],
        readOnly: Bool = false,
        destructive: Bool = false,
        idempotent: Bool = false,
        _ handler: @escaping @MainActor (Arguments) throws -> MCPToolOutput
    ) -> MCPTool {
        var schema: [String: JSONValue] = ["type": "object", "properties": .object(properties)]
        if !required.isEmpty { schema["required"] = .array(required.map { .string($0) }) }

        var annotations: [String: JSONValue] = ["readOnlyHint": .bool(readOnly)]
        if !readOnly {
            annotations["destructiveHint"] = .bool(destructive)
            annotations["idempotentHint"] = .bool(idempotent)
        }
        return MCPTool(
            name: name,
            summary: summary,
            inputSchema: .object(schema),
            annotations: .object(annotations),
            handler: { try handler(Arguments(values: $0)) }
        )
    }

    private static func string(_ description: String) -> JSONValue {
        ["type": "string", "description": .string(description)]
    }

    private static func number(_ description: String) -> JSONValue {
        ["type": "number", "description": .string(description)]
    }

    private static func integer(_ description: String) -> JSONValue {
        ["type": "integer", "description": .string(description)]
    }

    private static func boolean(_ description: String) -> JSONValue {
        ["type": "boolean", "description": .string(description)]
    }

    private static func nodeReference(_ description: String = "The node, by name, path or id.") -> JSONValue {
        ["type": ["string", "integer"], "description": .string(description + " Accepts a name (cube1), a path (/cube1) or a numeric id.")]
    }
}

/// The arguments of a call, read with the errors an agent can act on.
struct Arguments {
    let values: JSONValue

    func has(_ key: String) -> Bool {
        if let value = values[key] { return !value.isNull }
        return false
    }

    func value(_ key: String) -> JSONValue? {
        has(key) ? values[key] : nil
    }

    func requiredString(_ key: String) throws -> String {
        guard let value = value(key) else { throw MCPToolError("The argument '\(key)' is required.") }
        guard let text = value.string else { throw MCPToolError("The argument '\(key)' must be a string.") }
        return text
    }

    func string(_ key: String) throws -> String? {
        guard let value = value(key) else { return nil }
        guard let text = value.string else { throw MCPToolError("The argument '\(key)' must be a string.") }
        return text
    }

    func number(_ key: String) throws -> Double? {
        guard let value = value(key) else { return nil }
        guard let number = value.number else { throw MCPToolError("The argument '\(key)' must be a number.") }
        return number
    }

    func requiredNumber(_ key: String) throws -> Double {
        guard let number = try number(key) else { throw MCPToolError("The argument '\(key)' is required.") }
        return number
    }

    func integer(_ key: String) throws -> Int64? {
        guard let value = value(key) else { return nil }
        guard let integer = value.integer else { throw MCPToolError("The argument '\(key)' must be an integer.") }
        return integer
    }

    func bool(_ key: String) throws -> Bool? {
        guard let value = value(key) else { return nil }
        guard let flag = value.bool else { throw MCPToolError("The argument '\(key)' must be true or false.") }
        return flag
    }
}
