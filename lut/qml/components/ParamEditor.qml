import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//标签 + 数字调节框（SpinBox） 的组合行
RowLayout {
    id: root

    property string label: ""
    property real value: 0
    property real minValue: 0
    property real maxValue: 65535
    property real stepSize: 1
    property string suffix: ""
    property int decimals: 0

    signal valueChanged(real newValue)

    spacing: 8

    Label {
        text: root.label + ":"
        color: "#757575"
        font.pixelSize: 13
        Layout.preferredWidth: 140
    }

    SpinBox {
        id: spinBox
        Layout.preferredWidth: 210

        property int factor: Math.pow(10, root.decimals)

        from: Math.round(root.minValue * factor)
        to: Math.round(root.maxValue * factor)
        stepSize: Math.round(root.stepSize * factor)
        value: Math.round(root.value * factor)
        editable: true

        validator: DoubleValidator {
            bottom: root.minValue
            top: root.maxValue
            decimals: root.decimals
        }

        textFromValue: function(value, locale) {
            if (root.decimals > 0) {
                return (value / factor).toFixed(root.decimals)
            }
            return value.toString()
        }

        valueFromText: function(text, locale) {
            if (root.decimals > 0) {
                return Math.round(parseFloat(text) * factor)
            }
            return parseInt(text)
        }

        onValueModified: {
            var realVal = root.decimals > 0 ? value / factor : value
            root.valueChanged(realVal)
        }
    }

    Label {
        text: root.suffix
        color: "#757575"
        font.pixelSize: 12
        visible: root.suffix.length > 0
    }
}
