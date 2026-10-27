import QtQuick
import Helix

// A small chip naming the kind of a parameter.
//
// The chip carries a letter and the hue of the kind together, so a row of
// parameters says what each of them holds before its label is read.
Rectangle {
    id: root

    // The kind of the parameter, one of the kinds the parameter list knows.
    property string kind: ""

    // The letter drawn in the chip.
    readonly property string code: codeFor(root.kind)

    // The hue of the kind.
    readonly property color kindColor: colorFor(root.kind)

    // The letter shown for a kind.
    function codeFor(kind) {
        switch (kind) {
        case "float":
            return "f";
        case "int":
            return "i";
        case "bool":
        case "toggle":
            return "b";
        case "string":
            return "s";
        case "attribute":
            return "a";
        case "code":
            return "c";
        case "dropdown":
            return "d";
        case "vector":
            return "v";
        case "ramp":
            return "r";
        }
        return "·";
    }

    // The hue shown for a kind.
    function colorFor(kind) {
        switch (kind) {
        case "float":
            return Theme.parameterType.floatColor;
        case "int":
            return Theme.parameterType.intColor;
        case "bool":
        case "toggle":
            return Theme.parameterType.boolColor;
        case "string":
            return Theme.parameterType.stringColor;
        case "attribute":
            return Theme.parameterType.attributeColor;
        case "code":
            return Theme.parameterType.codeColor;
        case "dropdown":
            return Theme.parameterType.dropdownColor;
        case "vector":
            return Theme.parameterType.vectorColor;
        case "ramp":
            return Theme.parameterType.rampColor;
        }
        return Theme.var.textMuted;
    }

    implicitWidth: 15
    implicitHeight: 15
    radius: 4
    color: Qt.rgba(root.kindColor.r, root.kindColor.g, root.kindColor.b, 0.16)
    border.color: Qt.rgba(root.kindColor.r, root.kindColor.g, root.kindColor.b, 0.45)
    border.width: 1

    Text {
        anchors.centerIn: parent
        text: root.code
        color: root.kindColor
        font.family: Theme.var.fontMono
        font.pixelSize: Theme.var.textMicro
        font.weight: Font.DemiBold
        renderType: Text.NativeRendering
    }
}
