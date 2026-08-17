import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//位掩码编辑器（一组复选框，用于按位设置值）。
ColumnLayout {
    id: root

    property string label: ""
    property int maskValue: 0
    property var options: []
    property bool enabled: true

    signal valueCommitted(int value)

    spacing: 8

    Label {
        text: root.label + ":"
        color: "#757575"
        font.pixelSize: 13
        font.bold: true
    }

    Flow {
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: root.options

            CheckBox {
                required property var modelData
                text: modelData.label
                checked: (root.maskValue & modelData.bit) !== 0
                enabled: root.enabled
                Material.accent: "#009688"
                onToggled: {
                    let newValue = root.maskValue
                    if (checked) {
                        newValue |= modelData.bit
                    } else {
                        newValue &= ~modelData.bit
                    }
                    root.valueCommitted(newValue)
                }
            }
        }
    }
}
