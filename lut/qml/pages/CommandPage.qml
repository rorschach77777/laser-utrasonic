import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        Label {
            text: "命令控制"
            font.pixelSize: 24
            font.bold: true
            Layout.leftMargin: 16
            Layout.topMargin: 16
        }

        // Run Control
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "运行控制"
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 16

                    Button {
                        text: "启动运行"
                        enabled: deviceComm.connected
                        icon.name: "media-playback-start"
                        Material.background: "#4CAF50"
                        Material.foreground: "white"
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.run()
                    }

                    Button {
                        text: "停止运行"
                        enabled: deviceComm.connected
                        icon.name: "media-playback-stop"
                        Material.background: "#FF9800"
                        Material.foreground: "white"
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.stop()
                    }

                    Button {
                        text: "单次触发"
                        enabled: deviceComm.connected
                        Material.background: "#2196F3"
                        Material.foreground: "white"
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.singleTrigger()
                    }
                }
            }
        }

        // Parameter Commands
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "参数操作"
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 16

                    Button {
                        text: "保存参数"
                        enabled: deviceComm.connected
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.saveParams()
                    }

                    Button {
                        text: "同步到从板"
                        enabled: deviceComm.connected
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.syncToSlave()
                    }

                    Button {
                        text: "清除故障"
                        enabled: deviceComm.connected
                        Material.background: "#FF9800"
                        Material.foreground: "white"
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 48
                        onClicked: commandController.clearFault()
                    }
                }
            }
        }

        // Dangerous Commands
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "危险操作"
                    font.pixelSize: 18
                    font.bold: true
                    color: "#F44336"
                }

                Label {
                    text: "以下操作不可逆，请谨慎使用"
                    color: "#757575"
                    font.pixelSize: 12
                }

                RowLayout {
                    spacing: 16

                    Button {
                        text: "恢复出厂设置"
                        enabled: deviceComm.connected
                        Material.background: "#F44336"
                        Material.foreground: "white"
                        Layout.preferredWidth: 180
                        Layout.preferredHeight: 48
                        onClicked: factoryDialog.open()
                    }

                    Button {
                        text: "设备重启"
                        enabled: deviceComm.connected
                        Material.background: "#F44336"
                        Material.foreground: "white"
                        Layout.preferredWidth: 180
                        Layout.preferredHeight: 48
                        onClicked: rebootDialog.open()
                    }
                }
            }
        }

        // Status Log
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: 200
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: "命令日志"
                    font.pixelSize: 18
                    font.bold: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextArea {
                        id: logArea
                        readOnly: true
                        font.family: "Consolas"
                        font.pixelSize: 12
                        wrapMode: TextArea.Wrap
                        placeholderText: "命令执行日志..."
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }

    Dialog {
        id: factoryDialog
        title: "确认恢复出厂设置"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true

        Label {
            text: "确定要恢复出厂设置吗？\n此操作将清除所有用户参数！"
            color: "#F44336"
        }

        onAccepted: {
            commandController.factoryReset()
            logArea.append("[" + Qt.formatTime(new Date(), "hh:mm:ss") + "] 恢复出厂设置")
        }
    }

    Dialog {
        id: rebootDialog
        title: "确认重启设备"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true

        Label {
            text: "确定要重启设备吗？\n设备将断开连接并重新启动。"
        }

        onAccepted: {
            commandController.reboot()
            logArea.append("[" + Qt.formatTime(new Date(), "hh:mm:ss") + "] 设备重启")
        }
    }

    Connections {
        target: commandController
        function onCommandSent(cmdName) {
            logArea.append("[" + Qt.formatTime(new Date(), "hh:mm:ss") + "] 命令发送: " + cmdName)
        }
        function onCommandFailed(cmdName) {
            logArea.append("[" + Qt.formatTime(new Date(), "hh:mm:ss") + "] 命令失败: " + cmdName)
        }
    }
}
