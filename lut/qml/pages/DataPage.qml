import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import LST.Data 1.0
import "../components" as Comp

Rectangle {
    id: root
    objectName: "engineeringDataPage"
    color: "#111418"
    Material.theme: Material.Dark
    Material.accent: "#ee9b45"

    readonly property color ink: "#e8edf2"
    readonly property color muted: "#929da8"
    readonly property color blue: "#64b5f6"
    readonly property color amber: "#ee9b45"
    readonly property color green: "#64c89b"
    readonly property bool wide: width >= 980
    readonly property bool measuring: dataController.running && !dataController.paused
    readonly property bool validThickness: isFinite(dataController.thickness) && dataController.thickness > 0
    property bool experimentLoaded: false
    property date clock: new Date()

    component Panel: Rectangle {
        id: panel
        property string title
        property string annotation
        property color annotationColor: root.muted
        default property alias content: body.data
        color: "#1b1f25"
        border.color: "#343b43"
        radius: 5
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Rectangle { width: 3; height: 15; color: root.amber; radius: 1 }
                Label { text: panel.title; color: root.ink; font.pixelSize: 14; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: panel.annotation; color: panel.annotationColor; font.pixelSize: 10 }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#30363e" }
            Item { id: body; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
    component Action: Button {
        id: action
        property color tint: "#303740"
        implicitHeight: 34
        implicitWidth: Math.max(72, contentItem.implicitWidth + 24)
        hoverEnabled: true
        font.pixelSize: 12
        background: Rectangle {
            radius: 4
            color: action.down ? Qt.darker(action.tint, 1.25) : (action.hovered ? Qt.lighter(action.tint, 1.15) : action.tint)
            border.color: action.activeFocus ? root.amber : Qt.lighter(action.tint, 1.2)
            opacity: action.enabled ? 1 : 0.35
        }
        contentItem: Text {
            text: action.text
            color: action.enabled ? root.ink : "#67717b"
            font: action.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    component Field: RowLayout {
        property string caption
        property string reading
        property color valueColor: root.ink
        Layout.fillWidth: true
        Label { text: caption; color: root.muted; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Label {
            text: reading
            color: valueColor
            font.pixelSize: 11
            Layout.maximumWidth: Math.max(120, parent.width * 0.67)
            elide: Text.ElideRight
            ToolTip.visible: fieldHover.hovered
            ToolTip.text: text
            HoverHandler { id: fieldHover }
        }
    }
    component Stat: ColumnLayout {
        property string caption
        property string reading
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        spacing: 6
        Label { Layout.fillWidth: true; text: caption; color: root.muted; font.pixelSize: 10 }
        Label { Layout.fillWidth: true; text: reading; color: root.ink; font.pixelSize: 16; font.bold: true }
    }
    component Legend: RowLayout {
        property string caption
        property color tone: root.blue
        spacing: 5
        Rectangle { width: 7; height: 7; color: parent.tone; radius: 1 }
        Label { text: parent.caption; color: root.muted; font.pixelSize: 10 }
    }

    Timer { interval: 1000; repeat: true; running: root.visible; onTriggered: root.clock = new Date() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // Reference layout: compact status strip above a persistent command bar.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#1b1f25"
            border.color: root.measuring ? "#386d55" : "#343b43"
            radius: 5
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 16
                spacing: 14
                ColumnLayout {
                    spacing: 4
                    Label { text: "在线测厚"; color: root.ink; font.pixelSize: 19; font.bold: true }
                    Label { text: "MEASUREMENTS / 工程监视"; color: root.muted; font.pixelSize: 9; font.letterSpacing: 1 }
                }
                Item { Layout.fillWidth: true }
                Rectangle { width: 8; height: 8; radius: 4; color: root.measuring ? root.green : root.amber }
                Label {
                    text: !dataController.running ? "待机 · READY" : (dataController.paused ? "测量已暂停" : "正在测量 · LIVE")
                    color: root.measuring ? root.green : root.ink
                    font.pixelSize: 16
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    visible: root.width >= 700
                    spacing: 4
                    Label { text: Qt.formatDateTime(root.clock, "hh:mm:ss"); color: root.ink; font.pixelSize: 15; Layout.alignment: Qt.AlignRight }
                    Label { text: Qt.formatDateTime(root.clock, "yyyy-MM-dd"); color: root.muted; font.pixelSize: 10; Layout.alignment: Qt.AlignRight }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Action { objectName: "startMeasurement"; text: "开始测量"; tint: "#28523e"; enabled: !dataController.running; onClicked: dataController.startAcquisition() }
            Action {
                objectName: "pauseMeasurement"
                text: dataController.paused ? "继续测量" : "暂停"
                enabled: dataController.running
                onClicked: { if (dataController.paused) dataController.resumeAcquisition(); else dataController.pauseAcquisition() }
            }
            Action { objectName: "stopMeasurement"; text: "停止"; enabled: dataController.running; onClicked: dataController.stopAcquisition() }
            Rectangle { width: 1; height: 20; color: "#343b43"; Layout.leftMargin: 4; Layout.rightMargin: 4 }
            Action {
                objectName: "toggleRecording"
                text: historyManager.sessionActive ? "●  停止记录" : "开始记录"
                tint: historyManager.sessionActive ? "#5b3f27" : "#303740"
                enabled: historyManager.sessionActive || dataController.running
                onClicked: { if (historyManager.sessionActive) historyManager.finishSession(); else historyManager.startSession() }
            }
            Item { Layout.fillWidth: true }
            Label { visible: root.width > 800; text: "生产监视"; color: root.muted; font.pixelSize: 11 }
            Action { text: "状态详情"; onClicked: statusDetails.open() }
            Action { objectName: "openExperiment"; text: "实验"; tint: "#55412d"; onClicked: { root.experimentLoaded = true; experiment.open() } }
        }

        ScrollView {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            GridLayout {
                width: workspace.availableWidth
                columns: root.wide ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                readonly property real rowHeight: root.wide ? Math.max(270, (workspace.availableHeight - 12) / 2) : 290
                readonly property real leftWidth: root.wide ? Math.max(285, workspace.availableWidth * 0.30) : workspace.availableWidth

                Panel {
                    objectName: "pipeStatisticsPanel"
                    Layout.row: 0; Layout.column: 0
                    Layout.preferredWidth: parent.leftWidth
                    Layout.fillWidth: !root.wide
                    Layout.preferredHeight: parent.rowHeight
                    title: "管件统计"
                    annotation: dataController.pipeProfileFinalized ? "已完成" : "当前测量"
                    annotationColor: dataController.pipeProfileFinalized ? root.green : root.muted
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 2
                                Label { text: "当前壁厚"; color: root.muted; font.pixelSize: 11 }
                                Label { text: root.validThickness ? dataController.thickness.toFixed(3) : "—"; color: root.blue; font.pixelSize: 36; font.bold: true }
                            }
                            Label { text: "mm"; color: root.muted; font.pixelSize: 13; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 8 }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Stat { caption: "最小 / mm"; reading: isFinite(dataController.minThickness) && dataController.minThickness > 0 ? dataController.minThickness.toFixed(3) : "—" }
                            Stat { caption: "平均 / mm"; reading: isFinite(dataController.avgThickness) && dataController.avgThickness > 0 ? dataController.avgThickness.toFixed(3) : "—" }
                            Stat { caption: "最大 / mm"; reading: isFinite(dataController.maxThickness) && dataController.maxThickness > 0 ? dataController.maxThickness.toFixed(3) : "—" }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#30363e" }
                        Field { caption: "管长（雷达估计）"; reading: dataController.pipeLengthText }
                        Field { caption: "运动状态"; reading: pipePresenceState.motionLabel }
                        Field { caption: "瞬时速度"; reading: isFinite(pipePresenceState.speedMmS) ? pipePresenceState.speedMmS.toFixed(1) + " mm/s" : "—" }
                        Item { Layout.fillHeight: true }
                    }
                }

                Panel {
                    objectName: "thicknessTrendPanel"
                    Layout.row: root.wide ? 0 : 1; Layout.column: root.wide ? 1 : 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 700
                    Layout.preferredHeight: parent.rowHeight
                    title: "壁厚趋势"
                    annotation: "最近 1000 次测量"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        Comp.EngineeringTrendPlot { id: trend; Layout.fillWidth: true; Layout.fillHeight: true }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 18
                            Legend { caption: "实测壁厚" }
                            Legend { caption: "窗口均值"; tone: "#dcaa55" }
                            Label { text: "断线表示无效测量"; color: root.muted; font.pixelSize: 10 }
                        }
                    }
                }

                Panel {
                    objectName: "measurementQualityPanel"
                    Layout.row: root.wide ? 1 : 2; Layout.column: 0
                    Layout.preferredWidth: parent.leftWidth
                    Layout.fillWidth: !root.wide
                    Layout.preferredHeight: parent.rowHeight
                    title: "测量质量"
                    annotation: "检出率 ≠ 合格率"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 3
                                Label { text: "动态检出率"; color: root.muted; font.pixelSize: 11 }
                                Label { text: dataController.detectionRateText; color: root.green; font.pixelSize: 32; font.bold: true }
                            }
                            Item { Layout.fillWidth: true }
                            ColumnLayout {
                                spacing: 5
                                Label { text: "当前 SNR"; color: root.muted; font.pixelSize: 10 }
                                Label { text: isFinite(dataController.snr) && root.validThickness ? dataController.snrText : "— dB"; color: root.amber; font.pixelSize: 17; font.bold: true }
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0; to: 100
                            value: isFinite(dataController.detectionRate) ? dataController.detectionRate : 0
                            background: Rectangle { implicitHeight: 5; color: "#303740"; radius: 2 }
                            contentItem: Item { Rectangle { width: parent.width * parent.parent.visualPosition; height: parent.height; radius: 2; color: root.green } }
                        }
                        Field { caption: "当前结果"; reading: !dataController.running ? "采集未运行" : (dataController.paused ? "采集已暂停" : (root.validThickness ? "有效测量" : "当前帧无效")); valueColor: root.measuring && root.validThickness ? root.green : root.muted }
                        Field { caption: "管头厚度确认"; reading: dataController.laserHeadThicknessConfirmed ? "已确认" : "等待稳定厚度" }
                        Field { caption: "记录状态"; reading: historyManager.sessionActive ? "记录中 · " + historyManager.sessionDataCount + " 点" : "未记录"; valueColor: historyManager.sessionActive ? root.amber : root.muted }
                        Label {
                            Layout.fillWidth: true
                            text: "管件状态：" + pipePresenceState.phaseLabel
                            wrapMode: Text.Wrap
                            color: root.muted
                            font.pixelSize: 10
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Panel {
                    objectName: "axialProfilePanel"
                    Layout.row: root.wide ? 1 : 3; Layout.column: root.wide ? 1 : 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 700
                    Layout.preferredHeight: parent.rowHeight
                    title: "轴向壁厚剖面"
                    annotation: dataController.pipeProfileFinalized ? "管件完整剖面" : "管头 → 管尾"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        Comp.EngineeringPlotFrame {
                            id: axialFrame
                            Layout.fillWidth: true; Layout.fillHeight: true
                            xMin: axial ? axial.xMin : 0; xMax: axial ? axial.xMax : 1
                            yMin: axial ? axial.yMin : 0; yMax: axial ? axial.yMax : 1
                            hasData: axial && axial.pointCount > 0
                            emptyText: "等待厚度与轴向位置匹配"
                            AxialThicknessPlotItem {
                                id: axial
                                parent: axialFrame.viewport
                                anchors.fill: parent
                                source: dataController
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 18
                            Legend { caption: "实测" }
                            Legend { caption: "管尾位置估计"; tone: "#f58d25" }
                            Legend { caption: "缺口插值"; tone: "#9e9e9e" }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Rectangle { width: 6; height: 6; radius: 3; color: pipePresenceState.radarPresent ? root.green : root.muted }
            Label { text: "雷达：" + pipePresenceState.radarLinkLabel; color: root.muted; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: historyManager.sessionActive ? "● 测量记录中" : "○ 测量未记录"; color: historyManager.sessionActive ? root.amber : root.muted; font.pixelSize: 10 }
        }
    }

    Dialog {
        id: experiment
        objectName: "experimentDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 1400)
        height: parent.height - 32
        title: "实验 · 采集配置与波形诊断"
        modal: true
        standardButtons: Dialog.Close
        contentItem: Loader { active: root.experimentLoaded; source: "ExperimentDataPage.qml"; visible: experiment.visible }
    }
    Dialog {
        id: statusDetails
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 620)
        title: "测量链状态"
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            width: parent.width
            spacing: 14
            Repeater {
                model: [dataController.detectionRateStateText, dataController.axialOriginStatus,
                        dataController.radarSidecarStatus, dataController.tailFillStatus]
                Label { required property string modelData; text: modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.ink }
            }
        }
    }
    Dialog {
        id: acquisitionError
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 480)
        title: "采集提示"
        modal: true
        standardButtons: Dialog.Ok
        property string message: ""
        Label { width: parent.width; text: acquisitionError.message; wrapMode: Text.Wrap }
    }
    Connections {
        target: dataController
        function onThicknessHistoryUpdated(thickness) { trend.appendThickness(thickness) }
        function onThicknessHistoryBackfilled(framesAgo, thickness) { trend.replaceRecentThickness(framesAgo, thickness) }
        function onMeasurementDisplayReset() { trend.clear() }
        function onErrorOccurred(message) { acquisitionError.message = message; acquisitionError.open() }
    }
}
