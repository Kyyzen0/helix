import QtQuick
import Enzo

// Horizontal top bar menu.
//
// Each menu is { title, entries } where entries is a function returning the row list.
Item {
    id: bar

    implicitHeight: 38

    property var menus: []

    // The name shown at the leading edge of the bar, empty for none.
    property string brandText: ""

    // Title whose menu is open, or -1 when the bar is closed.
    property int openIndex: -1

    function openMenu(index) {
        if (index === openIndex)
            return;
        // A title with nothing to show leaves the bar closed.
        const entries = bar.menus[index].entries;
        if (entries && entries().length === 0)
            return;
        openIndex = index;
    }

    function closeMenu() {
        openIndex = -1;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.menu.barColor
    }

    // The line that separates the bar from the panes below it.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.menu.barBorderColor
    }

    Row {
        id: brand

        visible: bar.brandText !== ""
        anchors.left: parent.left
        anchors.leftMargin: Theme.var.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.var.space8

        // The mark that names the application.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            radius: 2.5
            color: Theme.var.accent
            rotation: 45
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.brandText
            color: Theme.menu.brandColor
            font.family: Theme.var.fontSans
            font.pixelSize: Theme.var.textCaption
            font.weight: Font.DemiBold
            font.letterSpacing: 1.6
            font.capitalization: Font.AllUppercase
        }
    }

    Row {
        id: titles

        anchors.left: brand.right
        anchors.leftMargin: brand.visible ? Theme.var.space16 : 0
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: bar.menus

            delegate: Rectangle {
                id: title

                required property int index
                required property var modelData

                readonly property bool open: bar.openIndex === index

                width: label.implicitWidth + Theme.var.space16
                height: 24
                radius: Theme.var.controlRadius
                color: open || hover.hovered ? Theme.menu.itemHoverFill : "transparent"

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: title.modelData.title
                    color: title.open ? Theme.menu.itemHoverColor : Theme.menu.itemColor
                    font.family: Theme.var.fontSans
                    font.pixelSize: Theme.var.textBody
                }

                HoverHandler {
                    id: hover
                    onHoveredChanged: if (hovered && bar.openIndex >= 0 && !title.open)
                        bar.openMenu(index)
                }
                TapHandler {
                    onTapped: title.open ? bar.closeMenu() : bar.openMenu(index)
                }

                onOpenChanged: open ? popup.open() : popup.close()

                MenuPopup {
                    id: popup
                    y: title.height + 4
                    entries: title.modelData.entries ? title.modelData.entries() : []
                    onClosed: if (bar.openIndex === title.index)
                        bar.closeMenu()
                }
            }
        }
    }
}
