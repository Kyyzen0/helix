import Foundation

// A command line request draws a picture and quits, before any window or menu exists.
let arguments = CommandLine.arguments
if let flag = arguments.firstIndex(of: SnapshotCommand.flag) {
    exit(SnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// An AI agent starts the app with this flag and talks to it over standard input and output.
if arguments.contains(MCPServer.flag) {
    exit(MCPServer.run())
}

// The document controller that opens a 3D file as a new scene has to exist before SwiftUI makes
// its own, since the first one made is the shared one.
_ = HelixDocumentController()

HelixApp.main()
