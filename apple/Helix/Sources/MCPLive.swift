import CxxStdlib
import Foundation
import HelixCore

/// What the tools do when they act on the scene open in a window: the live side of the server.
///
/// The handlers of `MCPHandlers` call the facade and know nothing of windows. Here each call is put
/// where the window can see it: a call that changes the scene is one step of the window's undo,
/// named for the Edit menu; a call that only reads runs as it is; the calls that would throw the
/// person's work away ask them first. The headless server does none of this.
@MainActor
enum MCPLive {
    /// What an agent hears when no window holds a scene. English, like every reply to an agent.
    static let noSceneMessage = "No scene is open in Helix. Open or create one first." // l10n-ok: for an agent, in English

    /// The tools that look and change nothing the window shows, which run as they are.
    private static let reading: Set<String> = [
        "scene_info", "list_node_types", "list_parameters", "geometry_summary", "read_attribute",
        "list_keyframes", "clear_cook_cache", "export_geometry",
    ]

    /// Runs one tool call on the scene that is open.
    static func run(_ tool: MCPTool, arguments: JSONValue) async throws -> MCPToolOutput {
        switch tool.name {
        case "new_scene":
            return try await MCPLiveScenes.newScene()
        case "open_scene":
            return try await MCPLiveScenes.openScene(Arguments(values: arguments))
        default:
            break
        }
        guard let document = EngineHost.shared.activeDocument, document.isActive else {
            throw MCPToolError(noSceneMessage)
        }
        let output: MCPToolOutput
        do {
            output = try await runOnDocument(document, tool, arguments)
        } catch {
            debugLog(document, tool.name, error: error)
            throw error
        }
        debugLog(document, tool.name, error: nil)
        return output
    }

    private static func runOnDocument(_ document: HelixDocument, _ tool: MCPTool, _ arguments: JSONValue) async throws -> MCPToolOutput {
        if reading.contains(tool.name) { return try tool.handler(arguments) }
        switch tool.name {
        case "set_frame":
            let output = try tool.handler(arguments)
            document.showFrameSetByAgent()
            return output
        case "save_scene":
            let path = try Arguments(values: arguments).string("path").map(MCPHandlers.expanded)
            let url = try await document.saveForAgent(to: path)
            return .data([
                "saved": .string(url.path), "node_count": .int(Int64(MCPHandlers.nodeList().count)),
                "modified": .bool(helix.core.isSceneModified()),
            ])
        case "import_file":
            return try await importFile(document, tool, arguments)
        case "render_view":
            return try renderView(document, tool, arguments)
        default:
            guard let name = actionName(tool.name) else { throw MCPToolError("Unknown tool: \(tool.name).") } // l10n-ok: for an agent, in English
            let prepared = tool.name == "create_node" ? placed(arguments, in: document) : arguments
            return try document.performToolEdit(named: name) {
                let output = try tool.handler(prepared)
                // `Scripts/live-smoke.py` proves that a call which fails after it changed the scene
                // leaves nothing, by making one fail.
                if ProcessInfo.processInfo.environment["HELIX_DEBUG_LIVE_FAIL_AFTER"] == tool.name {
                    throw MCPToolError("Failed on purpose, after the change.") // l10n-ok: a debug check
                }
                return output
            }
        }
    }

    // MARK: The calls that are more than one step

    /// The file is read off the main thread behind the sheet of the window, as the person's own
    /// import is, and the node is made once it is read. The call returns when it is done.
    private static func importFile(_ document: HelixDocument, _ tool: MCPTool, _ arguments: JSONValue) async throws -> MCPToolOutput {
        let path = MCPHandlers.expanded(try Arguments(values: arguments).requiredString("path"))
        let url = URL(fileURLWithPath: path)
        guard FileManager.default.fileExists(atPath: path) else { throw MCPToolError("There is no file at \(path).") } // l10n-ok: for an agent, in English
        guard document.importProgress == nil else {
            throw MCPToolError("Another import is going on in Helix. Try again when it is done.") // l10n-ok: for an agent, in English
        }
        switch await document.readForImport(url) {
        case .ready:
            break
        case .cancelled:
            throw MCPToolError("The person cancelled the import.") // l10n-ok: for an agent, in English
        case .failed(let message):
            throw MCPToolError(message)
        }
        guard document.isActive else { throw MCPToolError("The scene was closed while the file was read.") } // l10n-ok: for an agent, in English
        let name = String(localized: "AI: Import File")
        return try document.performToolEdit(named: name) { try tool.handler(arguments) }
    }

    /// A picture that leaves the window as it was: the display node and the frame are given back
    /// by the handler, and so is the camera, which the picture moves to frame the geometry.
    private static func renderView(_ document: HelixDocument, _ tool: MCPTool, _ arguments: JSONValue) throws -> MCPToolOutput {
        defer {
            // The viewport holds the geometry of the node drawn; it is cooked again for the node
            // the window shows, and drawn.
            document.cookGeometry(reframe: false)
        }
        return try tool.handler(arguments)
    }

    /// A node made with no place asked for goes to a free spot below the others, where the person
    /// sees it, and not on top of them at the origin.
    private static func placed(_ arguments: JSONValue, in document: HelixDocument) -> JSONValue {
        guard var fields = arguments.object, fields["x"] == nil || fields["x"]?.isNull == true,
              fields["y"] == nil || fields["y"]?.isNull == true
        else { return arguments }
        let graph = document.editor.graph
        let start: CGPoint
        if let lowest = graph.nodes.max(by: { $0.position.y < $1.position.y }) {
            start = CGPoint(x: lowest.position.x, y: lowest.position.y + EditorLayout.chainSpacing)
        } else {
            start = .zero
        }
        let spot = NodePlacement.free(start, in: graph)
        fields["x"] = .init(Float(spot.x))
        fields["y"] = .init(Float(spot.y))
        return .object(fields)
    }

    // MARK: Names

    /// The name of the step of undo a tool makes, for Edit > Undo. Nil for a tool that is not one
    /// step of the scene.
    static func actionName(_ tool: String) -> String? {
        switch tool {
        case "create_node": String(localized: "AI: Create Node")
        case "delete_node": String(localized: "AI: Delete Node")
        case "connect_nodes": String(localized: "AI: Connect Nodes")
        case "disconnect_nodes": String(localized: "AI: Disconnect Nodes")
        case "set_display_node": String(localized: "AI: Set Display Node")
        case "set_parameter": String(localized: "AI: Set Parameter")
        case "reset_parameter": String(localized: "AI: Reset Parameter")
        case "set_time_range": String(localized: "AI: Set Time Range")
        case "animate_skeleton": String(localized: "AI: Animate Skeleton")
        case "set_keyframe": String(localized: "AI: Set Keyframe")
        case "remove_keyframe": String(localized: "AI: Remove Keyframe")
        case "clear_keyframes": String(localized: "AI: Clear Keyframes")
        case "import_file": String(localized: "AI: Import File")
        default: nil
        }
    }

    /// What an agent did, in a few words for the list of the status item.
    static func activityText(_ tool: String) -> String {
        switch tool {
        case "scene_info": String(localized: "Looked at the scene")
        case "list_node_types": String(localized: "Looked up the node types")
        case "list_parameters": String(localized: "Read the parameters of a node")
        case "geometry_summary": String(localized: "Measured the geometry")
        case "read_attribute": String(localized: "Read an attribute")
        case "list_keyframes": String(localized: "Read the keyframes")
        case "clear_cook_cache": String(localized: "Cleared the cook cache")
        case "export_geometry": String(localized: "Exported the geometry")
        case "render_view": String(localized: "Took a picture of the view")
        case "set_frame": String(localized: "Moved to a frame")
        case "save_scene": String(localized: "Saved the scene")
        case "new_scene": String(localized: "Asked for a new scene")
        case "open_scene": String(localized: "Asked to open a scene")
        case "create_node": String(localized: "Created a node")
        case "delete_node": String(localized: "Deleted a node")
        case "connect_nodes": String(localized: "Connected two nodes")
        case "disconnect_nodes": String(localized: "Disconnected two nodes")
        case "set_display_node": String(localized: "Chose the node to show")
        case "set_parameter": String(localized: "Set a parameter")
        case "reset_parameter": String(localized: "Reset a parameter")
        case "set_time_range": String(localized: "Set the time range")
        case "animate_skeleton": String(localized: "Animated a skeleton")
        case "set_keyframe": String(localized: "Set a keyframe")
        case "remove_keyframe": String(localized: "Removed a keyframe")
        case "clear_keyframes": String(localized: "Cleared the keyframes")
        case "import_file": String(localized: "Imported a file")
        default: String(localized: "Used a tool")
        }
    }

    // MARK: Debug

    /// With `HELIX_DEBUG_LAUNCH=1`, says what a call did to the open scene, for `Scripts/live-smoke.py`:
    /// the nodes the document holds now, the display node, the step of undo on top, how many steps
    /// the agents made, and where the viewport camera is.
    private static func debugLog(_ document: HelixDocument, _ tool: String, error: Error?) {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        let paths = MCPHandlers.nodeList().map { String(engine: $0.path) }.joined(separator: ",")
        let display = document.displayNodeName ?? "none"
        let undo = document.undoManager?.undoActionName ?? ""
        let camera = helix.core.viewportCameraState()
        let cameraText = String(
            format: "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f", camera.centerX, camera.centerY, camera.centerZ, camera.distance,
            camera.yaw, camera.pitch
        )
        let frame = helix.core.timeInfo().frame
        let failed = error == nil ? "none" : (error as? MCPToolError)?.message ?? error!.localizedDescription
        HelixDocument.debugLog( // l10n-ok: read by live-smoke.py
            "live tool=\(tool) error=\(failed) nodes=\(paths) display=\(display) undo=\(undo) steps=\(AgentLink.shared.registeredSteps) camera=\(cameraText) frame=\(frame) edited=\(document.systemDocument?.isDocumentEdited ?? false)"
        )
    }
}
