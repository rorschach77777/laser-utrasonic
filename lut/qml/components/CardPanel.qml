import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//带标题和阴影的白色卡片容器
Pane {
    id: root

    property string title: ""
    default property alias content: contentColumn.data

    Material.elevation: 2
    Material.background: "#FFFFFF"
    Material.roundedScale: Material.MediumScale

    padding: 16

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        spacing: 12

        Label {
            text: root.title
            font.pixelSize: 16
            font.bold: true
            color: "#3F51B5"
            visible: root.title.length > 0
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#BDBDBD"
            visible: root.title.length > 0
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
