import QtQuick
import QtQuick.Controls
import QtQuick.Templates
import Enzo

// The strip between two panes.
//
// It shows a hairline down its middle and a grip in the centre, and washes with
// the accent while the cursor is on it, so the layout reads as something that
// can be dragged rather than as a gap.
Item {
    id: root

    // The split view the strip belongs to. The view parents its handles
    // through its content item, so the chain is walked to find it.
    readonly property SplitView view: {
        let item = parent;
        while (item) {
            if (item.inherits && item.inherits("QQuickSplitView"))
                return item;
            item = item.parent;
        }
        return null;
    }

    // Whether the panes sit side by side, and the strip runs down the screen.
    readonly property bool horizontal: root.view !== null && root.view.orientation === Qt.Horizontal

    // The thickness of the strip, in pixels.
    property int thickness: Theme.splitter.thickness

    // The strip is as wide as the gutter it makes, and as long as the panes.
    implicitWidth: root.horizontal ? root.thickness : (parent ? parent.width : 0)
    implicitHeight: root.horizontal ? (parent ? parent.height : 0) : root.thickness

    // Whether the cursor is on the strip.
    readonly property bool hovered: SplitHandle.hovered || hover.hovered

    // Whether the strip is being dragged.
    readonly property bool pressed: SplitHandle.pressed

    // The wash that shows the strip can be dragged.
    Rectangle {
        anchors.fill: parent
        color: root.pressed ? Theme.splitter.activeColor : Theme.splitter.hoverColor
        opacity: root.pressed ? 0.28 : (root.hovered ? 0.16 : 0)
        Behavior on opacity {
            NumberAnimation {
                duration: 110
            }
        }
    }

    // The hairline running along the strip.
    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? 1 : parent.width
        height: root.horizontal ? parent.height : 1
        color: root.hovered || root.pressed ? Theme.splitter.activeColor : Theme.var.border
        opacity: root.hovered || root.pressed ? 0.9 : 0.55
        Behavior on color {
            ColorAnimation {
                duration: 110
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: 110
            }
        }
    }

    // The grip in the centre of the strip.
    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? 3 : 22
        height: root.horizontal ? 22 : 3
        radius: 1.5
        color: root.hovered || root.pressed ? Theme.splitter.activeColor : Theme.var.line
        opacity: root.hovered || root.pressed ? 1 : 0.7
        Behavior on color {
            ColorAnimation {
                duration: 110
            }
        }
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.SplitHCursor
    }
}
