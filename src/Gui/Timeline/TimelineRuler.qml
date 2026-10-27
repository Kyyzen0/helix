import QtQuick
import Helix

// Frame scale carrying the playhead on the current frame.
Item {
    id: root

    property int startFrame: 1
    property int endFrame: 240
    property real frame: 1

    // The keyframes of the parameter being animated, one entry per key.
    property var keyframes: []

    readonly property real baselineY: height - 3

    // Frames between the two ends, counting the end frame itself, so every
    // frame of the range has a slot of its own on the scale.
    readonly property int frameSpan: Math.max(1, root.endFrame - root.startFrame + 1)

    signal frameRequested(int frame)

    // Height of the scale, plus the room the numbers take above it.
    implicitHeight: 24

    // Most ticks the scale will draw at once.
    readonly property int maxTickCount: 300

    // Frames from one tick to the next, the smallest round step that keeps the
    // scale under maxTickCount ticks.
    readonly property int tickStep: {
        const roundSteps = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000];
        for (const step of roundSteps) {
            if (root.frameSpan / step <= root.maxTickCount)
                return step;
        }
        return Math.ceil(root.frameSpan / root.maxTickCount);
    }

    // Every fifth tick is drawn taller, and every twentieth taller again and
    // numbered.
    readonly property int minorStep: root.tickStep * 5
    readonly property int majorStep: root.tickStep * 20

    // The frames that take a tick, so the two ends and every step between them.
    readonly property var tickFrames: {
        const frames = [root.startFrame];
        const firstStep = Math.ceil((root.startFrame + 1) / root.tickStep) * root.tickStep;
        for (let frame = firstStep; frame < root.endFrame; frame += root.tickStep)
            frames.push(frame);
        if (root.endFrame > root.startFrame)
            frames.push(root.endFrame);
        return frames;
    }

    // Returns whether a frame takes a tall numbered tick, so every twentieth
    // tick and the two ends.
    function isMajorFrame(value) {
        return value % root.majorStep === 0 || value === root.startFrame || value === root.endFrame;
    }

    function getFrameX(value) {
        return (value - root.startFrame) / root.frameSpan * root.width;
    }

    function getFrameAtX(x) {
        // The frame under the pointer, kept inside the range so a click past
        // the last tick lands on the last frame rather than past it.
        const frame = Math.round(root.startFrame + x / root.width * root.frameSpan);
        return Math.max(root.startFrame, Math.min(root.endFrame, frame));
    }

    Rectangle {
        y: root.baselineY
        width: parent.width
        height: 1
        color: Theme.timeline.baselineColor
    }

    Repeater {
        model: root.tickFrames

        delegate: Item {
            id: tick

            required property int modelData
            readonly property int frameNumber: tick.modelData
            readonly property bool isMajor: root.isMajorFrame(frameNumber)
            readonly property bool isMinor: frameNumber % root.minorStep === 0
            readonly property real tickHeight: {
                if (tick.isMajor)
                    return 8;
                if (tick.isMinor)
                    return 4;
                return 2;
            }
            readonly property color tickColor: {
                if (tick.isMajor)
                    return Theme.timeline.majorTickColor;
                if (tick.isMinor)
                    return Theme.timeline.minorTickColor;
                return Theme.timeline.frameTickColor;
            }

            x: root.getFrameX(frameNumber)

            Rectangle {
                y: root.baselineY - height
                width: 1
                height: tick.tickHeight
                color: tick.tickColor
            }

            Text {
                visible: tick.isMajor
                // Centres the number on its tick, and keeps the first and
                // last inside the scale.
                x: Math.max(-tick.x, Math.min(root.width - tick.x - width, -width / 2))
                text: tick.frameNumber
                color: Theme.var.textMuted
                font.family: Theme.var.fontMono
                font.pixelSize: 9
            }
        }
    }

    // A marker on every keyframe of the animated parameter.
    Repeater {
        model: root.keyframes

        delegate: Rectangle {
            id: marker

            required property var modelData

            readonly property real keyFrame: modelData.frame

            x: root.getFrameX(keyFrame)
            y: root.baselineY - 3

            width: 6
            height: 6
            radius: 1
            // A diamond, so a key reads as a key rather than as a tick.
            rotation: 45
            color: Theme.timeline.playheadColor
            border.color: Theme.var.fieldSurface
            border.width: 0.5
        }
    }

    Item {
        x: root.getFrameX(root.frame)

        Rectangle {
            x: -width / 2
            y: 10
            width: 2
            height: root.baselineY - y
            color: Theme.timeline.playheadColor
        }

        Rectangle {
            x: -width / 2
            y: -1
            width: number.width + 8
            height: 12
            radius: 3
            color: Theme.timeline.playheadColor

            Text {
                id: number

                anchors.centerIn: parent
                text: root.frame
                color: Theme.var.textStrong
                font.family: Theme.var.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }
    }

    // Sets the frame from the pointer on click and on drag.
    MouseArea {
        id: scrubber

        anchors.fill: parent
        onPressed: mouse => root.frameRequested(root.getFrameAtX(mouse.x))
        // Only a held button moves the playhead, so passing the pointer over
        // the scale does not scrub it.
        onPositionChanged: mouse => {
            if (scrubber.pressed)
                root.frameRequested(root.getFrameAtX(mouse.x));
        }
    }
}
