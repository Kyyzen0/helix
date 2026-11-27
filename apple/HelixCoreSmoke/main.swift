// A command line proof that Swift can import the HelixCore facade and drive the engine:
// it starts the engine, creates a cube, cooks it and prints what came out.
//
//   HelixCoreSmoke <nodes folder> <node library folder> <folder holding daslib>

import Foundation
import HelixCore

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data(("HelixCoreSmoke: " + message + "\n").utf8))
    exit(1)
}

let arguments = CommandLine.arguments
guard arguments.count == 4 else {
    fail("usage: HelixCoreSmoke <nodes folder> <node library folder> <folder holding daslib>")
}

var options = helix.core.InitOptions()
options.nodesDirectory = std.string(arguments[1])
options.librariesDirectory = std.string(arguments[2])
options.runtimeDirectory = std.string(arguments[3])

let started = helix.core.initialize(options)
guard started.ok else { fail("initialize: " + String(started.error)) }
print("Helix engine \(String(helix.core.version()))")
print("node types: \(helix.core.listNodeTypes().count)")

let cube = helix.core.createNode("helix::cube", "", 0, 0)
guard cube.status.ok else { fail("createNode: " + String(cube.status.error)) }
guard helix.core.setDisplayNode(cube.id).ok else { fail("setDisplayNode") }

let summary = helix.core.cookDisplaySummary()
guard summary.status.ok else { fail("cook: " + String(summary.status.error)) }
print("cube: \(summary.pointCount) points, \(summary.faceCount) faces, "
      + "\(summary.triangleCount) triangles, \(summary.primitiveCount) primitive")

guard helix.core.setVector3(cube.id, "size", 2, 2, 2).ok else { fail("setVector3") }
let buffers = helix.core.cookDisplayBuffers()
guard buffers.status.ok else { fail("cook buffers: " + String(buffers.status.error)) }
print("buffers: \(buffers.positions.size() / 3) vertices, \(buffers.triangleIndices.size() / 3) triangles")

let size = helix.core.getVector3(cube.id, "size")
print("size after the edit: \(size.x) \(size.y) \(size.z)")

// A failure comes back as a value, not as an exception.
let refused = helix.core.createNode("helix::nothing", "", 0, 0)
print("unknown type: ok=\(refused.status.ok) error=\"\(String(refused.status.error))\"")

guard summary.pointCount == 8, summary.triangleCount == 12, !refused.status.ok else {
    fail("unexpected geometry")
}
print("OK")
