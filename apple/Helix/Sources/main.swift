import Foundation

// A command line request draws a picture and quits, before any window or menu exists.
let arguments = CommandLine.arguments
if let flag = arguments.firstIndex(of: SnapshotCommand.flag) {
    exit(SnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

HelixApp.main()
