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
                        intValue: 160
                        intFrom: 0
                        intTo: 1000
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

                        Label { text: "串口地址:"; font.pixelSize: 12; color: "#424242" }
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
                            text: "暂停"
                            Layout.preferredWidth: 80
                            Layout.preferredHeight: 50
                            font.pixelSize: 13
                            font.bold: true
                            Material.background: "#2196F3"
                            Material.foreground: "white"
                            enabled: dataController && dataController.running && !dataController.paused
                            onClicked: {
                                if (dataController) dataController.pauseAcquisition()
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
                            text: "检出率："
                            font.pixelSize: 14
                            font.bold: true
                            color: "#424242"
                        }
                        Label {
                            id: lblDetectionRate
                            text: dataController ? dataController.detectionRateText : "0%"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#009688"
                        }
                        Item { Layout.preferredWidth: 24 }
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
                                if (thicknessPlot) thicknessPlot.clear()
                                if (graphItem) graphItem.clear()
                            }
                        }

                        Comp.SwitchControl {
                            id: historySwitch
                            label: "历史记录"
                            onToggled: (checked) => {
                                console.log("[DataPage] historySwitch onToggled, checked:", checked)
                                if (dataController) dataController.setHistoryRecording(checked)
                            }
                            Component.onCompleted: {
                                historySwitch.checked = dataController ? dataController.historyRecording : false
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
        function onThicknessHistoryUpdated(thickness) {
            if (thicknessPlot) {
                thicknessPlot.addThickness(thickness)
            }
        }
        function onErrorOccurred(message) {
            console.log("Data Error:", message)
        }
        function onHistoryRecordingChanged() {
            if (historySwitch && dataController) {
                historySwitch.checked = dataController.historyRecording
            }
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