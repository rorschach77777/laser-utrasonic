import QtQuick

// Thickness RESULTS, not an acquired signal waveform. Retain gaps and repair T2 backfills.
// Acquisition may append at 100 Hz; painting and bounds calculation are capped at 10 Hz.
EngineeringPlotFrame {
    id: root
    objectName: "engineeringTrend"
    readonly property int capacity: 1000
    property var samples: []
    property int totalFrames: 0
    property int pointCount: 0
    property int validPointCount: 0
    property real windowMean: NaN
    property bool dirty: false
    hasData: validPointCount > 0
    xMin: Math.max(1, totalFrames - pointCount + 1)
    xMax: Math.max(xMin + 1, totalFrames)
    xTitle: "测量序号 · 最近 1000 次"
    emptyText: pointCount > 0 ? "当前窗口无有效测厚结果" : "开始测量后显示壁厚趋势"

    function appendThickness(thickness) {
        samples.push(isFinite(thickness) && thickness > 0 ? Number(thickness) : NaN)
        if (samples.length > capacity)
            samples.shift()
        pointCount = samples.length
        totalFrames += 1
        dirty = true
    }
    function replaceRecentThickness(framesAgo, thickness) {
        const index = samples.length - framesAgo
        if (framesAgo <= 0 || index < 0 || index >= samples.length)
            return
        samples[index] = isFinite(thickness) && thickness > 0 ? Number(thickness) : NaN
        dirty = true
    }
    function clear() {
        samples = []
        pointCount = 0
        totalFrames = 0
        validPointCount = 0
        windowMean = NaN
        dirty = true
        redraw()
    }
    function redraw() {
        if (!dirty)
            return
        let low = Infinity, high = -Infinity, sum = 0, count = 0
        for (let i = 0; i < samples.length; ++i) {
            const value = samples[i]
            if (!isFinite(value))
                continue
            low = Math.min(low, value)
            high = Math.max(high, value)
            sum += value
            ++count
        }
        validPointCount = count
        windowMean = count ? sum / count : NaN
        const margin = count ? Math.max(0.03, (high - low) * 0.15) : 0
        yMin = count ? low - margin : 0
        yMax = count ? high + margin : 1
        dirty = false
        trace.requestPaint()
    }
    onVisibleChanged: { if (visible) redraw() }
    Timer {
        interval: 100
        repeat: true
        running: root.visible
        onTriggered: root.redraw()
    }
    Canvas {
        id: trace
        parent: root.viewport
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.hasData || width <= 0 || height <= 0)
                return
            const span = root.yMax - root.yMin
            const meanY = height * (root.yMax - root.windowMean) / span
            ctx.strokeStyle = "#dcaa55"
            ctx.lineWidth = 1
            ctx.beginPath()
            for (let dx = 0; dx < width; dx += 12) {
                ctx.moveTo(dx, meanY)
                ctx.lineTo(Math.min(dx + 6, width), meanY)
            }
            ctx.stroke()
            ctx.strokeStyle = "#64b5f6"
            ctx.fillStyle = "#64b5f6"
            ctx.lineWidth = 1.6
            ctx.beginPath()
            let connected = false
            for (let i = 0; i < root.samples.length; ++i) {
                const value = root.samples[i]
                if (!isFinite(value)) {
                    connected = false
                    continue
                }
                const x = width * i / Math.max(1, root.samples.length - 1)
                const y = height * (root.yMax - value) / span
                if (connected)
                    ctx.lineTo(x, y)
                else
                    ctx.moveTo(x, y)
                connected = true
            }
            ctx.stroke()
            // Isolated measurements between invalid frames remain visible.
            for (let j = 0; j < root.samples.length; ++j) {
                if (!isFinite(root.samples[j]))
                    continue
                const isolated = (j === 0 || !isFinite(root.samples[j - 1]))
                    && (j + 1 === root.samples.length || !isFinite(root.samples[j + 1]))
                if (isolated) {
                    ctx.beginPath()
                    ctx.arc(width * j / Math.max(1, root.samples.length - 1),
                            height * (root.yMax - root.samples[j]) / span, 2.5, 0, Math.PI * 2)
                    ctx.fill()
                }
            }
        }
    }
}
