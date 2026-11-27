import Foundation

// A command line request draws a picture and quits, before any window or menu exists.
let arguments = CommandLine.arguments
if let flag = arguments.firstIndex(of: SnapshotCommand.flag) {
    exit(SnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// The document controller that opens a 3D file as a new scene has to exist before SwiftUI makes
// its own, since the first one made is the shared one.
_ = HelixDocumentController()

HelixApp.main()
