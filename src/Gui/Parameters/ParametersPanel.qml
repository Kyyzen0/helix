import QtQuick
import QtQuick.Effects
import QtQuick.VectorImage
import Helix
import "../Components"
import "../Utils.js" as Utils

// The panel showing the parameters of the primary node.
//
// It fills the pane it sits in, and carries the name of the node it belongs to
// in the same hue its card carries in the graph, so the two read as one thing.
Item {
    id: panel

    // Layout constants.
    readonly property real defaultWidth: 500
    readonly property real defaultHeight: 420
    readonly property real minWidth: 200
    readonly property real minHeight: 120
    // Space above and below the panel's content.
    readonly property real verticalMargin: 12
    // Inset between the panel edge and its content.
    readonly property real sideMargin: 16
    // Extra left padding inside each parameter row. Not all parameters will use this.
    readonly property real parameterInset: 20
    readonly property real maxHeightInset: 28
    readonly property real gripSize: 18
    readonly property real radius: Theme.var.panelRadius

    // Whether the panel fills the pane it sits in, rather than floating over the
    // canvas with a size of its own.
    property bool docked: false

    width: panel.docked && parent ? parent.width : panel.defaultWidth
    visible: parameters.hasNode

    // Height follows the content until the user drags the resize grip, which
    // assigns an explicit size and takes over.
    implicitHeight: Math.max(defaultHeight, header.height + verticalMargin * 2 + scroller.contentHeight)
    height: panel.docked && parent
        ? parent.height
        : Math.min(implicitHeight, parent ? parent.height - maxHeightInset : implicitHeight)

    // The hue the node's family is drawn in, the same one its card carries.
    readonly property color signalColor: signalPalette[Utils.signalIndex(parameters.nodeFamily)]

    // The hues a family can take.
    readonly property var signalPalette: [
        Theme.var.signalBlue,
        Theme.var.signalTeal,
        Theme.var.signalGreen,
        Theme.var.signalAmber,
        Theme.var.signalRose,
        Theme.var.signalViolet
    ]

    // Lifts the panel off the canvas behind it, so its edges stay readable
    // against a bright surface.
    Rectangle {
        id: shadowCaster
        anchors.fill: parent
        radius: panel.radius
        color: Theme.parameter.panelColor
        visible: !panel.docked
    }

    MultiEffect {
        source: shadowCaster
        anchors.fill: shadowCaster
        visible: !panel.docked
        shadowEnabled: true
        shadowBlur: 1.0
        shadowOpacity: Theme.panel.shadowOpacity
        shadowVerticalOffset: Theme.panel.shadowOffset
    }

    // The panel body, clipped to the rounded outline.
    Rectangle {
        id: body

        anchors.fill: parent
        clip: true
        radius: panel.radius
        color: panel.docked ? Theme.panel.surfaceColor : Theme.parameter.panelColor

        // Gives the panel keyboard focus on hover, without blocking clicks to its
        // controls or stealing focus from a field being edited. Swallows the
        // wheel so it never reaches the network behind.
        MouseArea {
            id: panelHover

            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            onWheel: wheel => wheel.accepted = true
        }

        FocusReclaimer {
            target: panel
            area: panelHover
        }

        // Title strip naming the node the parameters belong to.
        Rectangle {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: headerRow.implicitHeight + panel.verticalMargin * 2
            visible: parameters.hasNode
            topLeftRadius: panel.radius
            topRightRadius: panel.radius
            color: Theme.panel.headerColor

            // The bar down the leading edge, in the hue of the node's family.
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.node.spineWidth
                height: parent.height - Theme.var.space16
                radius: width / 2
                color: panel.signalColor
            }

            Row {
                id: headerRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: panel.sideMargin
                spacing: Theme.var.space8

                VectorImage {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    preferredRendererType: VectorImage.CurveRenderer
                    fillMode: VectorImage.PreserveAspectFit
                    source: parameters.nodeIconUrl
                }

                Column {
                    spacing: 1

                    Text {
                        text: parameters.nodeType
                        color: panel.signalColor
                        font.family: Theme.var.fontSans
                        font.pixelSize: Theme.var.textMicro
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.9
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        text: parameters.nodeName
                        color: Theme.panel.titleColor
                        font.family: Theme.var.fontSans
                        font.pixelSize: Theme.var.textTitle
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Rectangle {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.panel.dividerColor
            visible: parameters.hasNode
        }

        // Spans the full body width so the scrollbar sits in the side margin
        // rather than over a parameter row.
        ScrollArea {
            id: scroller

            anchors.top: header.bottom
            anchors.topMargin: panel.verticalMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: panel.verticalMargin
            anchors.left: parent.left
            anchors.right: parent.right

            ParameterList {
                anchors.left: parent.left
                anchors.leftMargin: panel.sideMargin
                anchors.right: parent.right
                anchors.rightMargin: panel.sideMargin
                contentInset: panel.parameterInset
                model: parameters.parameters
            }
        }

        // Drawn last so the border isn't painted over by the header fill.
        Rectangle {
            anchors.fill: parent
            radius: panel.radius
            color: "transparent"
            border.color: Theme.panel.borderColor
        }

        // Shown when nothing is being worked on, so the pane is not blank.
        Column {
            anchors.centerIn: parent
            width: parent.width - Theme.var.space32
            spacing: Theme.var.space6
            visible: !parameters.hasNode

            Text {
                width: parent.width
                text: "No node selected"
                color: Theme.var.textLabel
                font.family: Theme.var.fontSans
                font.pixelSize: Theme.var.textBody
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "Pick a node in the graph to see its parameters."
                color: Theme.var.textMuted
                font.family: Theme.var.fontSans
                font.pixelSize: Theme.var.textCaption
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Drag the bottom left corner to resize. The top right stays pinned, so the
    // panel grows left and down. Only the floating panel can be resized.
    MouseArea {
        id: grip

        visible: !panel.docked
        width: panel.gripSize
        height: panel.gripSize
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeBDiagCursor

        property point origin
        property real startWidth
        property real startHeight

        onPressed: mouse => {
            origin = mapToItem(null, mouse.x, mouse.y);
            startWidth = panel.width;
            startHeight = panel.height;
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return;
            let here = mapToItem(null, mouse.x, mouse.y);
            panel.width = Math.max(panel.minWidth, startWidth - (here.x - origin.x));
            panel.height = Math.max(panel.minHeight, startHeight + (here.y - origin.y));
        }
    }
}
