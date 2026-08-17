import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
//带危险操作确认的按钮。
Button {
    id: root

    property bool dangerous: false
    property string confirmText: ""

    signal commandTriggered()

    Material.background: dangerous ? "#F44336" : "#3F51B5"
    Material.foreground: "white"
    Material.roundedScale: Material.SmallScale

    font.pixelSize: 13

    onClicked: {
        if (dangerous && confirmText.length > 0) {
            confirmDialog.open()
        } else {
            root.commandTriggered()
        }
    }

    Dialog {
        id: confirmDialog
        title: "Confirm Action"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay

        Label {
            text: root.confirmText
            wrapMode: Text.WordWrap
        }

        onAccepted: root.commandTriggered()
    }
}
