import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "." as Comp

Pane {
    id: card

    property var controller: null
    property string title: "水冷箱"

    padding: 12
    Material.elevation: 2

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: card.title
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#E0E0E0" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.preferredWidth: 200
                Layout.alignment: Qt.AlignTop
                spacing: 6

                Label { text: "状态"; font.pixelSize: 13; font.bold: true; color: "#616161" }

                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: card.controller && card.controller.model.online ? "#4CAF50" : "#9E9E9E"
                    }
                    Label {
                        text: card.controller && card.controller.model.online ? "在线" : "离线"
                        font.pixelSize: 12
                        color: card.controller && card.controller.model.online ? "#4CAF50" : "#9E9E9E"
                    }
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Label { text: "当前温度"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: card.controller ? card.controller.model.temperatureC.toFixed(1) + " °C" : "--"
                        font.pixelSize: 12
                    }
                }

                Button {
                    text: "手动刷新状态"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected && card.controller
                    onClicked: card.controller.readStatus()
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: "#E0E0E0" }

            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 6

                Label { text: "控制"; font.pixelSize: 13; font.bold: true; color: "#616161" }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "目标温度"
                    unit: "°C"
                    editorType: "doublefield"
                    doubleValue: card.controller ? card.controller.model.tempSetC : 0
                    doubleFrom: -100
                    doubleTo: 100
                    decimals: 1
                    enabled: deviceComm.connected && card.controller
                    onValueCommitted: function(value) {
                        card.controller.writeTempSet(value)
                    }
                }

                Button {
                    text: "手动刷新控制回读"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected && card.controller
                    onClicked: card.controller.readControl()
                }
            }
        }
    }
}
