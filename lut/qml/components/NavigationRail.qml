import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
//侧边栏导航菜单
Rectangle {
    id: root

    property int currentIndex: 0
    signal pageSelected(int index)

    width: 72
    color: "#3F51B5"

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 0

        // App logo / title
        Label {
            text: "LST"
            font.pixelSize: 18
            font.bold: true
            color: "white"
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            height: 1
            color: Qt.rgba(1, 1, 1, 0.3)
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: navColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: navColumn
                width: parent.width
                spacing: 0

                Repeater {
                    model: ListModel {
                        ListElement { label: "Connect";  icon: "[+]" }
                        ListElement { label: "Dash";     icon: "[#]" }
                        ListElement { label: "DFB";      icon: "[~]" }
                        ListElement { label: "MM Pump";  icon: "[M]" }
                        ListElement { label: "Pump1/2";  icon: "[P]" }
                        ListElement { label: "Slave";    icon: "[S]" }
                        ListElement { label: "Main T";   icon: "[T]" }
                        ListElement { label: "Slave T";  icon: "[t]" }
                        ListElement { label: "Temp";     icon: "[C]" }
                        ListElement { label: "Settings"; icon: "[=]" }
                        ListElement { label: "Cmds";     icon: "[!]" }
                    }

                    delegate: ItemDelegate {
                        id: navItem
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        highlighted: root.currentIndex === index

                        background: Rectangle {
                            color: navItem.highlighted ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                            Rectangle {
                                width: 3
                                height: parent.height
                                color: "white"
                                visible: navItem.highlighted
                            }
                        }

                        contentItem: ColumnLayout {
                            spacing: 2

                            Label {
                                text: model.icon
                                font.pixelSize: 14
                                font.bold: true
                                color: navItem.highlighted ? "white" : Qt.rgba(1, 1, 1, 0.7)
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: model.label
                                font.pixelSize: 9
                                color: navItem.highlighted ? "white" : Qt.rgba(1, 1, 1, 0.7)
                                Layout.alignment: Qt.AlignHCenter
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        onClicked: {
                            root.currentIndex = index
                            root.pageSelected(index)
                        }
                    }
                }
            }
        }
    }
}
