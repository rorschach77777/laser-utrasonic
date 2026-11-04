import QtQuick
import QtQuick.Controls

// Shared axes for engineering result plots. The viewport contains only data geometry.
Item {
    id: root
    property alias viewport: plotArea
    property real xMin: 0
    property real xMax: 1
    property real yMin: 0
    property real yMax: 1
    property bool hasData: false
    property string xTitle: "轴向位置 / mm"
    property int xDecimals: 0
    property string emptyText: "等待测量数据"

    Rectangle {
        anchors.fill: plotArea
        color: "#101317"
        border.color: "#353c45"
        radius: 1
    }
    Repeater {
        model: 5
        Item {
            required property int index
            x: plotArea.x
            y: plotArea.y + index * plotArea.height / 4
            width: plotArea.width
            Rectangle { width: parent.width; height: 1; color: "#272e36" }
            Text {
                anchors.right: parent.left
                anchors.rightMargin: 8
                y: -6
                text: root.hasData ? (root.yMax - index * (root.yMax - root.yMin) / 4).toFixed(2) : "—"
                color: "#a5afb9"
                font.pixelSize: 10
            }
        }
    }
    Repeater {
        model: 5
        Item {
            required property int index
            x: plotArea.x + index * plotArea.width / 4
            y: plotArea.y
            height: plotArea.height
            Rectangle { height: parent.height; width: 1; color: "#272e36" }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height + 7
                text: root.hasData ? (root.xMin + index * (root.xMax - root.xMin) / 4).toFixed(root.xDecimals) : "—"
                color: "#a5afb9"
                font.pixelSize: 10
            }
        }
    }
    Item {
        id: plotArea
        anchors.fill: parent
        anchors.leftMargin: 52
        anchors.rightMargin: 18
        anchors.topMargin: 22
        anchors.bottomMargin: 42
        clip: true
    }
    Text {
        x: plotArea.x
        text: "壁厚 / mm"
        color: "#929da8"
        font.pixelSize: 10
    }
    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: plotArea.horizontalCenter
        text: root.xTitle
        color: "#929da8"
        font.pixelSize: 10
    }
    Column {
        anchors.centerIn: plotArea
        spacing: 8
        visible: !root.hasData
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "— —"; color: "#44515e"; font.pixelSize: 24 }
        Text { text: root.emptyText; color: "#929da8"; font.pixelSize: 12 }
    }
}
