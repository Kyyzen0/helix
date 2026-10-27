import QtQuick
import Helix
import "."

// Rounds and clips panel contents.
//
// The chrome itself lives in PanelSurface, so every panel in the application is
// drawn by the same code. This wrapper keeps the plain panel, without a header,
// that the rest of the interface is written against.
Item {
    id: root

    property real radius: Theme.var.panelRadius
    property real borderWidth: 1
    property color borderColor: Theme.var.border

    // Place content inside container
    default property alias content: surface.content

    PanelSurface {
        id: surface

        anchors.fill: parent
        radius: root.radius
        borderWidth: root.borderWidth
        borderColor: root.borderColor
    }
}
