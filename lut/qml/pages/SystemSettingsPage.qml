import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components" as Comp

ScrollView {
    id: root
    contentWidth: availableWidth

    readonly property var baudOptions: ["4800", "9600", "19200", "38400", "57600", "115200", "230400"]
    readonly property var mainModuleOptions: [
        { label: "从板", bit: 0x01 },
        { label: "DFB", bit: 0x02 },
        { label: "多模泵", bit: 0x04 },
        { label: "泵浦1", bit: 0x08 },
        { label: "泵浦2", bit: 0x10 },
        { label: "功率监测", bit: 0x20 }
    ]
    readonly property var slaveModuleOptions: [
        { label: "从板泵浦1", bit: 0x01 },
        { label: "从板泵浦2", bit: 0x02 }
    ]
    readonly property var pulseChannelOptions: [
        { label: "CH0", bit: 0x0001 }, { label: "CH1", bit: 0x0002 },
        { label: "CH2", bit: 0x0004 }, { label: "CH3", bit: 0x0008 },
        { label: "CH4", bit: 0x0010 }, { label: "CH5", bit: 0x0020 },
        { label: "CH6", bit: 0x0040 }, { label: "CH7", bit: 0x0080 },
        { label: "CH8", bit: 0x0100 }, { label: "CH9", bit: 0x0200 },
        { label: "CH10", bit: 0x0400 }, { label: "CH11", bit: 0x0800 },
        { label: "CH12", bit: 0x1000 }, { label: "CH13", bit: 0x2000 },
        { label: "CH14", bit: 0x4000 }, { label: "CH15", bit: 0x8000 }
    ]

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16

            Label {
                text: "系统设置"
                font.pixelSize: 24
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: "从设备重读设置"
                enabled: deviceComm.connected
                onClicked: systemSettingsController.readAll()
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "通信参数"; font.pixelSize: 18; font.bold: true }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "从机地址"
                    editorType: "spinbox"
                    intValue: systemSettingsController.slaveAddr
                    intFrom: 1
                    intTo: 247
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlaveAddr(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "波特率"
                    editorType: "combo"
                    comboOptions: root.baudOptions
                    comboIndex: systemSettingsController.baudRateCode
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeBaudRate(value)
                    }
                }
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "模块通信使能"; font.pixelSize: 18; font.bold: true }

                Comp.InlineMaskEditor {
                    Layout.fillWidth: true
                    label: "主板子模块使能"
                    maskValue: systemSettingsController.subModuleEnable
                    options: root.mainModuleOptions
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSubModuleEnable(value)
                    }
                }

                Comp.InlineMaskEditor {
                    Layout.fillWidth: true
                    label: "从板子模块使能"
                    maskValue: systemSettingsController.slaveSubModuleEnable
                    options: root.slaveModuleOptions
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlaveSubModuleEnable(value)
                    }
                }
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "脉冲通道与参数"; font.pixelSize: 18; font.bold: true }

                Comp.InlineMaskEditor {
                    Layout.fillWidth: true
                    label: "主板通道使能"
                    maskValue: systemSettingsController.pulseChEnable
                    options: root.pulseChannelOptions
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writePulseChEnable(value)
                    }
                }

                Comp.InlineMaskEditor {
                    Layout.fillWidth: true
                    label: "从板通道使能"
                    maskValue: systemSettingsController.slavePulseChEnable
                    options: root.pulseChannelOptions
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlavePulseChEnable(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "主板脉冲周期"
                    unit: "ms"
                    editorType: "spinbox"
                    intValue: systemSettingsController.pulsePeriod
                    intFrom: 1
                    intTo: 65535
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writePulsePeriod(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "主板 Pulse Count"
                    editorType: "spinbox"
                    intValue: systemSettingsController.pulseCount
                    intFrom: 1
                    intTo: 8
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writePulseCount(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "从板脉冲周期"
                    unit: "ms"
                    editorType: "spinbox"
                    intValue: systemSettingsController.slavePulsePeriod
                    intFrom: 1
                    intTo: 65535
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlavePulsePeriod(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "从板 Pulse Count"
                    editorType: "spinbox"
                    intValue: systemSettingsController.slavePulseCount
                    intFrom: 1
                    intTo: 8
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlavePulseCount(value)
                    }
                }
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 16
            Material.elevation: 2
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "同步设置"; font.pixelSize: 18; font.bold: true }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "主板同步使能"
                    editorType: "switch"
                    switchValue: systemSettingsController.mainSyncEnable
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeMainSyncEnable(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "主板 Tw"
                    unit: "10ns"
                    editorType: "spinbox"
                    intValue: systemSettingsController.mainTw
                    intFrom: 0
                    intTo: 2147483647
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeMainTw(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "从板同步使能"
                    editorType: "switch"
                    switchValue: systemSettingsController.slaveSyncEnable
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlaveSyncEnable(value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "从板 Tw"
                    unit: "10ns"
                    editorType: "spinbox"
                    intValue: systemSettingsController.slaveTw
                    intFrom: 0
                    intTo: 2147483647
                    enabled: deviceComm.connected
                    onValueCommitted: function(value) {
                        systemSettingsController.writeSlaveTw(value)
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
