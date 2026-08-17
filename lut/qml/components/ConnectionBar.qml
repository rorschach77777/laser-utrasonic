import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//顶部/底部的连接状态栏，实时显示设备的连接状态、当前串口号和波特率。。
Rectangle {
    id: root

    height: 40
    color: "#FFFFFF"

    // Bottom border
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: "#BDBDBD"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        StatusIndicator {
            status: deviceComm.connected ? "online" : "offline"
            size: 10
        }

        Label {
            text: deviceComm.connected ? "Connected" : "Disconnected"
            font.pixelSize: 13
            font.bold: true
            color: deviceComm.connected ? "#4CAF50" : "#9E9E9E"
        }

        Rectangle {
            width: 1
            height: 20
            color: "#BDBDBD"
            visible: deviceComm.connected
        }

        Label {
            text: deviceComm.connected ? connectionController.selectedPort : ""
            font.pixelSize: 12
            color: "#757575"
            visible: deviceComm.connected
        }

        Label {
            text: deviceComm.connected ? (connectionController.baudRate + " bps") : ""
            font.pixelSize: 12
            color: "#757575"
            visible: deviceComm.connected
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "LST Control System"
            font.pixelSize: 13
            color: "#757575"
        }
    }
}
