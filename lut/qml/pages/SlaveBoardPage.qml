import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "../components" as Comp

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true

    function gateLevelText(value) {
        return value === 0 ? "低电平" : "高电平"
    }

    function gateStateText(value) {
        switch (value) {
        case 0: return "IDLE"
        case 1: return "START_DLY"
        case 2: return "RUNNING"
        case 3: return "STOP_DLY"
        default: return "未知(" + value + ")"
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        Label {
            text: "从板设备总览"
            font.pixelSize: 24
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 16
            Layout.topMargin: 12
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 12
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Label { text: "从板状态"; font.pixelSize: 16; font.bold: true; color: "#3F51B5" }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: dashboardController.slaveStatus.online ? "#4CAF50" : "#9E9E9E"
                    }
                    Label {
                        text: dashboardController.slaveStatus.online ? "在线" : "离线"
                        font.pixelSize: 12
                        color: dashboardController.slaveStatus.online ? "#4CAF50" : "#9E9E9E"
                    }
                }

                GridLayout {
                    columns: 6
                    columnSpacing: 16
                    rowSpacing: 4
                    Layout.fillWidth: true

                    Label { text: "系统状态"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: "0x" + Number(dashboardController.slaveStatus.sysStatus).toString(16).toUpperCase(); font.pixelSize: 12 }
                    Label { text: "故障码"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: "0x" + Number(dashboardController.slaveStatus.sysFault).toString(16).toUpperCase()
                        font.pixelSize: 12
                        color: dashboardController.slaveStatus.sysFault > 0 ? "#F44336" : "#4CAF50"
                    }
                    Label { text: "同步步骤"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dashboardController.slaveStatus.syncStep; font.pixelSize: 12 }
                    Label { text: "同步状态"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: dashboardController.slaveStatus.syncStatusCode === 0 ? "正常" : "异常"
                        font.pixelSize: 12
                        color: dashboardController.slaveStatus.syncStatusCode === 0 ? "#4CAF50" : "#F44336"
                    }
                    Label { text: "同步错误"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dashboardController.slaveStatus.syncErrorCount; font.pixelSize: 12 }
                    Label { text: "运行时间"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dashboardController.slaveStatus.uptimeSeconds + " s"; font.pixelSize: 12 }
                }
            }
        }

        SlaveGateCard {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        SlavePumpCard {
            controller: slavePump1Controller
            title: "从板泵浦 1"
            timingChannelIndex: 0
            timingChannelName: "振荡器"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        SlavePumpCard {
            controller: slavePump2Controller
            title: "从板泵浦 2"
            timingChannelIndex: 1
            timingChannelName: "放大器"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        TimingOnlyCard {
            title: "PUMP1_EN"
            timingChannelIndex: 2
            timingChannelName: "PUMP1_EN"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        TimingOnlyCard {
            title: "PUMP2_EN"
            timingChannelIndex: 3
            timingChannelName: "PUMP2_EN"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        Item { Layout.preferredHeight: 12 }
    }

    component DeviceCard: Pane {
        id: card
        property string title: ""
        property alias statusContent: statusCol.data
        property alias controlContent: controlCol.data
        property int timingChannelIndex: 0
        property string timingChannelName: ""
        property bool showTiming: true

        padding: 12
        Material.elevation: 2

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label { text: card.title; font.pixelSize: 16; font.bold: true; color: "#3F51B5" }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#E0E0E0" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    id: statusCol
                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Label { text: "状态"; font.pixelSize: 13; font.bold: true; color: "#616161" }
                }

                Rectangle { width: 1; Layout.fillHeight: true; color: "#E0E0E0" }

                ColumnLayout {
                    id: controlCol
                    Layout.preferredWidth: 1
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Label { text: "控制"; font.pixelSize: 13; font.bold: true; color: "#616161" }
                }

                Rectangle { width: 1; Layout.fillHeight: true; color: "#E0E0E0"; visible: card.showTiming }

                ColumnLayout {
                    visible: card.showTiming
                    Layout.preferredWidth: 1
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Label {
                        text: "时序"
                        font.pixelSize: 13
                        font.bold: true
                        color: "#616161"
                        visible: card.showTiming
                    }

                    Comp.PulseChannelEditor {
                        Layout.fillWidth: true
                        channelIndex: card.timingChannelIndex
                        channelName: card.timingChannelName
                        channel: slaveTimingController.timingModel
                                 ? slaveTimingController.timingModel.channel(card.timingChannelIndex) : null
                        controller: slaveTimingController
                        readOnly: !deviceComm.connected
                        visible: card.showTiming
                    }

                    Button {
                        text: "手动刷新"
                        font.pixelSize: 11
                        Material.background: "#3F51B5"
                        Material.foreground: "white"
                        enabled: deviceComm.connected
                        onClicked: slaveTimingController.readChannel(card.timingChannelIndex)
                        visible: card.showTiming
                    }
                }
            }
        }
    }

    component SlaveGateCard: DeviceCard {
        title: "从板 Gate 门控"
        showTiming: false

        statusContent: ColumnLayout {
            spacing: 6

            GridLayout {
                columns: 2
                columnSpacing: 8
                rowSpacing: 4

                Label { text: "当前电平"; color: "#757575"; font.pixelSize: 12 }
                Label {
                    text: gateLevelText(slaveGateController.model.gateLevel)
                    font.pixelSize: 12
                    color: slaveGateController.model.gateLevel === 0 ? "#FF9800" : "#4CAF50"
                }

                Label { text: "状态机"; color: "#757575"; font.pixelSize: 12 }
                Label {
                    text: gateStateText(slaveGateController.model.gateState)
                    font.pixelSize: 12
                    color: slaveGateController.model.gateState === 0 ? "#757575" : "#3F51B5"
                }
            }

            Button {
                text: "手动刷新状态"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: slaveGateController.readStatus()
            }
        }

        controlContent: ColumnLayout {
            spacing: 6

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "门控使能"
                editorType: "switch"
                switchValue: slaveGateController.model.gateEnable
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    slaveGateController.writeGateEnable(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "上升沿延时"
                unit: "μs"
                editorType: "spinbox"
                intValue: slaveGateController.model.riseDelayUs
                intFrom: 0
                intTo: 20000
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    slaveGateController.writeRiseDelayUs(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "下降沿停止延时"
                unit: "μs"
                editorType: "spinbox"
                intValue: slaveGateController.model.fallDelayUs
                intFrom: 0
                intTo: 20000
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    slaveGateController.writeFallDelayUs(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "Gate 极性"
                editorType: "combo"
                comboOptions: ["低电平有效", "高电平有效"]
                comboIndex: slaveGateController.model.polarity
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    slaveGateController.writePolarity(value)
                }
            }

            Button {
                text: "手动刷新控制回读"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: slaveGateController.readControl()
            }
        }
    }

    component SlavePumpCard: DeviceCard {
        id: pumpCard
        property var controller

        statusContent: ColumnLayout {
            spacing: 6
            RowLayout {
                spacing: 8
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: pumpCard.controller.model.online ? "#4CAF50" : "#9E9E9E"
                }
                Label {
                    text: pumpCard.controller.model.online ? "在线" : "离线"
                    font.pixelSize: 12
                    color: pumpCard.controller.model.online ? "#4CAF50" : "#9E9E9E"
                }
            }

            GridLayout {
                columns: 2
                columnSpacing: 8
                rowSpacing: 4
                Label { text: "使能回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.enableRb ? "已使能" : "已禁用"; font.pixelSize: 12 }
                Label { text: "状态码"; color: "#757575"; font.pixelSize: 12 }
                Label { text: "0x" + Number(pumpCard.controller.model.statusCode).toString(16).toUpperCase(); font.pixelSize: 12 }
                Label { text: "实际电流"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.currentActual + " A"; font.pixelSize: 12 }
                Label { text: "实际电压"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.voltageActual + " V"; font.pixelSize: 12 }
                Label { text: "实际频率"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.freqActual + " Hz"; font.pixelSize: 12 }
            }

            Button {
                text: "手动刷新状态"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: pumpCard.controller.readStatus()
            }
        }

        controlContent: ColumnLayout {
            spacing: 6

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "使能"
                editorType: "switch"
                switchValue: pumpCard.controller.model.enable
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeEnable(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "触发模式"
                editorType: "combo"
                comboOptions: ["内部触发", "外部触发"]
                comboIndex: pumpCard.controller.model.mode
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeMode(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "电流"
                unit: "A"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.current
                intFrom: 0
                intTo: 130
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeCurrent(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "脉宽"
                unit: "us"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.pulseWidth
                intFrom: 50
                intTo: 800
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writePulseWidth(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "限压"
                unit: "V"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.voltageLimit
                intFrom: 15
                intTo: 200
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeVoltageLimit(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "内部频率"
                unit: "Hz"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.intFreq
                intFrom: 1
                intTo: 500
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeIntFreq(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "外延迟"
                unit: "us"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.extDelay
                intFrom: 0
                intTo: 100
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeExtDelay(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "触发延时"
                unit: "us"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.delayOut
                intFrom: 50
                intTo: 850
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeDelayOut(value)
                }
            }

            Comp.InlineParamRow {
                Layout.fillWidth: true
                label: "从站地址"
                editorType: "spinbox"
                intValue: pumpCard.controller.model.addr
                intFrom: 1
                intTo: 247
                enabled: deviceComm.connected
                onValueCommitted: function(value) {
                    pumpCard.controller.writeAddr(value)
                }
            }

            Button {
                text: "手动刷新控制回读"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: pumpCard.controller.readControl()
            }
        }
    }

    component TimingOnlyCard: Pane {
        id: timingCard
        property string title: ""
        property int timingChannelIndex: 0
        property string timingChannelName: ""

        padding: 12
        Material.elevation: 2

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label { text: timingCard.title + " 时序"; font.pixelSize: 16; font.bold: true; color: "#3F51B5" }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#E0E0E0" }

            Comp.PulseChannelEditor {
                Layout.fillWidth: true
                channelIndex: timingCard.timingChannelIndex
                channelName: timingCard.timingChannelName
                channel: slaveTimingController.timingModel
                         ? slaveTimingController.timingModel.channel(timingCard.timingChannelIndex) : null
                controller: slaveTimingController
                readOnly: !deviceComm.connected
            }

            Button {
                text: "手动刷新"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: slaveTimingController.readChannel(timingCard.timingChannelIndex)
            }
        }
    }
}
