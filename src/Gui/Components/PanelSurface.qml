import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import Enzo

// The chrome every pane of the application is drawn on.
//
// A rounded plate, an optional header strip along its top edge, an outline
// tracing the rounded edge, and the page background filling the corners the
// plate leaves open. Every pane in the shell uses this one component, so the
// panels read as parts of the same instrument rather than as separate boxes.
Item {
    id: root

    // The shape of the plate.
    property real radius: Theme.var.panelRadius
    property int borderWidth: 1
    property color borderColor: Theme.panel.borderColor
    property color surfaceColor: Theme.panel.surfaceColor

    // The header strip. It is drawn when the pane names itself.
    property bool showHeader: false
    property string title: ""
    property string subtitle: ""
    property color headerColor: Theme.panel.headerColor
    property color titleColor: Theme.panel.titleColor
    property color subtitleColor: Theme.panel.subtitleColor

    // The height the header strip takes.
    readonly property real headerHeight: root.showHeader ? Theme.var.space32 + Theme.var.space4 : 0

    // Whether the plate casts a shadow and lifts off the ground.
    property bool elevated: false

    // The inset between the plate's edge and the content inside it.
    property real contentInset: root.borderWidth

    // Trailing content in the header strip, such as a cluster of controls.
    property alias headerContent: headerExtra.data

    // Everything below the header strip.
    default property alias content: body.data

    // The plate the pane sits on.
    Rectangle {
        id: plate

        anchors.fill: parent
        radius: root.radius
        color: root.surfaceColor
        antialiasing: true
    }

    // The shadow the plate casts. Its source is a plain rectangle, and the
    // effect draws the shadow of that rectangle behind the plate drawn over it.
    Rectangle {
        id: shadowSource

        anchors.fill: parent
        radius: root.radius
        color: root.borderColor
    }

    MultiEffect {
        id: shadow

        source: shadowSource
        anchors.fill: shadowSource
        visible: root.elevated
        shadowEnabled: true
        shadowBlur: 1.0
        shadowOpacity: Theme.panel.shadowOpacity
        shadowVerticalOffset: Theme.panel.shadowOffset
    }

    // The header strip along the top edge of the plate.
    Rectangle {
        id: header

        visible: root.showHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.headerHeight
        color: root.headerColor
        topLeftRadius: root.radius
        topRightRadius: root.radius
        bottomLeftRadius: 0
        bottomRightRadius: 0
    }

    // The divider that closes the header strip off from the body.
    Rectangle {
        visible: root.showHeader
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.panel.dividerColor
    }

    // The name of the pane, and what it holds.
    Row {
        id: headerRow

        visible: root.showHeader && root.title !== ""
        anchors.verticalCenter: header.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.var.space12
        spacing: Theme.var.space8

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: root.titleColor
            font.family: Theme.var.fontSans
            font.pixelSize: Theme.var.textCaption
            font.weight: Font.DemiBold
            font.letterSpacing: 1.1
            font.capitalization: Font.AllUppercase
        }

        Text {
            visible: root.subtitle !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.subtitle
            color: root.subtitleColor
            font.family: Theme.var.fontSans
            font.pixelSize: Theme.var.textCaption
            elide: Text.ElideRight
        }
    }

    // The trailing content of the header strip.
    Item {
        id: headerExtra

        visible: root.showHeader
        anchors.top: header.top
        anchors.bottom: header.bottom
        anchors.left: headerRow.right
        anchors.leftMargin: Theme.var.space8
        anchors.right: parent.right
        anchors.rightMargin: Theme.var.space8
    }

    // The body of the pane, everything below the header strip.
    Item {
        id: body

        anchors.top: root.showHeader ? header.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.contentInset
    }

    // The outline tracing the rounded edge, drawn over the content so nothing
    // paints past it.
    Shape {
        id: frame

        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        z: 100

        // The page background fills the corners, hiding content past the radius.
        ShapePath {
            fillColor: Theme.var.background
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            PathRectangle {
                width: frame.width
                height: frame.height
            }
            PathRectangle {
                width: frame.width
                height: frame.height
                radius: root.radius
            }
        }

        // The border tracing the rounded edge.
        ShapePath {
            fillColor: "transparent"
            strokeColor: root.borderColor
            strokeWidth: root.borderWidth
            PathRectangle {
                x: root.borderWidth / 2
                y: root.borderWidth / 2
                width: frame.width - root.borderWidth
                height: frame.height - root.borderWidth
                radius: root.radius
            }
        }
    }
}
