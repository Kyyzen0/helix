import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Helix
import "Spreadsheet"
import "Network"
import "Viewport"
import "Parameters"
import "Components"
import "MenuBar"
import "Timeline"

// Root window of the QML application.
//
// The shell is three bands: the menu bar at the top, the workspace in the
// middle, and the transport and the status strip along the bottom. The
// workspace is split into the node graph, the viewport and the spreadsheet
// down the middle, with the parameters of the primary node along the side.
ApplicationWindow {
    id: window

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    visibility: Window.Maximized
    title: scene.hasCurrentFile ? scene.currentFileName + " — Helix" : "Helix"
    color: Theme.var.background

    Shortcut {
        sequence: StandardKey.Undo
        onActivated: network.undo()
    }
    Shortcut {
        sequence: StandardKey.Redo
        onActivated: network.redo()
    }

    menuBar: AppMenuBar {}

    EditDismisser {
        anchors.fill: parent
        z: 1000
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // The workspace: the node graph, the viewport over the spreadsheet, and
        // the parameters of the node being worked on.
        SplitView {
            id: workspace

            orientation: Qt.Horizontal
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.var.space8
            Layout.rightMargin: Theme.var.space8
            Layout.topMargin: Theme.var.space8

            handle: SplitterHandle {}

            // The node graph.
            PanelSurface {
                id: networkPane

                SplitView.minimumWidth: 340
                SplitView.preferredWidth: 560

                showHeader: true
                title: "Network"

                Network {
                    anchors.fill: parent
                }
            }

            // The viewport over the spreadsheet.
            SplitView {
                id: centreColumn

                orientation: Qt.Vertical
                SplitView.fillWidth: true
                SplitView.minimumWidth: 320

                handle: SplitterHandle {}

                // The viewport.
                PanelSurface {
                    id: viewportPane

                    SplitView.minimumHeight: 200
                    SplitView.fillHeight: true

                    showHeader: true
                    title: "Viewport"

                    Viewport {
                        anchors.fill: parent
                    }
                }

                // The geometry spreadsheet.
                PanelSurface {
                    id: spreadsheetPane

                    SplitView.minimumHeight: 140
                    SplitView.preferredHeight: 240

                    contentInset: 0

                    Spreadsheet {
                        anchors.fill: parent
                        viewModel: spreadsheet
                    }
                }
            }

            // The parameters of the primary node.
            PanelSurface {
                id: parametersPane

                SplitView.minimumWidth: 280
                SplitView.preferredWidth: 340

                contentInset: 0

                ParametersPanel {
                    docked: true
                }
            }
        }

        // The transport along the bottom of the workspace.
        PanelSurface {
            id: timelinePane

            Layout.fillWidth: true
            Layout.leftMargin: Theme.var.space8
            Layout.rightMargin: Theme.var.space8
            Layout.bottomMargin: Theme.var.space8
            Layout.preferredHeight: timelineBar.implicitHeight + Theme.var.space12

            contentInset: 0

            Timeline {
                id: timelineBar

                anchors.fill: parent
            }
        }

        // What the application is doing right now.
        StatusBar {
            id: statusBar

            Layout.fillWidth: true
            Layout.preferredHeight: 26

            cookState: network.cookState
            frame: timeline.frame
            startFrame: timeline.startFrame
            endFrame: timeline.endFrame
            fps: timeline.fps
            nodeCount: network.nodeCount
            selectedCount: network.selectedCount
            sceneName: scene.hasCurrentFile ? scene.currentFileName : ""
        }
    }
}
