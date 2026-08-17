import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
//标签 + 数值文本 + 单位 的只读显示行。
//用于显示测量值或状态（如温度、压力）。value 属性控制显示内容，valueColor 可改变数值颜色（如正常为黑，报警为红）。
RowLayout {
    id: root

    property string label: ""
    property string value: "--"
    property string unit: ""
    property color valueColor: "#212121"

    spacing: 8

    Label {
        text: root.label + ":"
        color: "#757575"
        font.pixelSize: 13
        Layout.preferredWidth: 140
    }

    Label {
        text: root.value
        color: root.valueColor
        font.pixelSize: 13
        font.bold: true
    }

    Label {
        text: root.unit
        color: "#757575"
        font.pixelSize: 12
        visible: root.unit.length > 0
    }
}
