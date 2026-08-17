#include "SimpleGraphItem.h"
#include <QPainter>
#include <QtMath>
#include <cstring>

QColor SimpleGraphItem::s_lineColor[16] = {
    QColor(255, 255, 255), QColor(255, 0, 0), QColor(185, 99, 188),
    QColor(255, 0, 255), QColor(0, 255, 255), QColor(255, 255, 0),
    QColor(155, 55, 255), QColor(255, 127, 0), QColor(106, 147, 219),
    QColor(209, 146, 117), QColor(143, 188, 143), QColor(245, 182, 204),
    QColor(40, 193, 164), QColor(165, 228, 64), QColor(204, 150, 53),
    QColor(236, 228, 137)
};

SimpleGraphItem::SimpleGraphItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_plotCount(0)
    , m_pointCountPerScreen(0)
    , m_copyDataCountPerChan(0)
    , m_xIncByTime(0)
    , m_xCordDividedRate(0)
    , m_dataCountCachePerPlot(0)
    , m_mapDataIndexPerPlot(0)
    , m_dataCountPerPlot(0)
    , m_shiftCount(0)
    , m_recordEndFlag(false)
    , m_xPosition(0)
    , m_drawRecordEnd(false)
    , m_recordEndPointX(0)
    , m_autoScale(true)
    , m_timeUnit(Microsecond)
    , m_yUnit(Volt)
    , m_yCordRangeMax(5.0)
    , m_yCordRangeMin(-5.0)
    , m_xCordTimeDiv(0)
    , m_xCordTimeOffset(0)
    , m_drawDataBufferLength(0)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setFillColor(Qt::black);
}

SimpleGraphItem::~SimpleGraphItem()
{
}

void SimpleGraphItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);

    int w = (int)width();
    int h = (int)height();
    if (w <= 0 || h <= 0) return;

    int plotLeft = LEFT_MARGIN;
    int plotRight = w - RIGHT_MARGIN;
    int plotTop = TOP_MARGIN;
    int plotBottom = h - BOTTOM_MARGIN;
    int plotWidth = plotRight - plotLeft;
    int plotHeight = plotBottom - plotTop;

    // Background
    painter->fillRect(QRect(0, 0, w, h), Qt::black);

    // Grid
    QPen penGrid(Qt::darkGray, 1, Qt::DotLine);
    painter->setPen(penGrid);
    for (int i = 1; i < 10; i++) {
        int x = plotLeft + i * plotWidth / 10;
        painter->drawLine(x, plotTop, x, plotBottom);
        int y = plotTop + i * plotHeight / 10;
        painter->drawLine(plotLeft, y, plotRight, y);
    }

    // Record end marker
    if (m_drawRecordEnd) {
        QPen penEnd(Qt::yellow, 1);
        painter->setPen(penEnd);
        int x = plotLeft + m_recordEndPointX;
        if (x >= plotLeft && x <= plotRight)
            painter->drawLine(x, plotTop, x, plotBottom);
    }

    // Draw waveforms
    if (!m_dataPointBuffer.empty() && m_copyDataCountPerChan > 0) {
        int stride = w * 4 + 1;
        for (int i = 0; i < m_plotCount; i++) {
            QPen penWave(s_lineColor[i % 16], 1);
            painter->setPen(penWave);
            const QPoint *pts = m_dataPointBuffer.data() + i * stride;
            painter->drawPolyline(pts, m_copyDataCountPerChan);
        }
    }

    // Draw axis
    QPen axisPen(Qt::white, 1);
    painter->setPen(axisPen);
    painter->drawLine(plotLeft, plotTop, plotLeft, plotBottom);
    painter->drawLine(plotLeft, plotBottom, plotRight, plotBottom);

    // Y-axis labels
    QFont font = painter->font();
    font.setPointSize(10);
    painter->setFont(font);
    double yRange = m_yCordRangeMax - m_yCordRangeMin;
    if (yRange < 1e-12) yRange = 1.0;
    for (int i = 0; i <= 4; i++) {
        double yVal = m_yCordRangeMin + i * yRange / 4.0;
        int y = plotBottom - (int)((yVal - m_yCordRangeMin) / yRange * plotHeight);
        painter->drawLine(plotLeft - 5, y, plotLeft, y);
        QString label = QString::number(yVal, 'f', 2);
        painter->drawText(plotLeft - 35, y - 6, 30, 16, Qt::AlignRight, label);
    }

    // X-axis labels
    double xRange = m_xCordTimeDiv * 10;
    if (xRange < 1e-12) xRange = 1.0;
    for (int i = 0; i <= 10; i++) {
        double xVal = m_xCordTimeOffset + i * xRange / 10.0;
        int x = plotLeft + (int)(i * plotWidth / 10.0);
        painter->drawLine(x, plotBottom, x, plotBottom + 5);
        double val = xVal;
        QString unitStr;
        switch (m_timeUnit) {
            case Nanosecond:  unitStr = "ns"; break;
            case Microsecond: unitStr = "µs"; break;
            case Millisecond: unitStr = "ms"; break;
            case Second:      unitStr = "s";  break;
            default:          unitStr = "";
        }
        if (val >= 1000 && m_timeUnit < Second) { val /= 1000; }
        QString label = QString::number(val, 'f', 1) + unitStr;
        painter->drawText(x - 20, plotBottom + 12, 40, 12, Qt::AlignHCenter, label);
    }

    // Y unit label
    QString yUnitStr;
    switch (m_yUnit) {
        case Volt: yUnitStr = "V"; break;
        case Millivolt: yUnitStr = "mV"; break;
        case Microvolt: yUnitStr = "µV"; break;
        default: yUnitStr = "";
    }
    painter->drawText(plotLeft + 5, plotTop - 5, yUnitStr);

    // Draw markers
    if (!m_markers.empty() && m_xIncByTime > 1e-12 && m_pointCountPerScreen > 0) {
        QPen pen(Qt::red);
        pen.setWidth(1);
        painter->setPen(pen);
        painter->setBrush(QBrush(Qt::red, Qt::SolidPattern));
        for (const QPointF &marker : m_markers) {
            int idx = static_cast<int>(std::round(marker.x() / m_xIncByTime));
            if (idx >= 0 && idx < m_pointCountPerScreen) {
                int x = plotLeft + static_cast<int>(idx * m_xCordDividedRate);
                double yScale = 1.0 * plotHeight / yRange;
                int y = plotTop + plotHeight - static_cast<int>(yScale * (marker.y() - m_yCordRangeMin));
                y = qBound(plotTop, y, plotTop + plotHeight);
                painter->drawEllipse(QPoint(x, y), 5, 5);
            }
        }
    }
}

void SimpleGraphItem::chart(const QVector<double> &data, int plotCount, int pointsPerChan,
                             double xInc, bool recordEnd)
{
    m_xIncByTime = xInc;
    m_dataCountPerPlot = pointsPerChan;
    m_recordEndFlag = recordEnd;

    if (plotCount != m_plotCount) {
        m_plotCount = plotCount;
        int w = (int)width();
        if (w <= 0) w = 800;
        m_drawDataBufferLength = plotCount * (w * 4 + 1);
        m_drawDataBuffer.resize(m_drawDataBufferLength);
        m_dataPointBuffer.resize(plotCount * (w * 4 + 1));
        m_dataCountCachePerPlot = 0;
    }

    calcDrawParams(xInc);
    saveData(data, plotCount, pointsPerChan);
}

void SimpleGraphItem::clear()
{
    m_plotCount = 0;
    m_copyDataCountPerChan = 0;
    m_dataCountCachePerPlot = 0;
    m_dataCountPerPlot = 0;
    m_drawRecordEnd = false;
    m_drawDataBuffer.clear();
    m_dataPointBuffer.clear();
    m_markers.clear();
    update();
}

void SimpleGraphItem::addMarker(double xTimeSec, double yValue)
{
    m_markers.push_back(QPointF(xTimeSec, yValue));
    update();
}

void SimpleGraphItem::clearMarkers()
{
    m_markers.clear();
    update();
}

void SimpleGraphItem::setAutoScale(bool enable)
{
    if (m_autoScale != enable) {
        m_autoScale = enable;
        emit autoScaleChanged();
    }
}

void SimpleGraphItem::setYMin(double min)
{
    if (!qFuzzyCompare(m_yCordRangeMin, min)) {
        m_yCordRangeMin = min;
        emit yRangeChanged();
    }
}

void SimpleGraphItem::setYMax(double max)
{
    if (!qFuzzyCompare(m_yCordRangeMax, max)) {
        m_yCordRangeMax = max;
        emit yRangeChanged();
    }
}

void SimpleGraphItem::setYAxisRange(double min, double max)
{
    if (!qFuzzyCompare(m_yCordRangeMin, min) || !qFuzzyCompare(m_yCordRangeMax, max)) {
        m_yCordRangeMin = min;
        m_yCordRangeMax = max;
        applyYAxisRange();
        emit yRangeChanged();
    }
}

void SimpleGraphItem::setXCordTimeDiv(double div)
{
    if (!qFuzzyCompare(m_xCordTimeDiv, div)) {
        m_xCordTimeDiv = div;
        emit xCordChanged();
    }
}

void SimpleGraphItem::setXCordTimeOffset(double offset)
{
    if (!qFuzzyCompare(m_xCordTimeOffset, offset)) {
        m_xCordTimeOffset = offset;
        emit xCordChanged();
    }
}

void SimpleGraphItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_plotCount > 0) {
        int w = (int)newGeometry.width();
        if (w > 0) {
            m_dataPointBuffer.resize(m_plotCount * (w * 4 + 1));
            applyYAxisRange();
        }
    }
}

void SimpleGraphItem::calcDrawParams(double xIncBySec)
{
    if (m_dataCountPerPlot > 0 && width() > 0) {
        int plotWidth = (int)width() - LEFT_MARGIN - RIGHT_MARGIN;
        if (plotWidth < 1) plotWidth = 1;
        m_xCordDividedRate = width() / (double)(m_dataCountPerPlot - 1);
        m_pointCountPerScreen = m_dataCountPerPlot;
        m_shiftCount = 0;
        return;
    }
    if (xIncBySec < 1e-15) return;
    m_shiftCount = (int)(m_xCordTimeOffset * 1.0 / (xIncBySec * 1000));
    double xBase = width() * xIncBySec * 100.0;
    m_xCordDividedRate = xBase / m_xCordTimeDiv;
    m_pointCountPerScreen = (int)ceil((width() - LEFT_MARGIN - RIGHT_MARGIN) * m_xCordTimeDiv / xBase) + 1;
}

void SimpleGraphItem::saveData(const QVector<double> &data, int plotCount, int dataCountPerPlot)
{
    int neededLen = plotCount * dataCountPerPlot;
    if (neededLen > m_drawDataBufferLength) {
        m_drawDataBufferLength = neededLen;
        m_drawDataBuffer.resize(m_drawDataBufferLength);
    }
    std::memcpy(m_drawDataBuffer.data(), data.data(), neededLen * sizeof(double));
    m_dataCountCachePerPlot = dataCountPerPlot;
    m_mapDataIndexPerPlot = 0;
    m_shiftCount = 0;
    if (m_recordEndFlag) {
        m_xPosition = dataCountPerPlot - 1;
        m_drawRecordEnd = true;
    } else {
        m_drawRecordEnd = false;
    }
    if (m_autoScale) updateYAxisRangeFromData();
    applyYAxisRange();
}

void SimpleGraphItem::mapDataPoints()
{
    int plotHeight = (int)height() - TOP_MARGIN - BOTTOM_MARGIN;
    if (plotHeight < 1) plotHeight = 1;
    double yDivRate = 1.0 * plotHeight / (m_yCordRangeMax - m_yCordRangeMin);

    int count = m_dataCountCachePerPlot - (int)m_shiftCount - m_mapDataIndexPerPlot;
    m_copyDataCountPerChan = count > m_pointCountPerScreen ? m_pointCountPerScreen : count;
    if (m_copyDataCountPerChan < 1) return;

    int plotLeft = LEFT_MARGIN;
    int plotTop = TOP_MARGIN;
    int stride = (int)width() * 4 + 1;

    for (int idx = 0; idx < m_copyDataCountPerChan; ++idx) {
        for (int i = 0; i < m_plotCount; i++) {
            int dataIdx = i * stride + idx;
            double val = m_drawDataBuffer[m_plotCount * (idx + m_mapDataIndexPerPlot + (int)m_shiftCount) + i];
            int y = plotTop + plotHeight - (int)ceil(yDivRate * (val - m_yCordRangeMin));
            if (y < plotTop) y = plotTop;
            if (y > plotTop + plotHeight) y = plotTop + plotHeight;
            int x = plotLeft + (int)(idx * m_xCordDividedRate);
            m_dataPointBuffer[dataIdx].setX(x);
            m_dataPointBuffer[dataIdx].setY(y);
        }
    }
    if (m_drawRecordEnd) {
        m_recordEndPointX = (int)(m_xPosition * m_xCordDividedRate);
    }
}

void SimpleGraphItem::updateYAxisRangeFromData()
{
    if (m_dataCountCachePerPlot == 0 || m_plotCount == 0) return;
    double minVal = m_drawDataBuffer[0];
    double maxVal = minVal;
    int total = m_dataCountCachePerPlot * m_plotCount;
    for (int i = 0; i < total; ++i) {
        double v = m_drawDataBuffer[i];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    if (qFuzzyCompare(minVal, maxVal)) { minVal -= 1.0; maxVal += 1.0; }
    else { double margin = (maxVal - minVal) * 0.05; minVal -= margin; maxVal += margin; }
    m_yCordRangeMin = minVal;
    m_yCordRangeMax = maxVal;
}

void SimpleGraphItem::applyYAxisRange()
{
    if (m_dataCountCachePerPlot > 0 && m_plotCount > 0) {
        mapDataPoints();
        update();
    }
}