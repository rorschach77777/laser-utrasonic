import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import LST.Data 1.0

Rectangle {
    id: root
    color: "#10151c"
    Material.theme: Material.Dark
    Material.accent: "#42c6bd"
    property bool experimentLoaded: false
    readonly property color muted: "#91a2b5"
    readonly property color ink: "#e5edf5"

    component Metric: Rectangle {
        property string caption
        property string reading
        property color accent: "#e5edf5"
        color: "#1a2430"
        radius: 6
        implicitHeight: 94
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            Label { text: caption; color: root.muted; font.pixelSize: 12 }
            Label { text: reading; color: accent; font.pixelSize: 25; font.bold: true }
        }
    }
    component Detail: RowLayout {
        property string caption
        property string reading
        Layout.fillWidth: true
        Label { text: caption; color: root.muted; Layout.fillWidth: true }
        Label { text: reading; color: root.ink; font.bold: true }
    }

    ScrollView {
        id: engineeringScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: engineeringScroll.availableWidth
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                ColumnLayout {
                    Label { text: "在线测厚"; color: root.ink; font.pixelSize: 25; font.bold: true }
                    Label { text: "MEASUREMENTS  /  生产监视"; color: root.muted; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    implicitWidth: 136; implicitHeight: 34; radius: 17
                    color: dataController.running && !dataController.paused ? "#163d37" : "#28313d"
                    Label {
                        anchors.centerIn: parent
                        text: dataController.running ? (dataController.paused ? "●  已暂停" : "●  正在测量") : "●  待机"
                        color: dataController.running && !dataController.paused ? "#68e0bf" : root.muted
                    }
                }
                Button { text: "实验"; onClicked: { root.experimentLoaded = true; experiment.open() } }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18; Layout.rightMargin: 18
                Button { text: "开始测量"; highlighted: true; enabled: !dataController.running; onClicked: dataController.startAcquisition() }
                Button {
                    text: dataController.paused ? "继续" : "暂停"
                    enabled: dataController.running
                    onClicked: { if (dataController.paused) dataController.resumeAcquisition(); else dataController.pauseAcquisition() }
                }
                Button { text: "停止"; enabled: dataController.running; onClicked: dataController.stopAcquisition() }
                Item { Layout.fillWidth: true }
                Switch {
                    text: "测量记录"
                    checked: historyManager.sessionActive
                    enabled: historyManager.sessionActive || dataController.running
                    onToggled: { if (checked && !historyManager.sessionActive) historyManager.startSession(); else if (!checked && historyManager.sessionActive) historyManager.finishSession() }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18; Layout.rightMargin: 18
                columns: root.width < 850 ? 2 : 4
                columnSpacing: 12; rowSpacing: 12
                Metric { Layout.fillWidth: true; caption: "当前壁厚"; reading: dataController.thicknessText; accent: "#63dacf" }
                Metric { Layout.fillWidth: true; caption: "动态检出率"; reading: dataController.detectionRateText; accent: "#63b6ff" }
                Metric { Layout.fillWidth: true; caption: "当前信噪比"; reading: dataController.snrText; accent: "#edbd73" }
                Metric { Layout.fillWidth: true; caption: "管长 / 当前统计"; reading: dataController.pipeLengthText }
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18; Layout.rightMargin: 18
                columns: root.width < 1000 ? 1 : 2
                columnSpacing: 16; rowSpacing: 16
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 300
                    Layout.preferredHeight: 370
                    color: "#19212c"; radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 18; spacing: 16
                        Label { text: "管件概况"; color: root.ink; font.bold: true; font.pixelSize: 17 }
                        Detail { caption: "管件状态"; reading: pipePresenceState.phaseLabel }
                        Detail { caption: "平均壁厚"; reading: dataController.avgThicknessText }
                        Detail { caption: "最小壁厚"; reading: dataController.minThicknessText }
                        Detail { caption: "最大壁厚"; reading: dataController.maxThicknessText }
                        Detail { caption: "剖面状态"; reading: dataController.pipeProfileFinalized ? "已完成" : "采集 / 等待中" }
                        Detail { caption: "运动状态"; reading: pipePresenceState.motionLabel }
                        Detail { caption: "速度"; reading: isFinite(pipePresenceState.speedMmS) ? pipePresenceState.speedMmS.toFixed(1) + " mm/s" : "--" }
                        Item { Layout.fillHeight: true }
                        Label { text: "状态由雷达与测厚数据推定"; color: root.muted; font.pixelSize: 11 }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 720
                    Layout.preferredHeight: 370
                    color: "#19212c"; radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16
                        RowLayout {
                            Label { text: "轴向壁厚剖面"; color: root.ink; font.pixelSize: 17; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Label { text: "● 实测  /  管头 → 管尾"; color: "#63dacf"; font.pixelSize: 11 }
                        }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            AxialThicknessPlotItem { id: axial; anchors.fill: parent; anchors.leftMargin: 55; anchors.bottomMargin: 28; source: dataController }
                            Label { text: axial.yMax.toFixed(3); color: root.muted; font.pixelSize: 11; anchors.top: parent.top }
                            Label { text: axial.yMin.toFixed(3); color: root.muted; font.pixelSize: 11; anchors.bottom: parent.bottom; anchors.bottomMargin: 28 }
                            Label { text: "mm"; color: root.muted; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                            Label { text: axial.xMin.toFixed(0); color: root.muted; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 55 }
                            Label { text: axial.xMax.toFixed(0); color: root.muted; anchors.bottom: parent.bottom; anchors.right: parent.right }
                            Label { text: "轴向位置 / mm"; color: root.muted; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter }
                            Label { visible: axial.pointCount === 0; text: "等待有效轴向测量数据"; color: root.muted; anchors.centerIn: parent }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                implicitHeight: statusColumn.implicitHeight + 28
                color: "#19212c"; radius: 6
                ColumnLayout {
                    id: statusColumn
                    anchors.fill: parent; anchors.margins: 14
                    Label { text: "测量链状态"; color: root.ink; font.bold: true }
                    Label { text: "雷达：" + pipePresenceState.radarLinkLabel + "  ·  " + dataController.axialOriginStatus; color: root.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: dataController.detectionRateStateText + "  ·  " + dataController.tailFillStatus; color: root.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: dataController.radarSidecarStatus; color: root.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
            Item { implicitHeight: 8 }
        }
    }
    Dialog {
        id: experiment
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 1400)
        height: parent.height - 32
        title: "实验 · 采集配置与波形诊断"
        modal: true
        standardButtons: Dialog.Close
        contentItem: Loader {
            active: root.experimentLoaded
            source: "ExperimentDataPage.qml"
            visible: experiment.visible
        }
    }
    Dialog {
        id: acquisitionError
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "采集提示"
        modal: true
        standardButtons: Dialog.Ok
        property string message: ""
        Label { width: 420; text: acquisitionError.message; wrapMode: Text.Wrap }
    }
    Connections {
        target: dataController
        function onErrorOccurred(message) { acquisitionError.message = message; acquisitionError.open() }
    }
}
