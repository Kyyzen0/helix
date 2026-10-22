import QtQuick
import Enzo

// A drag selection box for nodes.
//
// The box is a wash of the accent over the nodes it covers, with a bracket at
// each corner. The brackets keep the weight of the outline off the nodes
// themselves while still showing exactly where the box reaches.
Rectangle {
    id: root

    property real viewZoom: 1

    property bool sweeping: false

    // Whether a press could still grow into a box.
    property bool waiting: false

    // Whether the release selected the boxed nodes, so the click that follows it
    // is left alone.
    property bool applied: false

    // Whether the boxed nodes join the selection instead of replacing it.
    property bool additive: false

    // The canvas points the box spans, the press anchor and the cursor.
    property point anchor
    property point cursor

    // The weight of the brackets, which scales back with the zoom so they keep
    // the same size on screen.
    readonly property real weight: 1 / root.viewZoom

    // Anchors the box at the press point.
    function press(canvasPoint, pressAdditive) {
        waiting = true;
        additive = pressAdditive;
        anchor = canvasPoint;
        cursor = canvasPoint;
    }

    // Follows the cursor.
    function drag(canvasPoint) {
        const dragged = Math.hypot(canvasPoint.x - anchor.x, canvasPoint.y - anchor.y) * viewZoom;
        if (waiting && dragged > Qt.styleHints.startDragDistance) {
            waiting = false;
            sweeping = true;
        }
        if (sweeping)
            cursor = canvasPoint;
    }

    // Selects the boxed nodes and ends the sweep.
    function release() {
        waiting = false;
        if (!sweeping)
            return;
        network.selectNodesInRect(Qt.rect(x, y, width, height), additive);
        sweeping = false;
        applied = true;
    }

    visible: sweeping

    // The anchor and the cursor put in order, so the box has a positive size
    // whichever way the drag runs.
    x: Math.min(anchor.x, cursor.x)
    y: Math.min(anchor.y, cursor.y)
    width: Math.abs(cursor.x - anchor.x)
    height: Math.abs(cursor.y - anchor.y)

    radius: Theme.network.selectionRadius / viewZoom
    color: Theme.network.selectionColor
    border.color: Theme.network.selectionBorderColor
    border.width: root.weight
    antialiasing: true

    // One bracket, drawn in the corner it belongs to.
    component Bracket: Item {
        id: bracket

        // How long each arm of the bracket reaches, in pixels.
        property real arm: 10 / root.viewZoom

        // The thickness of the arms.
        property real weight: 1 / root.viewZoom

        // The corner the bracket sits in.
        property int corner: 0

        // Which arms reach away from the corner.
        readonly property bool reachesRight: corner === 0 || corner === 3
        readonly property bool reachesDown: corner === 0 || corner === 1

        Rectangle {
            height: bracket.weight
            width: bracket.arm
            color: Theme.network.selectionBorderColor
            anchors.top: parent.top
            anchors.left: parent.left
            visible: bracket.reachesRight
        }

        Rectangle {
            height: bracket.arm
            width: bracket.weight
            color: Theme.network.selectionBorderColor
            anchors.top: parent.top
            anchors.left: parent.left
            visible: bracket.reachesDown
        }

        Rectangle {
            height: bracket.weight
            width: bracket.arm
            color: Theme.network.selectionBorderColor
            anchors.top: parent.top
            anchors.right: parent.right
            visible: !bracket.reachesRight
        }

        Rectangle {
            height: bracket.arm
            width: bracket.weight
            color: Theme.network.selectionBorderColor
            anchors.top: parent.top
            anchors.right: parent.right
            visible: !bracket.reachesDown
        }
    }

    // The brackets, one in each corner of the box.
    Bracket {
        corner: 0
        anchors.top: parent.top
        anchors.left: parent.left
    }

    Bracket {
        corner: 1
        anchors.top: parent.top
        anchors.right: parent.right
    }

    Bracket {
        corner: 2
        anchors.bottom: parent.bottom
        anchors.left: parent.left
    }

    Bracket {
        corner: 3
        anchors.bottom: parent.bottom
        anchors.right: parent.right
    }
}
