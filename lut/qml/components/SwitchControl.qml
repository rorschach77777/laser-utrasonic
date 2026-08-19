import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//标签 + 开关（Switch） 的组合行。
RowLayout {
    id: root

    property string label: ""
    property alias checked: switchControl.checked

    signal toggled(bool checked)

    spacing: 8

    Label {
        text: root.label + ":"
        color: "#757575"
        font.pixelSize: 13
        Layout.preferredWidth: 140
    }

    Switch {
        id: switchControl
        Material.accent: "#009688"
        onToggled: {
            console.log("[SwitchControl] toggled, checked:", switchControl.checked)
            root.toggled(switchControl.checked)
        }
    }
}
