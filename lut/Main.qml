import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "qml/pages"
import "qml/components"

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "LUT 上位机 V1.11"

    readonly property bool engineeringPageActive: stackLayout.currentIndex === 4
    readonly property var engineeringIcons: ["○", "▤", "▣", "◈", "▥", "▦", "⌁", "⚙", "›"]

    Material.theme: engineeringPageActive ? Material.Dark : Material.Light
    Material.primary: "#3F51B5"
    Material.accent: "#009688"

    header: ToolBar {
        Material.background: window.engineeringPageActive ? "#1b1f25" : "#3F51B5"
        Material.foreground: "white"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Label {
                text: "LUT"
                font.pixelSize: 18
                font.bold: true
                color: "white"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 10; height: 10; radius: 5
                color: deviceComm.connected ? "#4CAF50" : "#F44336"
            }

            Label {
                text: deviceComm.connected
                      ? connectionController.selectedPort + " @ " + connectionController.baudRate + " bps"
                      : "未连接"
                color: "white"
                font.pixelSize: 13
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Navigation Rail
        Pane {
            Layout.fillHeight: true
            Layout.preferredWidth: window.engineeringPageActive ? 148 : 80
            padding: 0
            Material.elevation: 4
            Material.background: window.engineeringPageActive ? "#1b1f25" : "#FAFAFA"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: ListModel {
                        ListElement { label: "连接";   icon: "🔗" }
                        ListElement { label: "总览";   icon: "📊" }
                        ListElement { label: "主板";   icon: "🖥" }
                        ListElement { label: "从板";   icon: "📡" }
                        ListElement { label: "数据";   icon: "📈" }
                        ListElement { label: "历史";   icon: "📜" }
                        ListElement { label: "工艺";   icon: "🧭" }
                        ListElement { label: "设置";   icon: "⚙" }
                        ListElement { label: "命令";   icon: "▶" }
                    }

                    delegate: ItemDelegate {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        highlighted: stackLayout.currentIndex === index
                        enabled: index === 0 || index === 4 || index === 5 || index === 6 || deviceComm.connected

                        background: Rectangle {
                            color: window.engineeringPageActive
                                   ? (parent.highlighted ? "#34302a" : "#1b1f25")
                                   : (parent.highlighted ? "#E8EAF6" : "#FAFAFA")
                            Rectangle {
                                visible: window.engineeringPageActive && parent.parent.highlighted
                                width: 3; height: parent.height
                                color: "#ee9b45"
                            }
                        }
                        contentItem: GridLayout {
                            columns: window.engineeringPageActive ? 2 : 1
                            columnSpacing: 10
                            rowSpacing: 2

                            Label {
                                text: window.engineeringPageActive ? window.engineeringIcons[index] : model.icon
                                color: window.engineeringPageActive && parent.parent.highlighted ? "#ee9b45" : (window.engineeringPageActive ? "#a6b0ba" : "#212121")
                                font.pixelSize: window.engineeringPageActive ? 18 : 20
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: !window.engineeringPageActive
                            }

                            Label {
                                text: model.label
                                Layout.leftMargin: window.engineeringPageActive ? 4 : 0
                                font.pixelSize: window.engineeringPageActive ? 12 : 10
                                horizontalAlignment: window.engineeringPageActive ? Text.AlignLeft : Text.AlignHCenter
                                Layout.fillWidth: true
                                color: window.engineeringPageActive
                                       ? (parent.parent.highlighted ? "#ee9b45" : (parent.parent.enabled ? "#c0c8d0" : "#646e78"))
                                       : (parent.parent.highlighted ? "#3F51B5" : (parent.parent.enabled ? "#212121" : "#BDBDBD"))
                            }
                        }

                        onClicked: stackLayout.currentIndex = index
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Page Content
        StackLayout {
            id: stackLayout
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            ConnectionPage {}
            DashboardPage {}
            MainBoardPage {}
            SlaveBoardPage {}
            DataPage {}
            HistoryPage {
                historyMgr: historyManager
                onAnalysisRequested: function(recordIndex) {
                    processAnalyzer.analyzeRecord(recordIndex)
                    stackLayout.currentIndex = 6
                }
            }
            ProcessAnalysisPage {}
            SystemSettingsPage {}
            CommandPage {}
        }
    }

    function stopAllPolling() {
        pollingService.stop()
    }



    Connections {
        target: deviceComm
        function onConnectedChanged() {
            if (deviceComm.connected) {
                stackLayout.currentIndex = 0
            } else {
                stopAllPolling()
                stackLayout.currentIndex = 0
            }
        }
    }

    Connections {
        target: connectionController
        function onDeviceInfoReadFinished(success) {
            if (!deviceComm.connected)
                return
            if (success) {
                systemSettingsController.readAll()
            } else {
                stopAllPolling()
                stackLayout.currentIndex = 0
            }
        }
    }

    Connections {
        target: systemSettingsController
        function onReadAllFinished(success) {
            if (!deviceComm.connected)
                return
            if (success) {
                pollingService.start()
                stackLayout.currentIndex = 1
            } else {
                stopAllPolling()
                stackLayout.currentIndex = 0
            }
        }
    }
}
