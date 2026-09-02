import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

ScrollView {
    id: root
    contentWidth: availableWidth
    contentHeight: dashboardContent.implicitHeight
    clip: true

    property var mainWaterCooler: waterCoolerController ? waterCoolerController.model : null
    property var slaveWaterCooler: slaveWaterCoolerController ? slaveWaterCoolerController.model : null

    ColumnLayout {
        id: dashboardContent
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 16

            Label {
                text: "系统总览"
                font.pixelSize: 24
                font.bold: true
                color: "#3F51B5"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: pollingService.active ? "#4CAF50" : "#9E9E9E"
            }
            Label {
                text: pollingService.active ? "状态轮询中" : "轮询停止"
                font.pixelSize: 12
                color: pollingService.active ? "#4CAF50" : "#757575"
            }

            Rectangle {
                width: 1
                height: 18
                color: "#E0E0E0"
                Layout.leftMargin: 8
                Layout.rightMargin: 8
            }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: healthMonitor.overallLevel === 3 ? "#F44336"
                      : healthMonitor.overallLevel === 2 ? "#FF9800"
                      : healthMonitor.overallLevel === 1 ? "#4CAF50"
                      : "#9E9E9E"
            }
            Label {
                text: "系统健康: " + healthMonitor.overallStatus
                font.pixelSize: 12
                font.bold: true
                color: healthMonitor.overallLevel === 3 ? "#F44336"
                     : healthMonitor.overallLevel === 2 ? "#FF9800"
                     : healthMonitor.overallLevel === 1 ? "#4CAF50"
                     : "#757575"
            }
            Label {
                visible: healthMonitor.warningCount + healthMonitor.alarmCount > 0
                text: "报警 " + healthMonitor.alarmCount + " / 警告 " + healthMonitor.warningCount
                font.pixelSize: 11
                color: healthMonitor.alarmCount > 0 ? "#F44336" : "#FF9800"
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 10
            Material.elevation: 2
            visible: healthMonitor.monitoring

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: healthMonitor.overallLevel === 1 ? "✓" : healthMonitor.overallLevel === 2 ? "⚠" : "!"
                    font.pixelSize: 24
                    font.bold: true
                    color: healthMonitor.overallLevel === 3 ? "#F44336"
                         : healthMonitor.overallLevel === 2 ? "#FF9800"
                         : "#4CAF50"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: healthMonitor.overallLevel === 1
                              ? "当前监控设备状态正常"
                              : "检测到设备状态或参数异常"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Label {
                        text: healthMonitor.thresholdNote
                        font.pixelSize: 10
                        color: "#757575"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Button {
                    text: "立即检查"
                    Layout.preferredWidth: 120
                    onClicked: healthMonitor.evaluateNow()
                }
            }
        }

        // System status summary
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 12

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "主板状态"; font.pixelSize: 15; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: deviceComm.connected ? "#4CAF50" : "#9E9E9E"
                        }
                        Label {
                            text: deviceComm.connected ? "在线" : "离线"
                            color: deviceComm.connected ? "#4CAF50" : "#757575"
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 4
                        Layout.fillWidth: true

                        Label { text: "初始化"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.initOk ? "完成" : "未完成" }
                        Label { text: "同步"; color: "#757575" }
                        Label {
                            text: dashboardController.systemStatus.syncOk ? "正常" : "未同步"
                            color: dashboardController.systemStatus.syncOk ? "#4CAF50" : "#FF9800"
                        }

                        Label { text: "DFB就绪"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.dfbReady ? "是" : "否" }
                        Label { text: "泵就绪"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.pumpReady ? "是" : "否" }

                        Label { text: "故障码"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.systemStatus.sysFault).toString(16).toUpperCase()
                            color: dashboardController.systemStatus.hasFault ? "#F44336" : "#4CAF50"
                        }
                        Label { text: "同步步骤"; color: "#757575" }
                        Label { text: dashboardController.systemStatus.mainSyncStep }
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "从板状态"; font.pixelSize: 15; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.slaveStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                        Label {
                            text: dashboardController.slaveStatus.online ? "在线" : "离线"
                            color: dashboardController.slaveStatus.online ? "#4CAF50" : "#757575"
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        rowSpacing: 4
                        Layout.fillWidth: true

                        Label { text: "同步状态"; color: "#757575" }
                        Label {
                            text: dashboardController.slaveStatus.syncStatusCode === 0 ? "正常" : "异常"
                            color: dashboardController.slaveStatus.syncStatusCode === 0 ? "#4CAF50" : "#F44336"
                        }
                        Label { text: "同步步骤"; color: "#757575" }
                        Label { text: dashboardController.slaveStatus.syncStep }

                        Label { text: "同步错误"; color: "#757575" }
                        Label {
                            text: dashboardController.slaveStatus.syncErrorCount
                            color: dashboardController.slaveStatus.syncErrorCount > 0 ? "#FF9800" : "#212121"
                        }
                        Label { text: "故障码"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.slaveStatus.sysFault).toString(16).toUpperCase()
                            color: dashboardController.slaveStatus.sysFault > 0 ? "#F44336" : "#4CAF50"
                        }
                    }
                }
            }
        }

        Label {
            text: "光源与泵浦监控"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 12

            // DFB
            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "DFB 激光器"; font.pixelSize: 15; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.dfbStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                        Label {
                            text: dashboardController.dfbStatus.online ? "在线" : "离线"
                            color: dashboardController.dfbStatus.online ? "#4CAF50" : "#757575"
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 5

                        Label { text: "实时温度"; color: "#757575" }
                        Label { text: Number(dashboardController.dfbStatus.temperature).toFixed(2) + " °C"; font.bold: true }

                        Label { text: "实时电流"; color: "#757575" }
                        Label { text: Number(dashboardController.dfbStatus.current).toFixed(1) + " mA"; font.bold: true }

                        Label { text: "PD电压"; color: "#757575" }
                        Label { text: Number(dashboardController.dfbStatus.pdVoltage).toFixed(1) + " mV" }

                        Label { text: "TEC状态"; color: "#757575" }
                        Label {
                            text: dashboardController.dfbStatus.tecStatus ? "正常" : "异常"
                            color: dashboardController.dfbStatus.tecStatus ? "#4CAF50" : "#FF9800"
                        }

                        Label { text: "故障码"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.dfbStatus.fault).toString(16).toUpperCase()
                            color: dashboardController.dfbStatus.fault > 0 ? "#F44336" : "#4CAF50"
                        }
                    }
                }
            }

            // Multi-mode pump
            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Label { text: "多模泵"; font.pixelSize: 15; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: dashboardController.mpumpStatus.online ? "#4CAF50" : "#9E9E9E"
                        }
                        Label {
                            text: dashboardController.mpumpStatus.online ? "在线" : "离线"
                            color: dashboardController.mpumpStatus.online ? "#4CAF50" : "#757575"
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 5

                        Label { text: "实际电流"; color: "#757575" }
                        Label { text: dashboardController.mpumpStatus.currentActual + " mA"; font.bold: true }

                        Label { text: "设定电流"; color: "#757575" }
                        Label { text: dashboardController.mpumpStatus.currentSet + " mA" }

                        Label { text: "驱动模式"; color: "#757575" }
                        Label { text: dashboardController.mpumpStatus.driveMode }

                        Label { text: "运行时间"; color: "#757575" }
                        Label { text: dashboardController.mpumpStatus.uptimeSeconds + " s" }

                        Label { text: "故障码"; color: "#757575" }
                        Label {
                            text: "0x" + Number(dashboardController.mpumpStatus.fault).toString(16).toUpperCase()
                            color: dashboardController.mpumpStatus.fault > 0 ? "#F44336" : "#4CAF50"
                        }
                    }
                }
            }
        }

        Label {
            text: "水冷箱监控"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 12

            Repeater {
                model: [
                    { title: "检测激光水冷箱", cooler: root.mainWaterCooler },
                    { title: "激发激光水冷箱", cooler: root.slaveWaterCooler }
                ]

                delegate: Pane {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    padding: 12
                    Material.elevation: 2

                    property var cooler: modelData.cooler

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.title; font.pixelSize: 15; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: cooler && cooler.online ? "#4CAF50" : "#9E9E9E"
                            }
                            Label {
                                text: cooler && cooler.online ? "在线" : "离线"
                                color: cooler && cooler.online ? "#4CAF50" : "#757575"
                                font.pixelSize: 12
                            }
                        }

                        RowLayout {
                            Label { text: "实时温度"; color: "#757575"; font.pixelSize: 13 }
                            Label {
                                text: cooler ? Number(cooler.temperatureC).toFixed(1) + " °C" : "--"
                                font.pixelSize: 20
                                font.bold: true
                                color: cooler && cooler.online ? "#3F51B5" : "#9E9E9E"
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: cooler ? "设定 " + Number(cooler.tempSetC).toFixed(1) + " °C" : ""
                                color: "#757575"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: "检测激光泵浦监控"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 12

            Repeater {
                model: [
                    { title: "主板泵浦 1", pump: dashboardController.pump1Status },
                    { title: "主板泵浦 2", pump: dashboardController.pump2Status }
                ]

                delegate: Pane {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    padding: 12
                    Material.elevation: 2

                    property var pump: modelData.pump

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.title; font.pixelSize: 15; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: pump && pump.online ? "#4CAF50" : "#9E9E9E"
                            }
                            Label {
                                text: pump && pump.online ? "在线" : "离线"
                                color: pump && pump.online ? "#4CAF50" : "#757575"
                                font.pixelSize: 12
                            }
                        }

                        GridLayout {
                            columns: 6
                            columnSpacing: 10
                            rowSpacing: 4
                            Layout.fillWidth: true

                            Label { text: "电流"; color: "#757575" }
                            Label { text: pump ? pump.currentActual + " A" : "--"; font.bold: true }
                            Label { text: "电压"; color: "#757575" }
                            Label { text: pump ? Number(pump.voltageActual).toFixed(2) + " V" : "--"; font.bold: true }
                            Label { text: "频率"; color: "#757575" }
                            Label { text: pump ? pump.freqActual + " Hz" : "--"; font.bold: true }
                        }
                    }
                }
            }
        }

        Label {
            text: "激发激光泵浦监控"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 12

            Repeater {
                model: [
                    { title: "从板泵浦 1", pump: dashboardController.slavePump1Status },
                    { title: "从板泵浦 2", pump: dashboardController.slavePump2Status }
                ]

                delegate: Pane {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    padding: 12
                    Material.elevation: 2

                    property var pump: modelData.pump

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.title; font.pixelSize: 15; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: pump && pump.online ? "#4CAF50" : "#9E9E9E"
                            }
                            Label {
                                text: pump && pump.online ? "在线" : "离线"
                                color: pump && pump.online ? "#4CAF50" : "#757575"
                                font.pixelSize: 12
                            }
                        }

                        GridLayout {
                            columns: 6
                            columnSpacing: 10
                            rowSpacing: 4
                            Layout.fillWidth: true

                            Label { text: "电流"; color: "#757575" }
                            Label { text: pump ? pump.currentActual + " A" : "--"; font.bold: true }
                            Label { text: "电压"; color: "#757575" }
                            Label { text: pump ? Number(pump.voltageActual).toFixed(2) + " V" : "--"; font.bold: true }
                            Label { text: "频率"; color: "#757575" }
                            Label { text: pump ? pump.freqActual + " Hz" : "--"; font.bold: true }
                        }
                    }
                }
            }
        }

        Label {
            text: "当前报警"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: alarmListColumn.implicitHeight + topPadding + bottomPadding
            padding: 10
            Material.elevation: 2

            ColumnLayout {
                id: alarmListColumn
                width: parent.width
                spacing: 6

                Label {
                    visible: healthMonitor.alarms.length === 0
                    text: healthMonitor.monitoring ? "当前无设备报警。" : "系统未进入健康监控状态。"
                    color: healthMonitor.monitoring ? "#4CAF50" : "#757575"
                    font.pixelSize: 12
                }

                Repeater {
                    model: healthMonitor.alarms

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: alarmContent.implicitHeight + 16
                        radius: 4
                        color: modelData.severity >= 2 ? "#FFEBEE" : "#FFF8E1"
                        border.color: modelData.severity >= 2 ? "#EF9A9A" : "#FFE082"

                        ColumnLayout {
                            id: alarmContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.severityText
                                    font.bold: true
                                    color: modelData.severity >= 2 ? "#D32F2F" : "#F57C00"
                                }
                                Label {
                                    text: modelData.device
                                    font.bold: true
                                    color: "#212121"
                                }
                                Label {
                                    text: "— " + modelData.message
                                    color: "#424242"
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: modelData.code
                                    font.pixelSize: 9
                                    color: "#9E9E9E"
                                }
                            }

                            Label {
                                text: "当前: " + modelData.value + "    正常: " + modelData.normal
                                font.pixelSize: 11
                                color: "#616161"
                            }
                            Label {
                                text: "建议: " + modelData.suggestion
                                font.pixelSize: 11
                                color: "#616161"
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: "温度（8通道）"
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 20
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            padding: 12
            Material.elevation: 2

            GridLayout {
                anchors.fill: parent
                columns: 4
                columnSpacing: 24
                rowSpacing: 8

                Repeater {
                    model: 8
                    delegate: RowLayout {
                        spacing: 8
                        property double tempVal: dashboardController.temperature
                                                 ? (dashboardController.temperature.temperatures[index] ?? 0.0)
                                                 : 0.0

                        Label {
                            text: "CH" + index + ":"
                            color: "#757575"
                            Layout.preferredWidth: 40
                        }
                        Label {
                            text: tempVal.toFixed(2) + " °C"
                            font.bold: true
                            color: tempVal > 80 ? "#F44336"
                                                : tempVal > 50 ? "#FF9800"
                                                               : "#4CAF50"
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
