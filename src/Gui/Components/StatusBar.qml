import QtQuick
import Enzo

// The strip along the bottom of the window reporting what the application is
// doing right now.
//
// It reads left to right as a sentence about the scene: the state of the cook,
// then where the playhead sits, then how much of the graph is selected.
Item {
    id: root

    // The state of the cook, one of idle, pending, cooking or error.
    property string cookState: "idle"

    // The frame the scene sits on, and the range it can be played over.
    property real frame: 0
    property int startFrame: 0
    property int endFrame: 0
    property real fps: 24

    // How much of the graph holds nodes, and how many of them are selected.
    property int nodeCount: 0
    property int selectedCount: 0

    // The name of the scene on disk.
    property string sceneName: ""

    // The word naming the state of the cook.
    readonly property string cookLabel: {
        switch (root.cookState) {
        case "pending":
            return "Queued";
        case "cooking":
            return "Cooking";
        case "error":
            return "Cook failed";
        }
        return "Ready";
    }

    // The colour the state is shown in.
    readonly property color cookColor: {
        switch (root.cookState) {
        case "pending":
        case "cooking":
            return Theme.statusBar.busyColor;
        case "error":
            return Theme.statusBar.errorColor;
        }
        return Theme.statusBar.readyColor;
    }

    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        color: Theme.statusBar.surfaceColor
    }

    // The divider that separates the strip from the panes above it.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.statusBar.borderColor
    }

    Row {
        id: leftGroup

        anchors.left: parent.left
        anchors.leftMargin: Theme.var.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.var.space8

        // The state of the cook, as a dot and a word.
        Rectangle {
            id: stateDot

            anchors.verticalCenter: parent.verticalCenter
            width: 7
            height: 7
            radius: 3.5
            color: root.cookColor

            // The dot breathes while a cook is running.
            SequentialAnimation on opacity {
                running: root.cookState === "cooking"
                loops: Animation.Infinite
                NumberAnimation {
                    to: 0.25
                    duration: 520
                    easing.type: Easing.InOutQuad
                }
                NumberAnimation {
                    to: 1
                    duration: 520
                    easing.type: Easing.InOutQuad
                }
                onStopped: stateDot.opacity = 1
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.cookLabel
            color: root.cookColor
            font.family: Theme.var.fontSans
            font.pixelSize: Theme.var.textCaption
            font.weight: Font.DemiBold
        }
    }

    // The readouts, each a field name and its value.
    Row {
        id: readouts

        anchors.left: leftGroup.right
        anchors.leftMargin: Theme.var.space20
        anchors.right: sceneLabel.left
        anchors.rightMargin: Theme.var.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.var.space20

        // The frame the scene sits on, over the range it can be played over.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.var.space6

            SectionLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: "Frame"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(root.frame)
                color: Theme.statusBar.valueColor
                font.family: Theme.var.fontMono
                font.pixelSize: Theme.var.textCaption
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "/ " + root.endFrame
                color: Theme.statusBar.labelColor
                font.family: Theme.var.fontMono
                font.pixelSize: Theme.var.textCaption
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "@ " + root.fps + " fps"
                color: Theme.statusBar.labelColor
                font.family: Theme.var.fontMono
                font.pixelSize: Theme.var.textCaption
            }
        }

        // How much of the graph holds nodes, and how many of them are picked.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.var.space6

            SectionLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: "Nodes"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.nodeCount
                color: Theme.statusBar.valueColor
                font.family: Theme.var.fontMono
                font.pixelSize: Theme.var.textCaption
            }

            Text {
                visible: root.selectedCount > 0
                anchors.verticalCenter: parent.verticalCenter
                text: "· " + root.selectedCount + " selected"
                color: Theme.var.accentBright
                font.family: Theme.var.fontMono
                font.pixelSize: Theme.var.textCaption
            }
        }
    }

    // The scene on disk, on the trailing edge.
    Text {
        id: sceneLabel

        anchors.right: parent.right
        anchors.rightMargin: Theme.var.space12
        anchors.verticalCenter: parent.verticalCenter
        text: root.sceneName
        color: Theme.statusBar.labelColor
        font.family: Theme.var.fontSans
        font.pixelSize: Theme.var.textCaption
        elide: Text.ElideLeft
    }
}
