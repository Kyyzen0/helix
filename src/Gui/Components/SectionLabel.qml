import QtQuick
import Helix

// A small uppercase label naming a group of things.
//
// Used for the eyebrows in panel headers, the groups in the settings page and
// the field names in the status bar. The letter spacing is what makes a run of
// capitals read as a label rather than as shouting.
Text {
    id: root

    // The colour the label is drawn in.
    property color labelColor: Theme.var.textMuted

    // The extra space between the letters, in pixels.
    property real tracking: 1.1

    color: root.labelColor
    font.family: Theme.var.fontSans
    font.pixelSize: Theme.var.textMicro
    font.weight: Font.DemiBold
    font.letterSpacing: root.tracking
    font.capitalization: Font.AllUppercase
    elide: Text.ElideRight
    renderType: Text.NativeRendering
}
