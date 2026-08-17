import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        Label {
            text: "串口连接"
            font.pixelSize: 24
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
            Layout.topMargin: 20
        }

        // Connection card
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "端口配置"
                    font.pixelSize: 18
                    font.bold: true
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 12
                    Layout.fillWidth: true

                    Label { text: "串口:"; color: "#757575" }
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        ComboBox {
                            id: portCombo
                            model: connectionController.portList
                            Layout.preferredWidth: 250
                            enabled: !deviceComm.connected
                            onActivated: connectionController.selectedPort = currentText
                        }
                        Button {
                            text: "刷新"
                            flat: true
                            enabled: !deviceComm.connected
                            onClicked: connectionController.refreshPorts()
                        }
                    }

                    Label { text: "波特率:"; color: "#757575" }
                    ComboBox {
                        id: baudCombo
                        model: ["4800", "9600", "19200", "38400", "57600", "115200", "230400"]
                        currentIndex: 1  // Default 9600
                        Layout.preferredWidth: 250
                        enabled: !deviceComm.connected
                        onActivated: connectionController.baudRate = parseInt(currentText)
                        }

                    Item { Layout.columnSpan: 2; Layout.preferredHeight: 4 }

                    Item {}
                    Button {
                        text: deviceComm.connected ? "断开连接" : "连接"
                        Material.background: deviceComm.connected ? "#F44336" : "#4CAF50"
                        Material.foreground: "white"
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 44

                        onClicked: {
                            if (deviceComm.connected) {
                                pollingService.stop()
                                connectionController.disconnectDevice()
                            } else {
                                if (portCombo.currentText !== "")
                                    connectionController.selectedPort = portCombo.currentText
                                connectionController.baudRate = parseInt(baudCombo.currentText)
                                connectionController.connectDevice()
                            }
                        }
                    }
                }
            }
        }

        // Device info card
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 16
            Material.elevation: 2
            visible: deviceComm.connected

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: "设备信息"
                    font.pixelSize: 18
                    font.bold: true
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8
                    Layout.fillWidth: true

                    Label { text: "设备类型:"; color: "#757575" }
                    Label {
                        text: connectionController.deviceInfo
                              ? "0x" + Number(connectionController.deviceInfo.deviceType).toString(16).toUpperCase()
                              : "--"
                        font.bold: true
                    }

                    Label { text: "硬件版本:"; color: "#757575" }
                    Label {
                        text: connectionController.deviceInfo
                              ? connectionController.deviceInfo.hwVersionString
                              : "--"
                        font.bold: true
                    }

                    Label { text: "软件版本:"; color: "#757575" }
                    Label {
                        text: connectionController.deviceInfo
                              ? connectionController.deviceInfo.swVersionString
                              : "--"
                        font.bold: true
                    }

                    Label { text: "序列号:"; color: "#757575" }
                    Label {
                        text: connectionController.deviceInfo
                              ? Number(connectionController.deviceInfo.serialNumber).toString()
                              : "--"
                        font.bold: true
                    }

                    Label { text: "运行时间:"; color: "#757575" }
                    Label {
                        text: connectionController.deviceInfo
                              ? connectionController.deviceInfo.uptimeSeconds + " s"
                              : "--"
                        font.bold: true
                    }
                }
            }
        }

        // Connection status
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 16
            Material.elevation: 2

            RowLayout {
                spacing: 12
                anchors.fill: parent

                Rectangle {
                    width: 16; height: 16; radius: 8
                    color: deviceComm.connected ? "#4CAF50" : "#9E9E9E"
                }

                Label {
                    text: deviceComm.connected
                          ? "已连接: " + connectionController.selectedPort + " @ " + connectionController.baudRate + " bps"
                          : "未连接。请选择串口并点击连接。"
                    color: deviceComm.connected ? "#4CAF50" : "#757575"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    Component.onCompleted: connectionController.refreshPorts()
}
