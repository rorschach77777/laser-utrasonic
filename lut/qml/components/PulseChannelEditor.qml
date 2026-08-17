import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "." as Comp
//脉冲通道参数编辑器（复杂专用控件）。
Pane {
    id: root

    property var channel: null
    property int channelIndex: 0
    property string channelName: "CH" + channelIndex
    property bool readOnly: false
    property var controller: null

    Material.elevation: 1
    Material.background: "#FFFFFF"
    Material.roundedScale: Material.SmallScale
    padding: 12

    function pulseActive(index) {
        return root.channel && index < root.channel.pulseCount
    }

    function writeTimingField(fieldKey, value) {
        if (!root.controller || !root.channel) return

        const regs = []
        regs.push(root.channel.period)
        regs.push(root.channel.pulseCount)
        for (let i = 0; i < 8; ++i) {
            regs.push(root.channel.getDelay(i))
            regs.push(root.channel.getWidth(i))
        }

        if (fieldKey === "period") {
            regs[0] = value
        } else if (fieldKey === "pulseCount") {
            regs[1] = value
        } else {
            const parts = fieldKey.split(":")
            const pulseIndex = parseInt(parts[1])
            if (parts[0] === "delay") {
                regs[2 + pulseIndex * 2] = value
            } else {
                regs[3 + pulseIndex * 2] = value
            }
        }

        root.controller.writeChannelRegisters(root.channelIndex, regs)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            spacing: 8

            Label {
                text: "CH" + root.channelIndex + " - " + root.channelName
                font.pixelSize: 14
                font.bold: true
                color: "#3F51B5"
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#E0E0E0"
        }

        Comp.InlineParamRow {
            Layout.fillWidth: true
            label: "Period"
            unit: "ms"
            editorType: "spinbox"
            intValue: root.channel ? root.channel.period : 0
            intFrom: 1
            intTo: 65535
            enabled: !root.readOnly && root.channel && root.controller
            onValueCommitted: function(value) {
                root.writeTimingField("period", value)
            }
        }

        Comp.InlineParamRow {
            Layout.fillWidth: true
            label: "Pulse Count"
            editorType: "spinbox"
            intValue: root.channel ? root.channel.pulseCount : 0
            intFrom: 1
            intTo: 8
            enabled: !root.readOnly && root.channel && root.controller
            onValueCommitted: function(value) {
                root.writeTimingField("pulseCount", value)
            }
        }

        Label {
            text: "Pulse Timing"
            font.pixelSize: 12
            color: "#757575"
            visible: root.channel !== null
        }

        Repeater {
            model: 8

            delegate: ColumnLayout {
                required property int index
                visible: root.pulseActive(index)
                spacing: 4

                Label {
                    text: "Pulse " + (index + 1)
                    font.pixelSize: 12
                    font.bold: true
                    color: "#616161"
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "Delay"
                    unit: "us"
                    editorType: "spinbox"
                    intValue: root.channel ? root.channel.getDelay(index) : 0
                    intFrom: 0
                    intTo: 65535
                    enabled: !root.readOnly && root.channel && root.controller
                    onValueCommitted: function(value) {
                        root.writeTimingField("delay:" + index, value)
                    }
                }

                Comp.InlineParamRow {
                    Layout.fillWidth: true
                    label: "Width"
                    unit: "us"
                    editorType: "spinbox"
                    intValue: root.channel ? root.channel.getWidth(index) : 0
                    intFrom: 1
                    intTo: 65535
                    enabled: !root.readOnly && root.channel && root.controller
                    onValueCommitted: function(value) {
                        root.writeTimingField("width:" + index, value)
                    }
                }
            }
        }
    }
}
