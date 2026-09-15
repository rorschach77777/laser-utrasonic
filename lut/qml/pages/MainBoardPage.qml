import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "../components" as Comp

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true

    function onOffText(value) {
        return value ? "开启" : "关闭"
    }

    function dfbDriveModeText(value) {
        return value === 0 ? "外部驱动" : "内部直流"
    }

    function pumpModeText(value) {
        return value === 0 ? "内部触发" : "外部触发"
    }

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

    Dialog {
        id: interlockDialog
        property string message: ""
        anchors.centerIn: parent
        title: "操作提示"
        modal: true
        width: 360
        standardButtons: Dialog.Ok
        Label { text: interlockDialog.message; wrapMode: Text.Wrap }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        Label {
            text: "主板设备总览"
            font.pixelSize: 24
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 16
            Layout.topMargin: 12
        }

        DeviceCard {
            title: "Gate 门控"
            showTiming: false
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            statusContent: ColumnLayout {
                spacing: 6

                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Label { text: "当前电平"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: gateLevelText(gateController.model.gateLevel)
                        font.pixelSize: 12
                        color: gateController.model.gateLevel === 0 ? "#FF9800" : "#4CAF50"
                    }

                    Label { text: "状态机"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: gateStateText(gateController.model.gateState)
                        font.pixelSize: 12
                        color: gateController.model.gateState === 0 ? "#757575" : "#3F51B5"
                    }
                }

                Button {
                    text: "手动刷新状态"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: gateController.readStatus()
                }
            }

            controlContent: ColumnLayout {
                spacing: 6

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "门控使能"
                    editorType: "switch"
                    switchValue: gateController.model.gateEnable
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        gateController.writeGateEnable(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "上升沿延时"
                    unit: "μs"
                    editorType: "spinbox"
                    intValue: gateController.model.riseDelayUs
                    intFrom: 0
                    intTo: 20000
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        gateController.writeRiseDelayUs(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "下降沿停止延时"
                    unit: "μs"
                    editorType: "spinbox"
                    intValue: gateController.model.fallDelayUs
                    intFrom: 0
                    intTo: 20000
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        gateController.writeFallDelayUs(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "Gate 极性"
                    editorType: "combo"
                    comboOptions: ["低电平有效", "高电平有效"]
                    comboIndex: gateController.model.polarity
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        gateController.writePolarity(value)
                    }
                }

                Button {
                    text: "手动刷新控制回读"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: gateController.readControl()
                }
            }
        }

        Comp.WaterCoolerCard {
            controller: waterCoolerController
            title: "水冷箱"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        DeviceCard {
            title: "DFB 激光器"
            showTiming: false
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            statusContent: ColumnLayout {
                spacing: 6

                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: dfbController.model.online
                               ? (dfbController.model.fault > 0 ? "#F44336" : "#4CAF50")
                               : "#9E9E9E"
                    }
                    Label {
                        text: dfbController.model.online ? "在线" : "离线"
                        font.pixelSize: 12
                        color: dfbController.model.online ? "#4CAF50" : "#9E9E9E"
                    }
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Label { text: "故障码"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: "0x" + Number(dfbController.model.fault).toString(16).toUpperCase()
                        font.pixelSize: 12
                        color: dfbController.model.fault > 0 ? "#F44336" : "#4CAF50"
                    }
                    Label { text: "TEC 状态"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: dfbController.model.tecStatus ? "稳定" : "未稳定"
                        font.pixelSize: 12
                        color: dfbController.model.tecStatus ? "#4CAF50" : "#FF9800"
                    }
                    Label { text: "温度"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dfbController.model.temperature.toFixed(2) + " °C"; font.pixelSize: 12 }
                    Label { text: "电流"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dfbController.model.current.toFixed(1) + " mA"; font.pixelSize: 12 }
                    Label { text: "PD 电压"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: dfbController.model.pdVoltage.toFixed(1) + " mV"; font.pixelSize: 12 }
                }

                Button {
                    text: "手动刷新状态"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: dfbController.readStatus()
                }
            }

            controlContent: ColumnLayout {
                spacing: 6

                Comp.InlineParamRow {
                    id: dfbSwitchRow
                    Layout.fillWidth: true
                    label: "DFB 开关"
                    editorType: "switch"
                    switchValue: dfbController.model.dfbSwitch
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        dfbController.writeSwitch(value)
                    }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: ldSwitchRow.implicitHeight

                    Comp.InlineParamRow {
                        id: ldSwitchRow
                        anchors.fill: parent
                        label: "LD 开关"
                        editorType: "switch"
                        switchValue: dfbController.model.ldSwitch
                        // enabled: deviceComm.connected && !multiModePumpController.model.pumpSwitch
                        enabled: deviceComm.connected
                        onValueCommitted: function(value) {
                            dfbController.writeLdSwitch(value)
                        }
                    }

                    // MouseArea {
                    //     anchors.fill: parent
                    //     enabled: multiModePumpController.model.pumpSwitch
                    //     visible: enabled
                    //     z: 1
                    //     onClicked: {
                    //         interlockDialog.message = "请先关闭多模泵，再关闭 LD"
                    //         interlockDialog.open()
                    //     }
                    // }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "目标温度"
                    unit: "°C"
                    editorType: "doublefield"
                    doubleValue: dfbController.model.tempTarget
                    doubleFrom: -50
                    doubleTo: 100
                    decimals: 2
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        dfbController.writeTempTarget(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "驱动模式"
                    editorType: "combo"
                    comboOptions: ["外部驱动", "内部直流"]
                    comboIndex: dfbController.model.driveMode
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        dfbController.writeDriveMode(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "电流设置"
                    unit: "mA"
                    editorType: "doublefield"
                    doubleValue: dfbController.model.currentSet
                    doubleFrom: 0
                    doubleTo: 3000
                    decimals: 1
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        dfbController.writeCurrentSet(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "最大电流"
                    unit: "mA"
                    editorType: "doublefield"
                    doubleValue: dfbController.model.currentMax
                    doubleFrom: 0
                    doubleTo: 3000
                    decimals: 1
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        dfbController.writeCurrentMax(value)
                    }
                }

                Button {
                    text: "手动刷新控制回读"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: dfbController.readControl()
                }
            }

            timingChannelIndex: 0
            timingChannelName: "DFB_EN"
        }

        DeviceCard {
            title: "多模泵"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            statusContent: ColumnLayout {
                spacing: 6

                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: multiModePumpController.model.online
                               ? (multiModePumpController.model.fault > 0 ? "#F44336" : "#4CAF50")
                               : "#9E9E9E"
                    }
                    Label {
                        text: multiModePumpController.model.online ? "在线" : "离线"
                        font.pixelSize: 12
                        color: multiModePumpController.model.online ? "#4CAF50" : "#9E9E9E"
                    }
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Label { text: "故障码"; color: "#757575"; font.pixelSize: 12 }
                    Label {
                        text: "0x" + Number(multiModePumpController.model.fault).toString(16).toUpperCase()
                        font.pixelSize: 12
                        color: multiModePumpController.model.fault > 0 ? "#F44336" : "#4CAF50"
                    }
                    Label { text: "实际电流"; color: "#757575"; font.pixelSize: 12 }
                    Label { text: multiModePumpController.model.currentActual + " mA"; font.pixelSize: 12 }
                }

                Button {
                    text: "手动刷新状态"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: multiModePumpController.readStatus()
                }
            }

            controlContent: ColumnLayout {
                spacing: 6

                Item {
                    Layout.fillWidth: true
                    implicitHeight: pumpSwitchRow.implicitHeight

                    Comp.InlineParamRow {
                        id: pumpSwitchRow
                        anchors.fill: parent
                        label: "泵开关"
                        editorType: "switch"
                        switchValue: multiModePumpController.model.pumpSwitch
                        // enabled: deviceComm.connected && dfbController.model.online && dfbController.model.ldSwitch
                        enabled: deviceComm.connected
                        onValueCommitted: function(value) {
                            multiModePumpController.writeSwitch(value)
                        }
                    }

                    // MouseArea {
                    //     anchors.fill: parent
                    //     enabled: !dfbController.model.online || !dfbController.model.ldSwitch
                    //     visible: enabled
                    //     z: 1
                    //     onClicked: {
                    //         if (!dfbController.model.online)
                    //             interlockDialog.message = "DFB 离线，无法开启多模泵"
                    //         else
                    //             interlockDialog.message = "请先开启 LD，再开启多模泵"
                    //         interlockDialog.open()
                    //     }
                    // }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "驱动模式"
                    editorType: "combo"
                    comboOptions: ["外部驱动", "内部直流"]
                    comboIndex: multiModePumpController.model.driveMode
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        multiModePumpController.writeDriveMode(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "电流设置"
                    unit: "mA"
                    editorType: "spinbox"
                    intValue: multiModePumpController.model.currentSet
                    intFrom: 0
                    intTo: 10000
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        multiModePumpController.writeCurrentSet(value)
                    }
                }

                Button {
                    text: "手动刷新控制回读"
                    font.pixelSize: 11
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    enabled: deviceComm.connected
                    onClicked: multiModePumpController.readControl()
                }
            }

            timingChannelIndex: 1
            timingChannelName: "PUMP_MM_EN"
        }

        PumpCard {
            controller: pump1Controller
            title: "泵浦 1"
            timingChannelIndex: 2
            timingChannelName: "PUMP1_EN"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        PumpCard {
            controller: pump2Controller
            title: "泵浦 2"
            timingChannelIndex: 3
            timingChannelName: "PUMP2_EN"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 16

            Item { Layout.fillWidth: true }

            TimingOnlyCard {
                title: "CH5"
                timingChannelIndex: 5
                timingChannelName: "CH5"
                Layout.preferredWidth: 400
            }

            TimingOnlyCard {
                title: "CH6"
                timingChannelIndex: 6
                timingChannelName: "CH6"
                Layout.preferredWidth: 400
            }
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
                        channel: mainTimingController.timingModel
                                 ? mainTimingController.timingModel.channel(card.timingChannelIndex) : null
                        controller: mainTimingController
                        readOnly: !deviceComm.connected
                        visible: card.showTiming
                    }

                    Button {
                        text: "手动刷新"
                        font.pixelSize: 11
                        Material.background: "#3F51B5"
                        Material.foreground: "white"
                        enabled: deviceComm.connected
                        onClicked: mainTimingController.readChannel(card.timingChannelIndex)
                        visible: card.showTiming
                    }
                }
            }
        }
    }

    component PumpCard: DeviceCard {
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
                Label { text: "电流回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.currentRb + " A"; font.pixelSize: 12 }
                Label { text: "脉宽回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.pulseWidthRb + " us"; font.pixelSize: 12 }
                Label { text: "限压回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.voltageLimitRb + " V"; font.pixelSize: 12 }
                Label { text: "内频回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.intFreqRb + " Hz"; font.pixelSize: 12 }
                Label { text: "外延迟回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.extDelayRb + " us"; font.pixelSize: 12 }
                Label { text: "触发延时回读"; color: "#757575"; font.pixelSize: 12 }
                Label { text: pumpCard.controller.model.delayOutRb + " us"; font.pixelSize: 12 }
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
                channel: mainTimingController.timingModel
                         ? mainTimingController.timingModel.channel(timingCard.timingChannelIndex) : null
                controller: mainTimingController
                readOnly: !deviceComm.connected
            }

            Button {
                text: "手动刷新"
                font.pixelSize: 11
                Material.background: "#3F51B5"
                Material.foreground: "white"
                enabled: deviceComm.connected
                onClicked: mainTimingController.readChannel(timingCard.timingChannelIndex)
            }
        }
    }
}
