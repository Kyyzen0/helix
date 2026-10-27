import QtQuick
import QtQuick.Effects
import QtQuick.VectorImage
import Helix
import "../Utils.js" as Utils

Item {
    id: root

    width: network.nodeWidth
    height: network.nodeHeight

    // The node's position on the canvas, centered on the card.
    property real modelX: 0
    property real modelY: 0

    x: modelX - width / 2
    y: modelY - height / 2

    property color fillColor: Theme.node.cardColor
    property color borderColor: Theme.node.borderColor

    property var nodeId
    property string label: "Grid"
    property string typeLabel
    property url iconUrl
    property real radius: 8
    property real viewZoom: 1
    property bool selected: false
    property bool primary: false
    property bool display: false
    property int inputPortCount: 0
    property int outputPortCount: 0

    // The family of the node, the namespace its type was published under.
    property string family: ""

    // Whether the last input port takes any number of inputs.
    property bool multiInput: false

    // While a link is being drawn the card lets presses through so the canvas can
    // grab and drop ports that sit beneath it.
    property bool linking: false

    // The index of the input or output to highlight, or -1 for none.
    property int highlightedInput: -1
    property int highlightedOutput: -1

    // The primary and selected nodes both carry an accent outline.
    readonly property bool highlighted: selected || primary

    // The hue the node's family is drawn in, so a graph reads as a set of
    // colour coded groups rather than a field of unrelated cards.
    readonly property color signalColor: signalPalette[Utils.signalIndex(root.family)]

    // The hues a family can take.
    readonly property var signalPalette: [
        Theme.var.signalBlue,
        Theme.var.signalTeal,
        Theme.var.signalGreen,
        Theme.var.signalAmber,
        Theme.var.signalRose,
        Theme.var.signalViolet
    ]

    // Emitted when the display flag is clicked, to show this node's geometry.
    signal displayToggled

    // Emitted on mouse down, additive when a modifier is held.
    signal pressed(bool additive)

    // Emitted on a release that was a click rather than a drag.
    signal clicked(bool additive)

    // The offset between the cursor and the grab point since last frame.
    signal dragMoved(real dx, real dy)

    // Emitted when a drag finishes, so the new position can be committed.
    signal dragReleased

    // The visible node card.
    Item {
        id: card

        anchors.fill: parent

        // Drag logic
        MouseArea {
            id: mouseArea

            anchors.fill: parent
            enabled: !root.linking
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            property real dragStartX: 0
            property real dragStartY: 0
            property bool dragged: false
            property bool pressAdditive: false

            onPressed: mouse => {
                dragStartX = mouse.x;
                dragStartY = mouse.y;
                dragged = false;
                pressAdditive = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0;
                root.pressed(pressAdditive);
            }
            onPositionChanged: mouse => {
                root.dragMoved(mouse.x - dragStartX, mouse.y - dragStartY);
                dragged = true;
            }
            onReleased: {
                if (dragged)
                    root.dragReleased();
                else
                    root.clicked(pressAdditive);
            }
        }

        // Drop Shadow
        Rectangle {
            id: dropShadowRect
            anchors.fill: parent
            radius: root.radius
            color: root.fillColor
        }
        MultiEffect {
            id: shadow
            source: dropShadowRect
            anchors.fill: dropShadowRect
            shadowEnabled: true
            shadowBlur: 0.7
            shadowOpacity: Theme.node.shadowOpacity
            shadowVerticalOffset: 3
        }

        // Main shape
        Rectangle {
            id: nodeShape
            anchors.fill: parent
            color: root.fillColor
            radius: root.radius
            antialiasing: true
            border.pixelAligned: false
            border.color: root.primary ? Theme.var.accentBright : (root.highlighted ? Theme.var.accent : (mouseArea.containsMouse ? Theme.node.hoverBorderColor : root.borderColor))
            border.width: (root.primary ? 2 : (root.highlighted ? 1.5 : 1)) / root.viewZoom
        }

        // The bar down the leading edge, in the hue of the node's family.
        Rectangle {
            id: spine

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.node.spineWidth
            height: parent.height - Theme.var.space16
            radius: width / 2
            color: root.signalColor
            opacity: root.highlighted ? 1 : 0.85
        }

        // The chip the icon sits in, in the hue of the node's family.
        Rectangle {
            id: iconChip

            anchors.verticalCenter: parent.verticalCenter
            x: Theme.var.space12
            width: 20
            height: 20
            radius: 5
            color: Qt.rgba(root.signalColor.r, root.signalColor.g, root.signalColor.b, 0.16)
        }

        VectorImage {
            id: icon

            width: 13
            height: 13
            anchors.centerIn: iconChip
            preferredRendererType: VectorImage.CurveRenderer
            fillMode: VectorImage.PreserveAspectFit
            source: root.iconUrl
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: iconChip.x + iconChip.width + Theme.var.space8
            width: parent.width - x - Theme.var.space8
            spacing: 1

            // The kind of node, in the hue of the family it belongs to.
            Text {
                width: parent.width
                text: root.typeLabel
                color: root.signalColor
                font.family: Theme.var.fontSans
                font.pixelSize: Theme.var.textMicro
                font.weight: Font.DemiBold
                font.letterSpacing: 0.5
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
            }

            // The name this node was given.
            Text {
                width: parent.width
                text: root.label
                color: Theme.node.titleColor
                font.family: Theme.var.fontSans
                font.pixelSize: Theme.var.textBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        // Display flag inside the right edge, marking which node feeds the viewport.
        Rectangle {
            id: displayTab

            property real rightMargin: 3

            width: 5
            height: parent.height * 0.55
            radius: 2.5
            anchors.verticalCenter: parent.verticalCenter
            x: parent.width - rightMargin - width

            // Dim at rest, the flag brightens on hover and lights up when active.
            color: Theme.node.displayFlagColor
            opacity: 0.35
            states: [
                State {
                    name: "hovered"
                    when: flagMouse.containsMouse && !root.display
                    PropertyChanges {
                        displayTab.opacity: 0.75
                    }
                },
                State {
                    name: "active"
                    when: root.display
                    PropertyChanges {
                        displayTab.opacity: 1
                    }
                }
            ]
            Behavior on opacity {
                NumberAnimation {
                    duration: 90
                }
            }

            MouseArea {
                id: flagMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                onClicked: root.displayToggled()
            }
        }
    }

    // One port, the place along a card edge where the link layer anchors its
    // curves. Hit testing lives on the canvas, so this is purely the visual mark
    // and lights up only while it is the highlighted port.
    component Port: Item {
        id: portDot

        // The stretch of card edge this port covers.
        property rect box

        property bool active: false

        // Whether this port holds any number of inputs.
        property bool multiInput: false

        x: box.x + box.width / 2
        y: box.y

        Rectangle {
            // A multi input port fills the edge it covers, a single port marks its center.
            width: portDot.multiInput ? portDot.box.width : Theme.node.portSize
            height: portDot.multiInput ? 4 : Theme.node.portSize
            radius: portDot.multiInput ? 2 : 2.5
            x: -width / 2
            y: -height / 2

            // The socket brightens once it is the closest port, to invite a drag.
            color: portDot.active ? Theme.var.accent : Theme.node.portColor
            scale: portDot.active && !portDot.multiInput ? 1.25 : 1
            Behavior on color {
                ColorAnimation {
                    duration: 90
                }
            }
            Behavior on scale {
                NumberAnimation {
                    duration: 90
                }
            }
        }
    }

    // Input ports along the top edge, output ports along the bottom.
    Repeater {
        model: root.inputPortCount
        delegate: Port {
            required property int index

            box: network.nodes.getPortBox(root.nodeId, index, false)

            // Holds every input from the last declared port onward.
            multiInput: root.multiInput && index === root.inputPortCount - 1
            active: multiInput ? root.highlightedInput >= index : root.highlightedInput === index
        }
    }
    Repeater {
        model: root.outputPortCount
        delegate: Port {
            required property int index

            box: network.nodes.getPortBox(root.nodeId, index, true)
            active: root.highlightedOutput === index
        }
    }
}
