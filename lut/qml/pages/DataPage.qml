import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Dialogs
import LST.Data 1.0
import "../components" as Comp

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true

    // One display-reset event covers both normal waveforms/history and the
    // GPU axial curve (the latter also listens to axialProfileReset in C++).
    Connections {
        target: dataController
        function onMeasurementDisplayReset() {
            if (thicknessPlot) thicknessPlot.clear()
            if (graphItem) graphItem.clear()
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 8

        // Title
        Label {
            text: "数据采集"
            font.pixelSize: 24
            font.bold: true
            color: "#3F51B5"
            Layout.leftMargin: 16
            Layout.topMargin: 12
        }

        // Keep only the measurement recording switch.
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 6
            Material.elevation: 2

            Switch {
                text: historyManager.sessionActive ? "停止记录" : "开始记录"
                checked: historyManager.sessionActive
                enabled: historyManager.sessionActive || (dataController && dataController.running)
                onToggled: {
                    if (checked && !historyManager.sessionActive)
                        historyManager.startSession()
                    else if (!checked && historyManager.sessionActive)
                        historyManager.finishSession()
                }
            }
        }

        // Live axial curve is independent; this only saves its valid plotted data.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 10
            CheckBox {
                text: "保存轴向壁厚曲线数据（CSV）"
                checked: dataController ? dataController.radarPositionSidecarEnabled : false
                enabled: dataController && !dataController.running
                onToggled: {
                    if (dataController && !dataController.running)
                        dataController.setRadarPositionSidecarEnabled(checked)
                }
            }
        }

        // LB1/LB2 are inferred from effective radar measurement output; no physical DI is wired.
        Pane {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 8
            Material.elevation: 2

            ColumnLayout {
                width: parent.width
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "钢管实时状态"; font.pixelSize: 14; font.bold: true; color: "#3F51B5" }
                    Label {
                        text: pipePresenceState.phaseLabel
                        font.pixelSize: 14
                        font.bold: true
                        color: pipePresenceState.radarPresent ? "#2E7D32" : "#795548"
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    Label { text: "LB1（推定）：" + pipePresenceState.lb1Label; font.pixelSize: 12 }
                    Label { text: "LB2（推定）：" + pipePresenceState.lb2Label; font.pixelSize: 12 }
                    Label { text: "雷达：" + pipePresenceState.radarLabel; font.pixelSize: 12 }
                    Label {
                        text: "激发光测点：" + (dataController && dataController.laserHeadThicknessConfirmed
                              ? "三帧厚度已确认" : "等待稳定厚度")
                        font.pixelSize: 12
                        color: dataController && dataController.laserHeadThicknessConfirmed
                               ? "#2E7D32" : "#795548"
                    }
                    Label { text: "运动：" + pipePresenceState.motionLabel; font.pixelSize: 12 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    Label {
                        text: "瞬时速度：" + (isFinite(pipePresenceState.speedMmS)
                              ? pipePresenceState.speedMmS.toFixed(2) + " mm/s" : "--")
                        font.pixelSize: 12
                    }
                    Label {
                        text: "距激光：" + (isFinite(pipePresenceState.distanceToLaserMm)
                              ? pipePresenceState.distanceToLaserMm.toFixed(2) + " mm（估计）" : "--")
                        font.pixelSize: 12
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 14
                    Label {
                        text: "UDP接收：" + pipePresenceState.receivedPacketCount + " 包"
                        font.pixelSize: 12
                        font.bold: true
                        color: pipePresenceState.packetAgeMs >= 0 && pipePresenceState.packetAgeMs <= 100
                               ? "#2E7D32" : "#C62828"
                    }
                    Label {
                        text: "瞬时速度原始值：" + (pipePresenceState.receivedPacketCount > 0
                              ? pipePresenceState.lastRawInstantSpeed : "--")
                        font.pixelSize: 12
                    }
                    Label {
                        text: "累计长度原始值：" + (pipePresenceState.receivedPacketCount > 0
                              ? pipePresenceState.lastRawTotalLength : "--")
                        font.pixelSize: 12
                    }
                    Label {
                        text: "Good Reading：" + (pipePresenceState.receivedPacketCount > 0
                              ? pipePresenceState.lastGoodReading : "--")
                        font.pixelSize: 12
                    }
                    Label {
                        text: "最新报文：" + (pipePresenceState.packetAgeMs >= 0
                              ? pipePresenceState.packetAgeMs + " ms前" : "尚未收到")
                        font.pixelSize: 12
                    }
                }
            }
        }

        // Main content area: left panel + right panel
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            spacing: 12

            // Left Panel - Parameter Controls
            Pane {
                Layout.preferredWidth: 270
                Layout.minimumWidth: 250
                Layout.fillHeight: true
                padding: 10
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label {
                        text: "触发参数"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#3F51B5"
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "触发源"
                        editorType: "combo"
                        editorWidth: 140
                        comboOptions: dataController ? dataController.triggerSourceList : []
                        enabled: dataController && dataController.triggerSupported
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setTriggerSource(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "触发类型"
                        editorType: "combo"
                        editorWidth: 140
                        comboOptions: dataController ? dataController.triggerEdgeList : []
                        enabled: dataController && dataController.triggerSupported
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setTriggerEdge(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "触发延迟"
                        editorWidth: 140
                        editorType: "spinbox"
                        intValue: 240
                        intFrom: 0
                        intTo: 10000
                        enabled: dataController && dataController.triggerSupported
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setDelayCount(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "触发电平"
                        unit: "V"
                        editorType: "doublefield"
                        editorWidth: 120
                        doubleValue: 2.0
                        doubleFrom: 0.0
                        doubleTo: 10.0
                        decimals: 1
                        enabled: dataController && dataController.triggerSupported
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setTriggerLevel(value)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#E0E0E0"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    Label {
                        text: "检测参数"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#3F51B5"
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "寻峰模式"
                        editorType: "combo"
                        editorWidth: 140
                        comboOptions: ["auto", "peak", "valley"]
                        enabled: dataController != null
                        onValueCommitted: function(value) {
                            if (dataController) {
                                var modes = ["auto", "peak", "valley"]
                                dataController.setDetectMode(modes[value])
                            }
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "测厚回波"
                        editorType: "combo"
                        editorWidth: 140
                        comboOptions: ["T1-only", "T2-only"]
                        comboIndex: dataController && dataController.thicknessEchoMode === "T2-only" ? 1 : 0
                        enabled: dataController != null && !dataController.running
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setThicknessEchoMode(value === 1 ? "T2-only" : "T1-only")
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "采用高精度检测"
                        editorType: "switch"
                        editorWidth: 140
                        switchValue: dataController ? dataController.t2HighPrecisionDetectionEnabled : true
                        visible: dataController && dataController.thicknessEchoMode === "T2-only"
                        enabled: dataController != null && !dataController.running
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setT2HighPrecisionDetectionEnabled(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "参考厚度"
                        unit: "mm"
                        editorType: "doublefield"
                        editorWidth: 120
                        doubleValue: 6.5
                        doubleFrom: 0.0
                        doubleTo: 100.0
                        decimals: 1
                        enabled: dataController != null
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setThicknessStd(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "误差范围"
                        unit: "mm"
                        editorType: "doublefield"
                        editorWidth: 120
                        doubleValue: 0.8
                        doubleFrom: 0.0
                        doubleTo: 10.0
                        decimals: 1
                        enabled: dataController != null
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setThicknessTolerance(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "峰    宽"
                        editorType: "spinbox"
                        editorWidth: 140
                        intValue: 9
                        intFrom: 1
                        intTo: 100
                        enabled: dataController != null
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setStructLen(value)
                        }
                    }

                    Comp.InlineParamRow {
                        Layout.fillWidth: true
                        label: "最小间距"
                        editorType: "spinbox"
                        editorWidth: 140
                        intValue: 1
                        intFrom: 1
                        intTo: 100
                        enabled: dataController != null
                        onValueCommitted: function(value) {
                            if (dataController) dataController.setMinWidth(value)
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "核心测厚参数..."
                        font.pixelSize: 12
                        enabled: dataController != null && !dataController.running
                        ToolTip.visible: hovered
                        ToolTip.text: dataController && dataController.running
                                      ? "请先停止采集，再修改跨帧跟踪与波包拟合参数"
                                      : "打开迁移自LSTAlgorithmTest的高级测厚参数"
                        onClicked: thicknessAlgorithmDialog.open()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#E0E0E0"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }
                    Label {
                        text: "数据汇总"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#3F51B5"
                    }



                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "最大值:"
                            font.pixelSize: 14
                            color: "#757575"
                            Layout.preferredWidth: 48
                        }
                        Label {
                            text: dataController ? dataController.maxThicknessText : "-- mm"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#E53935"
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "最小值:"
                            font.pixelSize: 14
                            color: "#757575"
                            Layout.preferredWidth: 48
                        }
                        Label {
                            text: dataController ? dataController.minThicknessText : "-- mm"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#43A047"
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "平均值:"
                            font.pixelSize: 14
                            color: "#757575"
                            Layout.preferredWidth: 48
                        }
                        Label {
                            text: dataController ? dataController.avgThicknessText : "-- mm"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#3F51B5"
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "总长度:"
                            font.pixelSize: 14
                            color: "#757575"
                            Layout.preferredWidth: 88
                        }
                        Label {
                            text: dataController ? dataController.pipeLengthText : "-- m"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#009688"
                            Layout.fillWidth: true
                            ToolTip.visible: lengthHover.hovered
                            //ToolTip.text: "由候选轴向终点计算，尚未进行实物长度标定"
                            HoverHandler { id: lengthHover }
                        }
                    }




                    Item { Layout.fillHeight: true }
                }
            }

            // Right Panel - Main Display Area
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                // Device/Profile bar
                Pane {
                    Layout.fillWidth: true
                    padding: 8
                    Material.elevation: 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8

                        Label { text: "主机:"; font.pixelSize: 12; color: "#424242" }
                        ComboBox {
                            id: cmbHostName
                            Layout.preferredWidth: 130
                            enabled: dataController != null
                            model: ["Local"]
                            editable: true
                            onEditTextChanged: {
                                if (dataController) dataController.setHostName(editText)
                            }
                        }

                        Item { Layout.preferredWidth: 8 }

                        Label { text: "设备:"; font.pixelSize: 12; color: "#424242" }
                        ComboBox {
                            id: cmbDevice
                            Layout.preferredWidth: 200
                            enabled: dataController != null
                            model: dataController ? dataController.deviceList : []
                            onCurrentIndexChanged: {
                                if (dataController && currentIndex >= 0)
                                    dataController.selectDevice(currentIndex)
                            }
                        }

                        Item { Layout.preferredWidth: 8 }

                        Label { text: "XML 地址:"; font.pixelSize: 12; color: "#424242" }
                        TextField {
                            id: txtProfilePath
                            Layout.fillWidth: true
                            text: "/qml/pages/profile.xml"
                            font.pixelSize: 12
                            enabled: dataController != null
                        }

                        Button {
                            text: "选择"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 12
                            Material.background: "#3F51B5"
                            Material.foreground: "white"
                            enabled: dataController != null
                            onClicked: fileDialog.open()
                        }
                    }
                }

                // Filter bar
                Pane {
                    Layout.fillWidth: true
                    padding: 8
                    Material.elevation: 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8

                        Label { text: "低频截止(MHz)"; font.pixelSize: 12; color: "#424242" }
                        TextField {
                            id: edtLowFreq
                            Layout.preferredWidth: 55
                            text: "3"
                            font.pixelSize: 12
                        }
                        Item { Layout.preferredWidth: 4 }
                        Label { text: "高频截止(MHz)"; font.pixelSize: 12; color: "#424242" }
                        TextField {
                            id: edtHighFreq
                            Layout.preferredWidth: 55
                            text: "30"
                            font.pixelSize: 12
                        }
                        Button {
                            text: "滤波"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 12
                            Material.background: "#3F51B5"
                            Material.foreground: "white"
                            onClicked: {
                                if (dataController) {
                                    dataController.applyFilter(
                                        parseFloat(edtLowFreq.text) || 3,
                                        parseFloat(edtHighFreq.text) || 30
                                    )
                                }
                            }
                        }
                        CheckBox {
                            id: chkShowOriginal
                            text: "原始波形"
                            font.pixelSize: 12
                            checked: dataController ? dataController.showOriginal : true
                            onCheckedChanged: {
                                if (dataController) dataController.setShowOriginal(checked)
                            }
                        }
                        Item { Layout.preferredWidth: 8 }
                        Label {
                            text: "波形刷新"
                            font.pixelSize: 12
                            color: "#424242"
                        }
                        ComboBox {
                            id: cmbWaveformRefresh
                            Layout.preferredWidth: 145
                            model: ["每1个波形", "每10个波形", "每100个波形"]
                            currentIndex: {
                                if (!dataController) return 0
                                if (dataController.waveformRefreshInterval === 100) return 2
                                if (dataController.waveformRefreshInterval === 10) return 1
                                return 0
                            }
                            onActivated: {
                                if (!dataController) return
                                var intervals = [1, 10, 100]
                                dataController.setWaveformRefreshInterval(intervals[currentIndex])
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "仅降低信号波形和峰值标记的界面刷新频率；100 Hz测厚和厚度历史仍逐帧更新"
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                // Y Range and Control bar
                Pane {
                    Layout.fillWidth: true
                    padding: 8
                    Material.elevation: 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8

                        Label { text: "最低值(V)"; font.pixelSize: 12; color: "#424242" }
                        TextField {
                            id: edtYMin
                            Layout.preferredWidth: 55
                            text: "-5"
                            font.pixelSize: 12
                            enabled: !chkAutoScale.checked
                        }
                        Item { Layout.preferredWidth: 12 }
                        Label { text: "最高值(V)"; font.pixelSize: 12; color: "#424242" }
                        TextField {
                            id: edtYMax
                            Layout.preferredWidth: 55
                            text: "5"
                            font.pixelSize: 12
                            enabled: !chkAutoScale.checked
                        }
                        Button {
                            text: "应用"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 12
                            Material.background: "#3F51B5"
                            Material.foreground: "white"
                            enabled: !chkAutoScale.checked
                            onClicked: {
                                if (graphItem) {
                                    graphItem.setYAxisRange(
                                        parseFloat(edtYMin.text) || -5,
                                        parseFloat(edtYMax.text) || 5
                                    )
                                }
                            }
                        }
                        CheckBox {
                            id: chkAutoScale
                            text: "自动缩放"
                            font.pixelSize: 12
                            checked: true
                            onCheckedChanged: {
                                if (graphItem) graphItem.setAutoScale(checked)
                            }
                        }
                        Item { Layout.fillWidth: true }

                        Button {
                            id: btnStart
                            text: "开始"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 13
                            font.bold: true
                            Material.background: "#4CAF50"
                            Material.foreground: "white"
                            enabled: dataController && !dataController.running
                            onClicked: {
                                if (dataController) dataController.startAcquisition()
                            }
                        }
                        Button {
                            id: btnPause
                            text: dataController && dataController.paused ? "继续" : "暂停"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 13
                            font.bold: true
                            Material.background: "#2196F3"
                            Material.foreground: "white"
                            enabled: dataController && dataController.running
                            onClicked: {
                                if (!dataController) return
                                if (dataController.paused)
                                    dataController.resumeAcquisition()
                                else
                                    dataController.pauseAcquisition()
                            }
                        }
                        Button {
                            id: btnStop
                            text: "停止"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 13
                            font.bold: true
                            Material.background: "#757575"
                            Material.foreground: "white"
                            enabled: dataController && dataController.running
                            onClicked: {
                                if (dataController) dataController.stopAcquisition()
                            }
                        }
                    }
                }

                // Waveform Graph
                Pane {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    padding: 0
                    Material.elevation: 2

                    SimpleGraphItem {
                        id: graphItem
                        anchors.fill: parent
                        anchors.margins: 1
                    }
                }

                // Thickness display
                Pane {
                    Layout.fillWidth: true
                    padding: 8
                    Material.elevation: 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 16

                        Label {
                            text: "厚度:"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#424242"
                        }
                        Label {
                            id: lblThickness
                            text: dataController ? dataController.thicknessText : "0 mm"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#3F51B5"
                        }
                        Item { Layout.preferredWidth: 24 }
                        Label {
                            text: "动态检出率："
                            font.pixelSize: 14
                            font.bold: true
                            color: "#424242"
                        }
                        Label {
                            id: lblDetectionRate
                            text: dataController ? dataController.detectionRateText : "--"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#009688"
                        }
                        Item { Layout.preferredWidth: 16 }
                        Label {
                            text: "信噪比:"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#424242"
                        }
                        Label {
                            id: lblSnr
                            text: dataController ? dataController.snrText : "0 dB"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#FF9800"
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                // Thickness History Plot
                Pane {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    padding: 0
                    Material.elevation: 2

                    ThicknessHistoryItem {
                        id: thicknessPlot
                        anchors.fill: parent
                        anchors.margins: 1
                    }
                }

                // Keep the distance control outside the graph so both plots
                // have the same 300px drawing area and black background.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: "管头轴向壁厚曲线"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#3F51B5"
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: "雷达至激光距离（mm）："
                        font.pixelSize: 12
                    }
                    SpinBox {
                        id: radarToLaserDistanceInput
                        from: dataController ? dataController.minRadarToLaserInputMm : 0
                        to: dataController ? dataController.maxRadarToLaserInputMm : 0
                        stepSize: 1
                        editable: true
                        value: dataController ? Math.round(dataController.radarToLaserDistanceMm) : 0
                        enabled: dataController && !dataController.running
                        Layout.preferredWidth: 140
                        onValueModified: {
                            if (dataController && !dataController.running)
                                dataController.setRadarToLaserDistanceMm(value)
                        }
                    }
                }

                Pane {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    padding: 0
                    Material.elevation: 2

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        color: "black"
                        clip: true

                        Rectangle {
                            x: 57
                            y: 10
                            width: 1
                            height: Math.max(1, parent.height - 47)
                            color: "white"
                        }
                        Rectangle {
                            x: 57
                            y: Math.max(10, parent.height - 37)
                            width: Math.max(1, parent.width - 79)
                            height: 1
                            color: "white"
                        }
                        // The 100Hz curve is rendered on the scene graph thread
                        // and its geometry is updated at about 10Hz.
                        AxialThicknessPlotItem {
                            id: axialThicknessPlot
                            anchors.fill: parent
                            anchors.leftMargin: 58
                            anchors.rightMargin: 22
                            anchors.topMargin: 11
                            anchors.bottomMargin: 38
                            source: dataController
                        }
                        // The final per-pipe mean is an overlay, never a real
                        // measurement or an input to the ultrasound algorithm.
                        Item {
                            id: axialMeanGuide
                            visible: dataController && dataController.pipeProfileFinalized &&
                                     dataController.pipeSummaryValid &&
                                     axialThicknessPlot.yMax > axialThicknessPlot.yMin
                            x: 58
                            width: Math.max(0, parent.width - 80)
                            height: 1
                            y: axialThicknessPlot.y + axialThicknessPlot.height *
                               (axialThicknessPlot.yMax - (dataController ? dataController.pipeAverageThicknessMm : 0)) /
                               Math.max(0.001, axialThicknessPlot.yMax - axialThicknessPlot.yMin)
                            Repeater {
                                model: axialMeanGuide.visible ? Math.ceil(axialMeanGuide.width / 12) : 0
                                Rectangle {
                                    x: index * 12
                                    width: 7
                                    height: 1
                                    color: "#FFEB3B"
                                }
                            }
                        }
                        Label {
                            visible: axialMeanGuide.visible
                            x: 0
                            width: 55
                            horizontalAlignment: Text.AlignRight
                            y: axialMeanGuide.y - 9
                            text: "均" + (dataController ? dataController.pipeAverageThicknessMm.toFixed(2) : "--")
                            font.pixelSize: 11
                            font.bold: true
                            color: "#FFEB3B"
                        }
                        Label {
                            x: 0
                            y: 8
                            text: axialThicknessPlot.yMax.toFixed(3) + " mm"
                            font.pixelSize: 11
                            color: "white"
                        }
                        Label {
                            x: 0
                            y: Math.max(8, parent.height - 52)
                            text: axialThicknessPlot.yMin.toFixed(3) + " mm"
                            font.pixelSize: 11
                            color: "white"
                        }
                        Label {
                            x: 54
                            y: parent.height - 29
                            text: axialThicknessPlot.xMin.toFixed(1)
                            font.pixelSize: 11
                            color: "white"
                        }
                        Label {
                            x: parent.width - 65
                            y: parent.height - 29
                            text: axialThicknessPlot.xMax.toFixed(1)
                            font.pixelSize: 11
                            color: "white"
                        }
                        Label {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "轴向位置 / mm"
                            font.pixelSize: 12
                            color: "white"
                        }
                        Label {
                            visible: axialThicknessPlot.pointCount === 0
                            anchors.centerIn: parent
                            text: "暂无轴向壁厚数据"
                            font.pixelSize: 14
                            color: "#9E9E9E"
                        }
                    }
                }

                // Bottom action bar
                Pane {
                    Layout.fillWidth: true
                    padding: 8
                    Material.elevation: 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 12

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "重置"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 12
                            Material.background: "#3F51B5"
                            Material.foreground: "white"
                            enabled: dataController != null
                            onClicked: {
                                if (dataController) dataController.resetDetectionRate()
                            }
                        }

                        CheckBox {
                            id: chkSavePerFrame
                            text: "波形保存"
                            font.pixelSize: 12
                            enabled: dataController != null
                            onCheckedChanged: {
                                if (dataController) dataController.setSavePerFrame(checked)
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: thicknessAlgorithmDialog
        title: "核心测厚参数"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 760)
        height: Math.min(root.height - 40, 760)
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        // 以下默认值逐项对齐 LSTAlgorithmTest/AlgorithmTestPage.qml。
        property var protectionParameters: [
            { key: "filterOrder", label: "FIR阶数", type: "int", defaultValue: 50, from: 1, to: 1000 },
            { key: "impulseMadMultiplier", label: "Raw差分 / σ", type: "double", defaultValue: 20, from: 0.1, to: 1000, decimals: 1 },
            { key: "impulseMinJumpV", label: "最小脉冲跳变", unit: "V", type: "double", defaultValue: 1, from: 0.000001, to: 100, decimals: 3 },
            { key: "impulseIsolationRatio", label: "Raw局部孤立度倍数", type: "double", defaultValue: 20, from: 5, to: 100, decimals: 1 },
            { key: "impulseGuardSamples", label: "脉冲额外保护点", type: "int", defaultValue: 8, from: 0, to: 1000 },
            { key: "energyWindowSamples", label: "局部能量窗", unit: "点", type: "int", defaultValue: 7, from: 3, to: 255, step: 2 },
            { key: "minLocalSnrDb", label: "最低局部SNR", unit: "dB", type: "double", defaultValue: 7, from: 0.1, to: 100, decimals: 1 },
            { key: "lowSnrSoftMinDb", label: "LOCK低SNR软下限", unit: "dB", type: "double", defaultValue: 6, from: 0.1, to: 30, decimals: 1 },
            { key: "lowSnrSoftMinTrackingCorrelation", label: "低SNR最低模板相关", type: "double", defaultValue: 0.90, from: 0.1, to: 1, decimals: 2 },
            { key: "lowSnrSoftMaxResidualSamples", label: "低SNR最大预测残差", unit: "点", type: "double", defaultValue: 2, from: 0.1, to: 20, decimals: 1 },
            { key: "lowSnrSoftMinFitR2", label: "低SNR最低拟合R²", type: "double", defaultValue: 0.95, from: 0.1, to: 0.9999, decimals: 2 },
            { key: "noiseMadRejectMultiplier", label: "Noise dB-MAD剔除倍数", type: "double", defaultValue: 3, from: 1, to: 10, decimals: 2 },
            { key: "noiseEwmaAlphaUp", label: "Noise EWMA快速上升α", type: "double", defaultValue: 0.30, from: 0.01, to: 1, decimals: 2 },
            { key: "noiseEwmaAlphaDown", label: "Noise EWMA慢速下降α", type: "double", defaultValue: 0.15, from: 0.001, to: 1, decimals: 3 },
            { key: "minCandidateRatio", label: "第一/第二显著性比", type: "double", defaultValue: 1.2, from: 1.01, to: 100, decimals: 2 },
            { key: "candidateSeparationSamples", label: "独立波包间距", unit: "点", type: "int", defaultValue: 12, from: 1, to: 1000 },
            { key: "predictionHistorySize", label: "有效峰历史长度", type: "int", defaultValue: 5, from: 1, to: 32 },
            { key: "predictionMinValid", label: "建门所需有效峰", type: "int", defaultValue: 3, from: 1, to: 32 },
            { key: "predictionHalfWidth", label: "预测半窗", unit: "点", type: "int", defaultValue: 16, from: 1, to: 1000 },
            { key: "predictionMaxMisses", label: "连续失检进入LOST帧数", type: "int", defaultValue: 3, from: 1, to: 100 },
            { key: "trackingResidualAcceptSamples", label: "LOCK正常残差", unit: "点", type: "double", defaultValue: 6, from: 0.5, to: 100, decimals: 1 },
            { key: "trackingResidualRejectSamples", label: "严重跳变残差", unit: "点", type: "double", defaultValue: 9, from: 0.5, to: 200, decimals: 1 },
            { key: "reacquireConfirmFrames", label: "重捕获连续确认帧", type: "int", defaultValue: 2, from: 2, to: 10 },
            { key: "reacquireConsistencySamples", label: "重捕获位置一致", unit: "点", type: "double", defaultValue: 5, from: 0.5, to: 100, decimals: 1 }
        ]

        property var packetParameters: [
            { key: "longitudinalVelocityMps", label: "纵波声速", unit: "m/s", type: "double", defaultValue: 6459.7, from: 1, to: 20000, decimals: 1 },
            { key: "echoMinCorrelation", label: "辅助回波相关门限", type: "double", defaultValue: 0.70, from: 0.1, to: 1, decimals: 2 },
            { key: "echoMinSnrDb", label: "辅助回波最低SNR", unit: "dB", type: "double", defaultValue: 3, from: 0.1, to: 30, decimals: 1 },
            { key: "echoMinRelativeEnergy", label: "辅助回波最小相对能量", type: "double", defaultValue: 0.03, from: 0.001, to: 0.5, decimals: 3 },
            { key: "echoPeriodTolerance", label: "回波间隔相对容差", type: "double", defaultValue: 0.20, from: 0.01, to: 0.4, decimals: 2 },
            { key: "packetMinWidth", label: "最小波包宽", unit: "点", type: "int", defaultValue: 8, from: 5, to: 100 },
            { key: "packetMaxWidth", label: "最大波包宽", unit: "点", type: "int", defaultValue: 50, from: 6, to: 250 },
            { key: "packetMergeGap", label: "能量片段合并间隙", type: "int", defaultValue: 4, from: 0, to: 20 },
            { key: "packetEnergyFraction", label: "波包相对能量边界", type: "double", defaultValue: 0.05, from: 0.001, to: 0.5, decimals: 3 },
            { key: "lockedPacketMaxCoreWidth", label: "LOCK宽核心最大宽", unit: "点", type: "int", defaultValue: 60, from: 6, to: 250 },
            { key: "lockedBroadMinTrackingCorrelation", label: "宽包恢复最低相关", type: "double", defaultValue: 0.80, from: 0.1, to: 1, decimals: 2 },
            { key: "lockedBroadEnergyPeakToleranceSamples", label: "宽包能量峰/ToF容差", unit: "点", type: "double", defaultValue: 7, from: 0.5, to: 50, decimals: 1 },
            { key: "fitFootFraction", label: "拟合脚底比例", type: "double", defaultValue: 0.10, from: 0.01, to: 0.8, decimals: 2 },
            { key: "fitFootConfirmSamples", label: "脚底连续确认", unit: "点", type: "int", defaultValue: 3, from: 1, to: 20 },
            { key: "fitFootSmoothRadius", label: "脚底平滑半径", unit: "点", type: "int", defaultValue: 1, from: 0, to: 8 },
            { key: "fitCenterHalfRange", label: "拟合中心微调±", unit: "点", type: "double", defaultValue: 0.75, from: 0.01, to: 10, decimals: 2 },
            { key: "fitAmplitudeMinRatio", label: "拟合幅值下限/峰值比", type: "double", defaultValue: 0.75, from: 0.05, to: 3, decimals: 2 },
            { key: "fitAmplitudeMaxRatio", label: "拟合幅值上限/峰值比", type: "double", defaultValue: 1.50, from: 0.05, to: 5, decimals: 2 },
            { key: "fitMinR2", label: "等效单脉冲最低质量", type: "double", defaultValue: 0.50, from: 0.1, to: 0.9999, decimals: 2 },
            { key: "firstEchoPhaseRefinementAlpha", label: "首波Raw相位微调α（0关闭）", type: "double", defaultValue: 0.13, from: 0, to: 1, decimals: 2 },
            { key: "adaptiveWidthHistorySize", label: "自适应脉宽历史", unit: "帧", type: "int", defaultValue: 21, from: 5, to: 101 },
            { key: "adaptiveWidthMinHistory", label: "脉宽模型最少历史", unit: "帧", type: "int", defaultValue: 7, from: 3, to: 101 },
            { key: "adaptiveWidthMadMultiplier", label: "脉宽MAD倍数", type: "double", defaultValue: 4, from: 1, to: 12, decimals: 1 },
            { key: "adaptiveWidthMinToleranceSamples", label: "脉宽最小动态容差", unit: "点", type: "double", defaultValue: 4.5, from: 0.5, to: 20, decimals: 1 },
            { key: "adaptiveWidthOutlierMultiplier", label: "脉宽异常倍数", type: "double", defaultValue: 2.5, from: 1.1, to: 5, decimals: 1 }
        ]

        property var trackingParameters: [
            { key: "trackingMinCorrelation", label: "连续帧最小相关", type: "double", defaultValue: 0.65, from: 0.1, to: 1, decimals: 2 },
            { key: "trackingCorrelationMargin", label: "相关不唯一判定差", type: "double", defaultValue: 0.10, from: 0.001, to: 1, decimals: 3 },
            { key: "trackingEmaTemplateAlpha", label: "EMA模板更新α", type: "double", defaultValue: 0.15, from: 0.01, to: 1, decimals: 2 },
            { key: "trackingShapeTemplateAlpha", label: "形态模板更新α", type: "double", defaultValue: 0.30, from: 0.01, to: 1, decimals: 2 },
            { key: "trackingMultiTemplateAgreementSamples", label: "多模板ToF一致诊断", unit: "点", type: "double", defaultValue: 2, from: 0.25, to: 10, decimals: 1 },
            { key: "trackingBackupTemplateMaxResidualSamples", label: "后备模板最大预测残差", unit: "点", type: "double", defaultValue: 5, from: 0.25, to: 6, decimals: 1 },
            { key: "trackingBackupRecentBlendAlpha", label: "CONFIRMED模板更新α", type: "double", defaultValue: 0.25, from: 0.01, to: 1, decimals: 2 },
            { key: "trackingSoftCorrelation", label: "软恢复最低相关", type: "double", defaultValue: 0.53, from: 0.1, to: 1, decimals: 2 },
            { key: "trackingSoftFitPredictionToleranceSamples", label: "软恢复拟合/预测容差", unit: "点", type: "double", defaultValue: 3, from: 0.1, to: 20, decimals: 1 },
            { key: "trackingSoftMinFitR2", label: "软恢复最低拟合R²", type: "double", defaultValue: 0.80, from: 0.1, to: 0.9999, decimals: 2 },
            { key: "trackingSoftMinMaxSnrDb", label: "软恢复最低MaxSNR", unit: "dB", type: "double", defaultValue: 15, from: 0.1, to: 60, decimals: 1 }
        ]

        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, thicknessAlgorithmDialog.width - 48)
                spacing: 10

                GroupBox {
                    title: "防误检、SNR与预测门控"
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: thicknessAlgorithmDialog.protectionParameters
                            delegate: Comp.InlineParamRow {
                                Layout.fillWidth: true
                                label: modelData.label
                                unit: modelData.unit || ""
                                editorType: modelData.type === "int" ? "spinbox" : "doublefield"
                                editorWidth: 210
                                intValue: modelData.defaultValue
                                intFrom: modelData["from"]
                                intTo: modelData["to"]
                                intStep: modelData.step || 1
                                doubleValue: modelData.defaultValue
                                doubleFrom: modelData["from"]
                                doubleTo: modelData["to"]
                                decimals: modelData.decimals || 2
                                enabled: dataController != null && !dataController.running
                                onValueCommitted: function(value) {
                                    if (dataController)
                                        dataController.setThicknessAlgorithmParameter(modelData.key, value)
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    title: "纵波判据、波包与拟合"
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: thicknessAlgorithmDialog.packetParameters
                            delegate: Comp.InlineParamRow {
                                Layout.fillWidth: true
                                label: modelData.label
                                unit: modelData.unit || ""
                                editorType: modelData.type === "int" ? "spinbox" : "doublefield"
                                editorWidth: 210
                                intValue: modelData.defaultValue
                                intFrom: modelData["from"]
                                intTo: modelData["to"]
                                intStep: modelData.step || 1
                                doubleValue: modelData.defaultValue
                                doubleFrom: modelData["from"]
                                doubleTo: modelData["to"]
                                decimals: modelData.decimals || 2
                                enabled: dataController != null && !dataController.running
                                onValueCommitted: function(value) {
                                    if (dataController)
                                        dataController.setThicknessAlgorithmParameter(modelData.key, value)
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    title: "多模板跟踪与软恢复"
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: thicknessAlgorithmDialog.trackingParameters
                            delegate: Comp.InlineParamRow {
                                Layout.fillWidth: true
                                label: modelData.label
                                unit: modelData.unit || ""
                                editorType: modelData.type === "int" ? "spinbox" : "doublefield"
                                editorWidth: 210
                                intValue: modelData.defaultValue
                                intFrom: modelData["from"]
                                intTo: modelData["to"]
                                intStep: modelData.step || 1
                                doubleValue: modelData.defaultValue
                                doubleFrom: modelData["from"]
                                doubleTo: modelData["to"]
                                decimals: modelData.decimals || 2
                                enabled: dataController != null && !dataController.running
                                onValueCommitted: function(value) {
                                    if (dataController)
                                        dataController.setThicknessAlgorithmParameter(modelData.key, value)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Connections for data updates
    Connections {
        target: dataController
        function onDataReady(data, plotCount, pointsPerChan, xInc, recordEnd) {
            if (graphItem) {
                graphItem.chart(data, plotCount, pointsPerChan, xInc, recordEnd)
            }
        }
        function onMarkerReady(peakTimeSec, peakVoltage) {
            if (graphItem) {
                graphItem.addMarker(peakTimeSec, peakVoltage)
            }
        }
        function onClearMarkersRequested() {
            if (graphItem) {
                graphItem.clearMarkers()
            }
        }
        function onThicknessHistoryBackfilled(framesAgo, thickness) {
            if (thicknessPlot) {
                thicknessPlot.replaceRecentThickness(framesAgo, thickness)
            }
        }
        function onThicknessHistoryUpdated(thickness) {
            if (thicknessPlot) {
                thicknessPlot.addThickness(thickness)
            }
        }
        function onErrorOccurred(message) {
            console.log("Data Error:", message)
        }
        function onGraphConfigReady(xCordTimeDiv, xCordTimeOffset, timeUnit, yMin, yMax, yUnit) {
            if (graphItem) {
                graphItem.xCordTimeDiv = xCordTimeDiv
                graphItem.xCordTimeOffset = xCordTimeOffset
                graphItem.setTimeUnit(timeUnit)
                graphItem.setYUnit(yUnit)
                graphItem.setYAxisRange(yMin, yMax)
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "打开配置文件"
        nameFilters: ["XML Files (*.xml)", "All Files (*)"]
        onAccepted: {
            if (dataController) {
                dataController.setProfilePath(selectedFile)
                txtProfilePath.text = selectedFile
            }
        }
    }
}