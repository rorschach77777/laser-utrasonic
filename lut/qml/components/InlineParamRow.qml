import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

//动态输入行，根据 editorType 自动切换为开关、下拉框、数字框或文本输入框。
RowLayout {
    id: root

    property string label: ""
    property string unit: ""
    property bool enabled: true

    // Editor type: "switch", "combo", "spinbox", "doublefield"
    property string editorType: "switch"

    property int editorWidth: -1

    // For switch
    property bool switchValue: false

    // For combo
    property var comboOptions: []
    property int comboIndex: 0

    // For spinbox
    property int intValue: 0
    property int intFrom: 0
    property int intTo: 100
    property int intStep: 1

    // For doublefield
    property real doubleValue: 0.0
    property real doubleFrom: 0.0
    property real doubleTo: 100.0
    property int decimals: 2

    signal valueCommitted(var value)

    spacing: 8

    Label {
        text: root.label + ":"
        color: "#757575"
        font.pixelSize: 13
        Layout.alignment: Qt.AlignVCenter
    }

    Item {
        Layout.fillWidth: true
    }

    Loader {
        id: editorLoader
        Layout.alignment: Qt.AlignVCenter

        Layout.preferredWidth: {
            if (root.editorWidth > 0) return root.editorWidth
            switch (root.editorType) {    
            case "switch": return 122
            case "combo": return 210
            case "spinbox": return 190
            case "doublefield": return 190
            default: return 190
            }
        }

        sourceComponent: {
            switch (root.editorType) {
            case "switch": return switchComponent
            case "combo": return comboComponent
            case "spinbox": return spinboxComponent
            case "doublefield": return doublefieldComponent
            default: return null
            }
        }
    }

    Label {
        visible: root.unit.length > 0
        text: root.unit
        color: "#757575"
        font.pixelSize: 12
        Layout.alignment: Qt.AlignVCenter
    }

    Component {
        id: switchComponent
        Switch {
            checked: root.switchValue
            enabled: root.enabled
            Material.accent: "#009688"
            onToggled: root.valueCommitted(checked)
        }
    }

    Component {
        id: comboComponent
        ComboBox {
            model: root.comboOptions
            currentIndex: root.comboIndex
            enabled: root.enabled
            onActivated: root.valueCommitted(currentIndex)
        }
    }

    Component {
        id: spinboxComponent
        SpinBox {
            from: root.intFrom
            to: root.intTo
            stepSize: root.intStep
            value: root.intValue
            enabled: root.enabled
            editable: true
            onValueModified: root.valueCommitted(value)
        }
    }

    Component {
        id: doublefieldComponent
        TextField {
            id: doubleField
            text: root.doubleValue.toFixed(root.decimals)
            enabled: root.enabled
            selectByMouse: true
            validator: DoubleValidator {
                bottom: root.doubleFrom
                top: root.doubleTo
                decimals: root.decimals
            }
            onEditingFinished: {
                const val = parseFloat(text)
                if (!isNaN(val)) {
                    root.valueCommitted(val)
                }
            }
        }
    }
}
