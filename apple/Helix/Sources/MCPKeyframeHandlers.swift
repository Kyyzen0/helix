import CxxStdlib
import Foundation
import HelixCore

/// The tools of the server that work on time: keyframes and the playback range.
extension MCPHandlers {
    private static func interpolation(_ text: String) throws -> helix.core.Interpolation {
        switch text {
        case "step": return .Step
        case "linear": return .Linear
        case "smooth": return .Smooth
        default: throw MCPToolError("The interpolation is step, linear or smooth, not '\(text)'.")
        }
    }

    private static func interpolationName(_ value: helix.core.Interpolation) -> String {
        switch value {
        case .Step: "step"
        case .Linear: "linear"
        case .Smooth: "smooth"
        @unknown default: "smooth"
        }
    }

    private static func trackJSON(_ track: helix.core.KeyframeTrackInfo) -> JSONValue {
        [
            "parameter": .string(String(engine: track.parameter)),
            "component": .int(Int64(track.component)),
            "base_value": .init(track.baseValue),
            "keys": .array(track.keys.map { key in
                [
                    "frame": .init(key.frame), "value": .init(key.value),
                    "interpolation": .string(interpolationName(key.interpolation)),
                ]
            }),
        ]
    }

    /// What the keys of a node look like after a change: its tracks, and the time.
    private static func keyframesResult(_ node: helix.core.NodeInfo, parameter: String? = nil) -> MCPToolOutput {
        var tracks = Array(helix.core.listKeyframes(node.id))
        if let parameter { tracks = tracks.filter { String(engine: $0.parameter) == parameter } }
        return .data(["node": identity(node), "tracks": .array(tracks.map(trackJSON)), "time": timeJSON()])
    }

    /// The component asked for, 0 when none is, checked against what the parameter holds.
    private static func component(_ arguments: Arguments, of info: helix.core.ParameterInfo) throws -> UInt32 {
        let component = try arguments.integer("component") ?? 0
        guard component >= 0, component < Int64(info.componentCount) else {
            throw MCPToolError(
                "Component \(component) is out of range, the parameter '\(String(engine: info.name))' holds \(info.componentCount)."
            )
        }
        return UInt32(component)
    }

    private static func frame(_ arguments: Arguments) throws -> Float {
        if let frame = try arguments.number("frame") { return Float(frame) }
        return helix.core.timeInfo().frame
    }

    static func listKeyframes(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        return keyframesResult(node)
    }

    static func setKeyframe(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        let info = try findParameterInfo(node, name)
        let index = try component(arguments, of: info)
        let frame = try frame(arguments)
        let style = try interpolation(try arguments.string("interpolation") ?? "smooth")

        // Without a value, the key takes the one the component shows now.
        let value: Float
        if let given = try arguments.number("value") {
            value = Float(given)
        } else {
            let current = helix.core.getFloat(node.id, std.string(name), index)
            try check(current.status)
            value = current.value
        }
        try check(helix.core.setKeyframe(node.id, std.string(name), index, frame, value, style))
        return keyframesResult(node, parameter: name)
    }

    static func removeKeyframe(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        let info = try findParameterInfo(node, name)
        let index = try component(arguments, of: info)
        try check(helix.core.removeKeyframe(node.id, std.string(name), index, try frame(arguments)))
        return keyframesResult(node, parameter: name)
    }

    static func clearKeyframes(_ arguments: Arguments) throws -> MCPToolOutput {
        let node = try resolve(arguments.values["node"])
        let name = try arguments.requiredString("parameter")
        let info = try findParameterInfo(node, name)
        // Without a component, every component of the parameter that holds keys.
        let components: [UInt32]
        if arguments.has("component") {
            components = [try component(arguments, of: info)]
        } else {
            components = Array(0..<info.componentCount)
        }
        for index in components { try check(helix.core.clearKeyframes(node.id, std.string(name), index)) }
        return keyframesResult(node, parameter: name)
    }

    static func setTimeRange(_ arguments: Arguments) throws -> MCPToolOutput {
        let start = try arguments.integer("start")
        let end = try arguments.integer("end")
        let fps = try arguments.number("fps")
        guard start != nil || end != nil || fps != nil else {
            throw MCPToolError("Give a start, an end or an fps.")
        }
        let current = helix.core.timeInfo()
        if start != nil || end != nil {
            func held(_ value: Int64?, _ fallback: Int32) throws -> Int32 {
                guard let value else { return fallback }
                guard let narrowed = Int32(exactly: value) else { throw MCPToolError("A frame of the range sits within a million of zero.") }
                return narrowed
            }
            try check(helix.core.setFrameRange(try held(start, current.startFrame), try held(end, current.endFrame)))
        }
        if let fps { try check(helix.core.setFps(Float(fps))) }
        return .data(["time": timeJSON()])
    }
}
