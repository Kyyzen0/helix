import Foundation

// `--language <en|fr>` runs this launch in a language, whatever the system and the Settings say.
// Foundation reads the language as the program starts, so the program starts again with it as
// `-AppleLanguages`, which every command and the app itself then run under.
switch LanguageLaunch.plan(CommandLine.arguments) {
case .none:
    break
case .restart(let restarted):
    LanguageLaunch.restart(with: restarted)
    FileHandle.standardError.write(Data("Helix: could not start again in that language\n".utf8))
    exit(1)
case .invalid:
    FileHandle.standardError.write(Data("usage: Helix \(LanguageLaunch.usage) ...\n".utf8))
    exit(2)
}

// The language this launch is in, as the Settings window compares it to its choice.
_ = AppLanguage.atLaunch

// A command line request draws a picture and quits, before any window or menu exists.
let arguments = CommandLine.arguments
if let flag = arguments.firstIndex(of: SnapshotCommand.flag) {
    exit(SnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// The same for a picture of the node editor.
if let flag = arguments.firstIndex(of: NodeEditorSnapshotCommand.flag) {
    exit(NodeEditorSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And a picture of the inspector.
if let flag = arguments.firstIndex(of: InspectorSnapshotCommand.flag) {
    exit(InspectorSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And the timeline, and the spreadsheet of a node.
if let flag = arguments.firstIndex(of: TimelineSnapshotCommand.flag) {
    exit(TimelineSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}
if let flag = arguments.firstIndex(of: SpreadsheetSnapshotCommand.flag) {
    exit(SpreadsheetSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And the Appearance tab of the Settings window.
if let flag = arguments.firstIndex(of: SettingsSnapshotCommand.flag) {
    exit(SettingsSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And the whole document window, toolbar included.
if let flag = arguments.firstIndex(of: RenderWindowSnapshotCommand.flag) {
    exit(RenderWindowSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And the Getting Started window of the Help menu.
if let flag = arguments.firstIndex(of: GettingStartedSnapshotCommand.flag) {
    exit(GettingStartedSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// And the node palette, with a query typed in it.
if let flag = arguments.firstIndex(of: PaletteSnapshotCommand.flag) {
    exit(PaletteSnapshotCommand.run(Array(arguments[(flag + 1)...])))
}

// An AI agent starts the app with this flag and talks to it over standard input and output.
if arguments.contains(MCPServer.flag) {
    exit(MCPServer.run())
}

HelixApp.main()
