import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12

            Label {
                text: "工艺分析"
                font.pixelSize: 24
                font.bold: true
                color: "#3F51B5"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: analysisTabs.currentIndex === 0 ? "单路纵向壁厚诊断" : "跨钢管周期 / 顶头诊断"
                font.pixelSize: 12
                color: "#757575"
            }
        }

        TabBar {
            id: analysisTabs
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            TabButton { text: "单管分析" }
            TabButton { text: "批次趋势" }
        }

        Pane {
            visible: analysisTabs.currentIndex === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 8
            Material.elevation: 2

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: "钢管记录:"; font.pixelSize: 12 }
                ComboBox {
                    id: recordCombo
                    Layout.preferredWidth: 260
                    model: historyManager ? historyManager.recordTitles : []
                    enabled: count > 0
                }

                Label { text: "长/短周期分界:"; font.pixelSize: 12 }
                TextField {
                    id: cutoffField
                    Layout.preferredWidth: 60
                    text: "1.0"
                    font.pixelSize: 12
                }
                Label { text: "m"; font.pixelSize: 11; color: "#757575" }

                Label { text: "最小波动σ:"; font.pixelSize: 12 }
                TextField {
                    id: minVariationField
                    Layout.preferredWidth: 60
                    text: "0.05"
                    font.pixelSize: 12
                }
                Label { text: "mm"; font.pixelSize: 11; color: "#757575" }

                Label { text: "能量占比阈值:"; font.pixelSize: 12 }
                TextField {
                    id: dominanceField
                    Layout.preferredWidth: 55
                    text: "0.65"
                    font.pixelSize: 12
                }

                Label { text: "壁厚上下限±:"; font.pixelSize: 12 }
                TextField {
                    id: thicknessToleranceField
                    Layout.preferredWidth: 55
                    text: "0.8"
                    font.pixelSize: 12
                    onTextChanged: thicknessProfileCanvas.requestPaint()
                }
                Label { text: "mm"; font.pixelSize: 11; color: "#757575" }

                Item { Layout.fillWidth: true }

                Button {
                    text: processAnalyzer.hasAnalysis ? "重新分析" : "分析"
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 44
                    enabled: recordCombo.currentIndex >= 0
                    Material.background: "#3F51B5"
                    Material.foreground: "white"
                    onClicked: {
                        var cutoff = parseFloat(cutoffField.text)
                        var minVariation = parseFloat(minVariationField.text)
                        var dominance = parseFloat(dominanceField.text)
                        if (!isFinite(cutoff) || cutoff <= 0) cutoff = 1.0
                        if (!isFinite(minVariation) || minVariation < 0) minVariation = 0.05
                        if (!isFinite(dominance)) dominance = 0.65
                        processAnalyzer.analyzeRecord(recordCombo.currentIndex,
                                                      cutoff,
                                                      minVariation,
                                                      dominance)
                    }
                }
            }
        }

        Pane {
            visible: analysisTabs.currentIndex === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            padding: 10
            Material.elevation: 2

            RowLayout {
                anchors.fill: parent
                spacing: 18

                Label {
                    text: "钢管: " + (processAnalyzer.hasAnalysis
                                      ? (String(processAnalyzer.result.pipeId).length > 0
                                         ? processAnalyzer.result.pipeId
                                         : processAnalyzer.result.title)
                                      : "--")
                    font.pixelSize: 14
                    font.bold: true
                    color: "#3F51B5"
                }

                Label {
                    text: "钢级: " + (processAnalyzer.hasAnalysis && String(processAnalyzer.result.steelGrade).length > 0
                                      ? processAnalyzer.result.steelGrade : "--")
                    font.pixelSize: 12
                    color: "#424242"
                }

                Label {
                    text: "公称厚度: " + (processAnalyzer.hasAnalysis && Number(processAnalyzer.result.nominalThicknessMm) > 0
                                          ? Number(processAnalyzer.result.nominalThicknessMm).toFixed(3) + " mm"
                                          : "--")
                    font.pixelSize: 12
                    color: "#424242"
                }

                Label {
                    text: "测量点数: " + (processAnalyzer.hasAnalysis ? processAnalyzer.result.pointCount : "--")
                    font.pixelSize: 12
                    color: "#424242"
                }

                Label {
                    text: "管长: " + (processAnalyzer.hasAnalysis && Number(processAnalyzer.result.pipeLengthM) > 0
                                      ? Number(processAnalyzer.result.pipeLengthM).toFixed(2) + " m"
                                      : "--")
                    font.pixelSize: 12
                    color: "#424242"
                }

                Label {
                    text: "有效率: " + (processAnalyzer.hasAnalysis
                                        ? Number(processAnalyzer.result.validRatePercent).toFixed(1) + "%"
                                        : "--")
                    font.pixelSize: 12
                    font.bold: true
                    color: processAnalyzer.hasAnalysis && Number(processAnalyzer.result.validRatePercent) < 80
                           ? "#F44336" : "#009688"
                }

                Item { Layout.fillWidth: true }
            }
        }

        Pane {
            visible: analysisTabs.currentIndex === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: 330
            padding: 8
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: processAnalyzer.hasAnalysis && processAnalyzer.result.spatialAxis
                              ? "纵向壁厚剖面（位置 / m）"
                              : "纵向壁厚剖面（缺少线速度时按采样点）"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }

                    Rectangle { width: 20; height: 3; color: "#3F51B5" }
                    Label { text: "实际壁厚"; font.pixelSize: 10; color: "#616161" }
                    Rectangle { width: 20; height: 3; color: "#4CAF50" }
                    Label { text: "公称壁厚"; font.pixelSize: 10; color: "#616161" }
                    Rectangle { width: 20; height: 3; color: "#F44336" }
                    Label { text: "上/下限"; font.pixelSize: 10; color: "#616161" }
                }

                Canvas {
                    id: thicknessProfileCanvas
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var data = processAnalyzer.thicknessProfile
                        if (!data || data.length < 2) return

                        var nominal = Number(processAnalyzer.result.nominalThicknessMm)
                        var tolerance = parseFloat(thicknessToleranceField.text)
                        if (!isFinite(tolerance) || tolerance < 0) tolerance = 0.8

                        var left = 52, right = 18, top = 14, bottom = 34
                        var w = width - left - right
                        var h = height - top - bottom

                        var xMin = Number(data[0].x)
                        var xMax = Number(data[data.length - 1].x)
                        if (!isFinite(xMin)) xMin = 0
                        if (!isFinite(xMax) || xMax <= xMin) xMax = xMin + 1

                        var minY = Number.POSITIVE_INFINITY
                        var maxY = Number.NEGATIVE_INFINITY
                        for (var i = 0; i < data.length; ++i) {
                            var value = Number(data[i].thickness)
                            if (isFinite(value)) {
                                minY = Math.min(minY, value)
                                maxY = Math.max(maxY, value)
                            }
                        }

                        var upper = nominal > 0 ? nominal + tolerance : NaN
                        var lower = nominal > 0 ? nominal - tolerance : NaN
                        if (isFinite(upper)) maxY = Math.max(maxY, upper)
                        if (isFinite(lower)) minY = Math.min(minY, lower)
                        if (!isFinite(minY) || !isFinite(maxY)) return

                        var span = maxY - minY
                        if (span < 0.05) span = 0.05
                        minY -= span * 0.10
                        maxY += span * 0.10

                        function px(x) {
                            return left + (x - xMin) / (xMax - xMin) * w
                        }
                        function py(y) {
                            return top + h - (y - minY) / (maxY - minY) * h
                        }
                        function drawReference(value, color, dash) {
                            if (!isFinite(value)) return
                            ctx.strokeStyle = color
                            ctx.lineWidth = 1.2
                            ctx.setLineDash(dash)
                            ctx.beginPath()
                            ctx.moveTo(left, py(value))
                            ctx.lineTo(left + w, py(value))
                            ctx.stroke()
                            ctx.setLineDash([])
                        }

                        ctx.strokeStyle = "#9E9E9E"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(left, top)
                        ctx.lineTo(left, top + h)
                        ctx.lineTo(left + w, top + h)
                        ctx.stroke()

                        drawReference(nominal, "#4CAF50", [6, 4])
                        drawReference(upper, "#F44336", [4, 4])
                        drawReference(lower, "#F44336", [4, 4])

                        ctx.strokeStyle = "#3F51B5"
                        ctx.lineWidth = 1.6
                        ctx.beginPath()
                        var started = false
                        for (var j = 0; j < data.length; ++j) {
                            var valid = Boolean(data[j].valid)
                            var tx = Number(data[j].x)
                            var ty = Number(data[j].thickness)
                            if (!valid || !isFinite(tx) || !isFinite(ty)) {
                                started = false
                                continue
                            }
                            if (!started) {
                                ctx.moveTo(px(tx), py(ty))
                                started = true
                            } else {
                                ctx.lineTo(px(tx), py(ty))
                            }
                        }
                        ctx.stroke()

                        ctx.fillStyle = "#616161"
                        ctx.font = "10px sans-serif"
                        ctx.fillText(maxY.toFixed(3) + " mm", 2, top + 9)
                        ctx.fillText(minY.toFixed(3) + " mm", 2, top + h)
                        ctx.fillText(xMin.toFixed(processAnalyzer.result.spatialAxis ? 2 : 0), left - 4, top + h + 16)
                        ctx.fillText(xMax.toFixed(processAnalyzer.result.spatialAxis ? 2 : 0), left + w - 28, top + h + 16)
                        ctx.fillText(processAnalyzer.result.spatialAxis ? "位置 / m" : "采样点", left + w / 2 - 18, top + h + 30)
                    }

                    Connections {
                        target: processAnalyzer
                        function onAnalysisChanged() { thicknessProfileCanvas.requestPaint() }
                    }
                }
            }
        }

        RowLayout {
            visible: analysisTabs.currentIndex === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 10

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1.6
                Layout.preferredHeight: 330
                padding: 8
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        text: processAnalyzer.hasAnalysis && processAnalyzer.result.spatialAxis
                              ? "壁厚空间频谱（cycles/m）"
                              : "壁厚频谱（缺少线速度时按采样点分析）"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Canvas {
                        id: spectrumCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var data = processAnalyzer.spectrum
                            if (!data || data.length < 2) return

                            var left = 45, right = 12, top = 16, bottom = 30
                            var w = width - left - right
                            var h = height - top - bottom
                            var maxX = 0, maxY = 0
                            for (var i = 0; i < data.length; ++i) {
                                maxX = Math.max(maxX, Number(data[i].frequency))
                                maxY = Math.max(maxY, Number(data[i].amplitude))
                            }
                            var dominantFrequency = processAnalyzer.hasAnalysis
                                ? Number(processAnalyzer.result.dominantFrequency) : 0
                            var dominantAmplitude = processAnalyzer.hasAnalysis
                                ? Number(processAnalyzer.result.dominantAmplitudeMm) : 0
                            maxX = Math.max(maxX, dominantFrequency)
                            maxY = Math.max(maxY, dominantAmplitude)
                            if (maxX <= 0 || maxY <= 0) return

                            var cutoffFrequency = processAnalyzer.hasAnalysis
                                ? Number(processAnalyzer.result.cutoffFrequency) : 0
                            var cutoffX = cutoffFrequency > 0
                                ? left + Math.min(1.0, cutoffFrequency / maxX) * w
                                : left

                            if (cutoffX > left) {
                                ctx.fillStyle = "rgba(76, 175, 80, 0.10)"
                                ctx.fillRect(left, top, cutoffX - left, h)
                            }
                            if (cutoffX < left + w) {
                                ctx.fillStyle = "rgba(255, 152, 0, 0.10)"
                                ctx.fillRect(cutoffX, top, left + w - cutoffX, h)
                            }

                            ctx.fillStyle = "#2E7D32"
                            ctx.font = "10px sans-serif"
                            if (cutoffX - left > 55)
                                ctx.fillText("长周期区域", left + 6, top + 12)
                            ctx.fillStyle = "#E65100"
                            if (left + w - cutoffX > 55)
                                ctx.fillText("短周期区域", cutoffX + 6, top + 12)

                            ctx.strokeStyle = "#9E9E9E"
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(left, top)
                            ctx.lineTo(left, top + h)
                            ctx.lineTo(left + w, top + h)
                            ctx.stroke()

                            if (cutoffFrequency > 0 && cutoffFrequency < maxX) {
                                ctx.strokeStyle = "#757575"
                                ctx.lineWidth = 1
                                ctx.setLineDash([5, 4])
                                ctx.beginPath()
                                ctx.moveTo(cutoffX, top)
                                ctx.lineTo(cutoffX, top + h)
                                ctx.stroke()
                                ctx.setLineDash([])
                            }

                            ctx.strokeStyle = "#3F51B5"
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            for (var j = 0; j < data.length; ++j) {
                                var x = left + Number(data[j].frequency) / maxX * w
                                var y = top + h - Number(data[j].amplitude) / maxY * h
                                if (j === 0) ctx.moveTo(x, y)
                                else ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            if (dominantFrequency > 0 && dominantAmplitude > 0) {
                                var peakX = left + dominantFrequency / maxX * w
                                var peakY = top + h - dominantAmplitude / maxY * h
                                ctx.fillStyle = "#D32F2F"
                                ctx.beginPath()
                                ctx.arc(peakX, peakY, 5, 0, Math.PI * 2)
                                ctx.fill()
                                ctx.font = "10px sans-serif"
                                ctx.fillText("主峰", peakX + 7, Math.max(top + 12, peakY - 5))
                                if (processAnalyzer.result.spatialAxis && Number(processAnalyzer.result.dominantWavelength) > 0)
                                    ctx.fillText("λ=" + Number(processAnalyzer.result.dominantWavelength).toFixed(3) + " m",
                                                 peakX + 7, Math.max(top + 24, peakY + 8))
                            }

                            ctx.fillStyle = "#616161"
                            ctx.font = "10px sans-serif"
                            ctx.fillText("0", left - 8, top + h + 14)
                            ctx.fillText(maxX.toFixed(2), left + w - 24, top + h + 14)
                            ctx.fillText(maxY.toFixed(3) + " mm", 2, top + 10)
                            ctx.fillText(processAnalyzer.hasAnalysis && processAnalyzer.result.spatialAxis
                                         ? "空间频率 / cycles·m⁻¹" : "归一化频率",
                                         left + w / 2 - 45, top + h + 28)
                        }

                        Connections {
                            target: processAnalyzer
                            function onAnalysisChanged() { spectrumCanvas.requestPaint() }
                        }
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: 330
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label { text: "纵向工艺特征"; font.pixelSize: 16; font.bold: true }

                    GridLayout {
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 6
                        Layout.fillWidth: true

                        Label { text: "标准差:"; color: "#757575" }
                        Label { text: processAnalyzer.hasAnalysis ? Number(processAnalyzer.result.stdDevMm).toFixed(4) + " mm" : "--" }

                        Label { text: "主周期:"; color: "#757575" }
                        Label {
                            text: processAnalyzer.hasAnalysis
                                  ? Number(processAnalyzer.result.dominantWavelength).toFixed(3)
                                    + (processAnalyzer.result.spatialAxis ? " m" : " samples")
                                  : "--"
                        }

                        Label { text: "主周期幅值:"; color: "#757575" }
                        Label { text: processAnalyzer.hasAnalysis ? Number(processAnalyzer.result.dominantAmplitudeMm).toFixed(4) + " mm" : "--" }

                        Label { text: "长周期能量:"; color: "#757575" }
                        Label {
                            text: processAnalyzer.hasAnalysis ? (Number(processAnalyzer.result.longEnergyRatio) * 100).toFixed(1) + "%" : "--"
                            color: processAnalyzer.hasAnalysis && processAnalyzer.result.longAbnormal ? "#F44336" : "#212121"
                        }

                        Label { text: "短周期能量:"; color: "#757575" }
                        Label {
                            text: processAnalyzer.hasAnalysis ? (Number(processAnalyzer.result.shortEnergyRatio) * 100).toFixed(1) + "%" : "--"
                            color: processAnalyzer.hasAnalysis && processAnalyzer.result.shortAbnormal ? "#F44336" : "#212121"
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#E0E0E0" }

                    Label {
                        text: "诊断建议"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#3F51B5"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: processAnalyzer.hasAnalysis ? processAnalyzer.result.diagnosis : "选择一支钢管记录后进行分析。"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: "#424242"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: processAnalyzer.hasAnalysis
                        text: processAnalyzer.hasAnalysis ? processAnalyzer.result.thresholdNote : ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                        color: "#FF9800"
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        ColumnLayout {
            visible: analysisTabs.currentIndex === 1
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 10

            Pane {
                Layout.fillWidth: true
                padding: 8
                Material.elevation: 2

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label { text: "最近记录:"; font.pixelSize: 12 }
                    SpinBox {
                        id: batchCountBox
                        from: 3
                        to: 100
                        value: Math.min(30, Math.max(3, historyManager ? historyManager.recordCount : 3))
                        editable: true
                        Layout.preferredWidth: 120
                    }
                    Label { text: "支"; font.pixelSize: 11; color: "#757575" }

                    Label { text: "极差率阈值:"; font.pixelSize: 12 }
                    TextField {
                        id: rangeRateThresholdField
                        Layout.preferredWidth: 60
                        text: "5.0"
                        font.pixelSize: 12
                    }
                    Label { text: "%"; font.pixelSize: 11; color: "#757575" }

                    Label { text: "最低有效率:"; font.pixelSize: 12 }
                    TextField {
                        id: minValidRateField
                        Layout.preferredWidth: 60
                        text: "80"
                        font.pixelSize: 12
                    }
                    Label { text: "%"; font.pixelSize: 11; color: "#757575" }

                    Label { text: "在线顶头数:"; font.pixelSize: 12 }
                    SpinBox {
                        id: plugCountBox
                        from: 0
                        to: 20
                        value: 0
                        editable: true
                        Layout.preferredWidth: 120
                    }

                    Label { text: "周期容差:"; font.pixelSize: 12 }
                    SpinBox {
                        id: cycleToleranceBox
                        from: 0
                        to: 3
                        value: 0
                        editable: true
                        Layout.preferredWidth: 120
                    }
                    Label { text: "支"; font.pixelSize: 11; color: "#757575" }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "分析批次"
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 44
                        Material.background: "#3F51B5"
                        Material.foreground: "white"
                        enabled: historyManager && historyManager.recordCount >= 3
                        onClicked: {
                            var threshold = parseFloat(rangeRateThresholdField.text)
                            var minValid = parseFloat(minValidRateField.text)
                            if (!isFinite(threshold) || threshold < 0) threshold = 5.0
                            if (!isFinite(minValid)) minValid = 80.0
                            processAnalyzer.analyzeBatch(
                                batchCountBox.value,
                                threshold,
                                minValid,
                                plugCountBox.value,
                                cycleToleranceBox.value)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Pane {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1.7
                    Layout.preferredHeight: 360
                    padding: 8
                    Material.elevation: 2

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "钢管壁厚极差率趋势"
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis
                                      ? "异常 " + processAnalyzer.batchResult.abnormalCount
                                        + " / " + processAnalyzer.batchResult.analyzedCount + " 支"
                                      : ""
                                font.pixelSize: 11
                                color: "#757575"
                            }
                        }

                        Canvas {
                            id: batchCanvas
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var data = processAnalyzer.batchTrend
                                if (!data || data.length === 0) return

                                var left = 48, right = 16, top = 18, bottom = 34
                                var w = width - left - right
                                var h = height - top - bottom
                                var maxY = 0
                                for (var i = 0; i < data.length; ++i)
                                    maxY = Math.max(maxY, Number(data[i].rangeRatePercent))
                                var threshold = processAnalyzer.hasBatchAnalysis
                                    ? Number(processAnalyzer.batchResult.rangeRateThresholdPercent)
                                    : 0
                                maxY = Math.max(maxY, threshold) * 1.15
                                if (maxY <= 0) maxY = 1

                                ctx.strokeStyle = "#9E9E9E"
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.moveTo(left, top)
                                ctx.lineTo(left, top + h)
                                ctx.lineTo(left + w, top + h)
                                ctx.stroke()

                                if (threshold > 0) {
                                    var ty = top + h - threshold / maxY * h
                                    ctx.strokeStyle = "#FF9800"
                                    ctx.setLineDash([5, 4])
                                    ctx.beginPath()
                                    ctx.moveTo(left, ty)
                                    ctx.lineTo(left + w, ty)
                                    ctx.stroke()
                                    ctx.setLineDash([])
                                }

                                ctx.strokeStyle = "#3F51B5"
                                ctx.lineWidth = 1.5
                                ctx.beginPath()
                                for (var j = 0; j < data.length; ++j) {
                                    var x = data.length > 1
                                        ? left + j / (data.length - 1) * w
                                        : left + w / 2
                                    var y = top + h - Number(data[j].rangeRatePercent) / maxY * h
                                    if (j === 0) ctx.moveTo(x, y)
                                    else ctx.lineTo(x, y)
                                }
                                ctx.stroke()

                                for (var k = 0; k < data.length; ++k) {
                                    var px = data.length > 1
                                        ? left + k / (data.length - 1) * w
                                        : left + w / 2
                                    var py = top + h - Number(data[k].rangeRatePercent) / maxY * h
                                    ctx.beginPath()
                                    ctx.arc(px, py, data[k].abnormal ? 5 : 3, 0, Math.PI * 2)
                                    ctx.fillStyle = data[k].abnormal ? "#F44336" : (data[k].qualified ? "#3F51B5" : "#BDBDBD")
                                    ctx.fill()
                                }

                                ctx.fillStyle = "#616161"
                                ctx.font = "10px sans-serif"
                                ctx.fillText("1", left - 3, top + h + 16)
                                ctx.fillText(String(data.length), left + w - 10, top + h + 16)
                                ctx.fillText(maxY.toFixed(1) + "%", 2, top + 10)
                                if (threshold > 0)
                                    ctx.fillText("阈值 " + threshold.toFixed(1) + "%", left + 6, top + 12)
                            }

                            Connections {
                                target: processAnalyzer
                                function onBatchAnalysisChanged() { batchCanvas.requestPaint() }
                            }
                        }
                    }
                }

                Pane {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 360
                    padding: 12
                    Material.elevation: 2

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 7

                        Label { text: "批次周期特征"; font.pixelSize: 16; font.bold: true }

                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 5
                            Layout.fillWidth: true

                            Label { text: "分析钢管:"; color: "#757575" }
                            Label { text: processAnalyzer.hasBatchAnalysis ? processAnalyzer.batchResult.analyzedCount + " 支" : "--" }

                            Label { text: "有效钢管:"; color: "#757575" }
                            Label { text: processAnalyzer.hasBatchAnalysis ? processAnalyzer.batchResult.qualifiedCount + " 支" : "--" }

                            Label { text: "异常钢管:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis ? processAnalyzer.batchResult.abnormalCount + " 支" : "--"
                                color: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.abnormalCount > 0 ? "#F44336" : "#212121"
                            }

                            Label { text: "检测周期:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.detectedCyclePipes > 0
                                      ? processAnalyzer.batchResult.detectedCyclePipes + " 支"
                                      : "--"
                            }

                            Label { text: "周期稳定度:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis
                                      ? Number(processAnalyzer.batchResult.cycleMatchPercent).toFixed(1) + "%"
                                      : "--"
                            }

                            Label { text: "顶头间隔匹配:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.plugCount > 0
                                      ? Number(processAnalyzer.batchResult.plugIntervalMatchPercent).toFixed(1) + "%"
                                      : "--"
                            }

                            Label { text: "轮换相位集中:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.plugCount > 0
                                      ? Number(processAnalyzer.batchResult.phaseMatchPercent).toFixed(1) + "%"
                                      : "--"
                            }

                            Label { text: "疑似顶头相位:"; color: "#757575" }
                            Label {
                                text: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.plugSuspected
                                      ? "第 " + processAnalyzer.batchResult.suspectPlugPosition + " 个"
                                      : "--"
                                color: processAnalyzer.hasBatchAnalysis && processAnalyzer.batchResult.plugSuspected ? "#F44336" : "#212121"
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#E0E0E0" }

                        Label { text: "诊断建议"; font.pixelSize: 14; font.bold: true; color: "#3F51B5" }
                        Label {
                            Layout.fillWidth: true
                            text: processAnalyzer.hasBatchAnalysis
                                  ? processAnalyzer.batchResult.diagnosis
                                  : "设置极差率阈值和在线顶头数量后分析最近一批钢管。"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: "#424242"
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: processAnalyzer.hasBatchAnalysis
                            text: processAnalyzer.hasBatchAnalysis ? processAnalyzer.batchResult.formulaNote : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                            color: "#FF9800"
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: processAnalyzer.hasBatchAnalysis
                            text: processAnalyzer.hasBatchAnalysis ? processAnalyzer.batchResult.sequenceNote : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                            color: "#757575"
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 12 }
    }

    Connections {
        target: processAnalyzer
        function onErrorOccurred(message) {
            console.log("Process analysis error:", message)
        }
    }
}
