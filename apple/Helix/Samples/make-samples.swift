// Writes the sample scenes of this folder through the HelixCore facade, so they are files the
// engine itself wrote and a later version of the format can rewrite.
//
//   make-samples <nodes folder> <node library folder> <folder holding daslib> <output folder>

import Foundation
import HelixCore

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data(("make-samples: " + message + "\n").utf8))
    exit(1)
}

func check(_ status: helix.core.Status, _ what: String) {
    guard status.ok else { fail(what + ": " + String(status.error)) }
}

let arguments = CommandLine.arguments
guard arguments.count == 5 else {
    fail("usage: make-samples <nodes folder> <node library folder> <folder holding daslib> <output folder>")
}
let output = URL(fileURLWithPath: arguments[4])

var options = helix.core.InitOptions()
options.nodesDirectory = std.string(arguments[1])
options.librariesDirectory = std.string(arguments[2])
options.runtimeDirectory = std.string(arguments[3])
check(helix.core.initialize(options), "initialize")

func node(_ type: String, _ name: String, _ x: Float, _ y: Float) -> helix.core.NodeId {
    let created = helix.core.createNode(std.string(type), std.string(name), x, y)
    check(created.status, "createNode " + type)
    return created.id
}

// Cube.kut: what a new scene holds, a cube that is shown.
check(helix.core.newScene(), "newScene")
let cube = node("helix::cube", "cube1", 0, 0)
check(helix.core.setDisplayNode(cube), "setDisplayNode")
check(helix.core.saveScene(std.string(output.appending(path: "Cube.kut").path)), "save Cube.kut")

// Waves.kut: a grid bent into rings by a sine wave, with a transform on the end that is the node
// shown. Three nodes in a chain, so the file holds links and a display node that is not the first.
check(helix.core.newScene(), "newScene")
let grid = node("helix::grid", "grid1", 0, 0)
check(helix.core.setInt(grid, "rows", 0, 60), "rows")
check(helix.core.setInt(grid, "columns", 0, 60), "columns")
check(helix.core.setFloat(grid, "size", 0, 12), "size x")
check(helix.core.setFloat(grid, "size", 1, 12), "size y")
let wave = node("helix::sineWave", "wave1", 0, 120)
check(helix.core.setInt(wave, "radial", 0, 1), "radial")
check(helix.core.setFloat(wave, "frequency", 0, 0.6), "frequency")
check(helix.core.setFloat(wave, "amplitude", 0, 0.8), "amplitude")
let lift = node("helix::transform", "lift1", 0, 240)
check(helix.core.setFloat(lift, "translate", 1, 0.5), "translate")
check(helix.core.connectNodes(grid, 0, wave, 0), "connect grid to wave")
check(helix.core.connectNodes(wave, 0, lift, 0), "connect wave to lift")
check(helix.core.setDisplayNode(lift), "setDisplayNode")
check(helix.core.saveScene(std.string(output.appending(path: "Waves.kut").path)), "save Waves.kut")
print("wrote Cube.kut and Waves.kut")
