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
            text: "系统总览"
            font.pixelSize: 24
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
            Layout.topMargin: 20
        }

        // Row 1: System + Slave
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 16

            // System Status
            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 16
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label { text: "系统状态"; font.pixelSize: 16; font.bold: true }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4
                        Layout.fillWidth: true

                        Label { text: "初始化:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.initOk ? "完成" : "未完成"; color: dashboardController.systemStatus.initOk ? "#4CAF50" : "#9E9E9E" }

                        Label { text: "DFB就绪:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.dfbReady ? "是" : "否"; color: dashboardController.systemStatus.dfbReady ? "#4CAF50" : "#9E9E9E" }

                        Label { text: "泵就绪:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.pumpReady ? "是" : "否"; color: dashboardController.systemStatus.pumpReady ? "#4CAF50" : "#9E9E9E" }

                        Label { text: "从板就绪:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.slaveReady ? "是" : "否"; color: dashboardController.systemStatus.slaveReady ? "#4CAF50" : "#9E9E9E" }

                        Label { text: "同步:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.syncOk ? "正常" : "未同步"; color: dashboardController.systemStatus.syncOk ? "#4CAF50" : "#FF9800" }

                        Label { text: "故障码:"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.systemStatus.sysFault).toString(16).toUpperCase()
                            color: dashboardController.systemStatus.hasFault ? "#F44336" : "#4CAF50"
                        }

                        Label { text: "同步步骤:"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.mainSyncStep }

                        Label { text: "同步状态:"; color: "#757575" }
                        Label {
                            text: dashboardController.systemStatus.mainSyncStatus === 0 ? "正常" : "失败"
                            color: dashboardController.systemStatus.mainSyncStatus === 0 ? "#4CAF50" : "#F44336"
                        }
                    }
                }
            }

            // Slave Status
            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 16
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "从板状态"; font.pixelSize: 16; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.slaveStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                        Label {
                            text: dashboardController.slaveStatus.online ? "在线" : "离线"
                            color: dashboardController.slaveStatus.online ? "#4CAF50" : "#9E9E9E"
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4
                        Layout.fillWidth: true

                        Label { text: "故障码:"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.slaveStatus.sysFault).toString(16).toUpperCase()
                            color: dashboardController.slaveStatus.sysFault > 0 ? "#F44336" : "#4CAF50"
                        }

                        Label { text: "同步步骤:"; color: "#757575" }
                        Label { text: dashboardController.slaveStatus.syncStep }

                        Label { text: "同步状态:"; color: "#757575" }
                        Label {
                            text: dashboardController.slaveStatus.syncStatusCode === 0 ? "正常" : "丢失"
                            color: dashboardController.slaveStatus.syncStatusCode === 0 ? "#4CAF50" : "#F44336"
                        }

                        Label { text: "同步错误:"; color: "#757575" }
                        Label {
                            text: dashboardController.slaveStatus.syncErrorCount
                            color: dashboardController.slaveStatus.syncErrorCount > 0 ? "#FF9800" : "#212121"
                        }

                        Label { text: "运行时间:"; color: "#757575" }
                        Label { text: dashboardController.slaveStatus.uptimeSeconds + " s" }
                    }
                }
            }
        }

        // Row 2: DFB + MultiMode Pump
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 16

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 16
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "DFB 激光器"; font.pixelSize: 16; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.dfbStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4

                        Label { text: "温度:"; color: "#757575" }
                        Label { text: dashboardController.dfbStatus.temperature.toFixed(2) + " °C" }

                        Label { text: "电流:"; color: "#757575" }
                        Label { text: dashboardController.dfbStatus.current.toFixed(1) + " mA" }

                        Label { text: "TEC:"; color: "#757575" }
                        Label {
                            text: dashboardController.dfbStatus.tecStatus ? "稳定" : "不稳定"
                            color: dashboardController.dfbStatus.tecStatus ? "#4CAF50" : "#FF9800"
                        }

                        Label { text: "PD电压:"; color: "#757575" }
                        Label { text: dashboardController.dfbStatus.pdVoltage.toFixed(1) + " mV" }
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 16
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "多模泵"; font.pixelSize: 16; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.mpumpStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4

                        Label { text: "实际电流:"; color: "#757575" }
                        Label { text: dashboardController.mpumpStatus.currentActual + " mA" }

                        Label { text: "故障码:"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.mpumpStatus.fault).toString(16).toUpperCase()
                            color: dashboardController.mpumpStatus.fault > 0 ? "#F44336" : "#4CAF50"
                        }
                    }
                }
            }
        }

        // Row 3: Pump1 + Pump2
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 16

            Repeater {
                model: [
                    { title: "泵浦1", status: dashboardController.pump1Status },
                    { title: "泵浦2", status: dashboardController.pump2Status }
                ]

                delegate: Pane {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    padding: 16
                    Material.elevation: 2

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6

                        RowLayout {
                            Label { text: modelData.title; font.pixelSize: 16; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: modelData.status.online ? "#4CAF50" : "#9E9E9E"
                            }
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 4

                            Label { text: "电流:"; color: "#757575" }
                            Label { text: modelData.status.currentActual + " A" }

                            Label { text: "电压:"; color: "#757575" }
                            Label { text: modelData.status.voltageActual + " V" }

                            Label { text: "频率:"; color: "#757575" }
                            Label { text: modelData.status.freqActual + " Hz" }
                        }
                    }
                }
            }
        }

        // Temperature
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 16
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: "温度 (8通道)"; font.pixelSize: 16; font.bold: true }

                GridLayout {
                    columns: 4
                    columnSpacing: 24
                    rowSpacing: 6
                    Layout.fillWidth: true

                    Repeater {
                        model: 8
                        delegate: RowLayout {
                            spacing: 8
                            property double tempVal: dashboardController.temperature.temperatures[index] ?? 0.0

                            Label {
                                text: "CH" + index + ":"
                                color: "#757575"
                                Layout.preferredWidth: 40
                            }
                            Label {
                                text: tempVal.toFixed(2) + " °C"
                                font.bold: true
                                color: tempVal > 80 ? "#F44336" : tempVal > 50 ? "#FF9800" : "#4CAF50"
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
