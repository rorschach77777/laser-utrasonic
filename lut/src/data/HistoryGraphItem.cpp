#include "HistoryGraphItem.h"
#include <QPainter>
#include <QFontMetrics>
#include <cmath>

HistoryGraphItem::HistoryGraphItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setFillColor(Qt::black);
}

HistoryGraphItem::~HistoryGraphItem()
{
}

void HistoryGraphItem::setData(const QVector<double> &data)
{
    QMutexLocker locker(&m_mutex);
    m_data = data;
    locker.unlock();
    emit dataChanged();
    update();
}

void HistoryGraphItem::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit titleChanged();
        update();
    }
}

void HistoryGraphItem::setInterpolatedData(const QVector<double> &interpolated)
{
    QMutexLocker locker(&m_mutex);
    m_interpolated = interpolated;
    locker.unlock();
    update();
}

void HistoryGraphItem::updateYRange(double &yMin, double &yMax) const
{
    bool first = true;
    yMin = 0.0;
    yMax = 1.0;

    for (double val : m_data) {
        if (std::isnan(val)) continue;
        if (first) {
            yMin = yMax = val;
            first = false;
        } else {
            if (val < yMin) yMin = val;
            if (val > yMax) yMax = val;
        }
    }

    if (first) {
        yMin = -0.5;
        yMax = 0.5;
        return;
    }

    if (yMin > 0) yMin = 0;
    if (yMax < 0) yMax = 0;
    double margin = (yMax - yMin) * 0.1;
    if (margin < 0.1) margin = 0.1;
    yMin -= margin;
    yMax += margin;
}

void HistoryGraphItem::paint(QPainter *painter)
{
    QMutexLocker locker(&m_mutex);
    painter->setRenderHint(QPainter::Antialiasing);

    int w = (int)width();
    int h = (int)height();
    if (w <= 0 || h <= 0) return;

    // Background
    painter->fillRect(QRect(0, 0, w, h), Qt::black);

    if (m_data.isEmpty()) {
        painter->setPen(Qt::gray);
        painter->drawText(QRect(0, 0, w, h), Qt::AlignCenter, tr("No data"));
        return;
    }

    int plotLeft = LEFT_MARGIN;
    int plotRight = w - RIGHT_MARGIN;
    int plotTop = TOP_MARGIN;
    int plotBottom = h - BOTTOM_MARGIN;
    int plotWidth = plotRight - plotLeft;
    int plotHeight = plotBottom - plotTop;

    double yMin, yMax;
    updateYRange(yMin, yMax);
    double yRange = yMax - yMin;

    // Draw title
    if (!m_title.isEmpty()) {
        painter->setPen(Qt::white);
        QFont font = painter->font();
        font.setPointSize(12);
        font.setBold(true);
        painter->setFont(font);
        painter->drawText(LEFT_MARGIN, 22, m_title);
    }

    // Grid
    painter->setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
    for (int i = 1; i <= 6; ++i) {
        int y = plotTop + i * plotHeight / 7;
        painter->drawLine(plotLeft, y, plotRight, y);
    }
    for (int i = 1; i <= 10; ++i) {
        int x = plotLeft + i * plotWidth / 11;
        painter->drawLine(x, plotTop, x, plotBottom);
    }

    // Axis
    painter->setPen(QPen(Qt::white, 1));
    painter->drawLine(plotLeft, plotTop, plotLeft, plotBottom);
    painter->drawLine(plotLeft, plotBottom, plotRight, plotBottom);

    // Y-axis labels
    QFont font = painter->font();
    font.setPointSize(10);
    painter->setFont(font);
    painter->setPen(Qt::white);
    for (int i = 0; i <= 6; ++i) {
        double yVal = yMin + i * yRange / 6.0;
        int y = plotBottom - (int)((yVal - yMin) / yRange * plotHeight);
        painter->drawText(5, y - 4, 40, 16, Qt::AlignRight, QString::number(yVal, 'f', 2));
    }

    // X-axis label
    painter->drawText(plotLeft + plotWidth / 2 - 30, h - 8, tr("Point Index"));

    // Calculate step with sampling if too many points
    int nPoints = m_interpolated.isEmpty() ? m_data.size() : m_interpolated.size();
    const QVector<double> &drawData = m_interpolated.isEmpty() ? m_data : m_interpolated;
    int sampleStep = 1;
    if (nPoints > 2000) {
        sampleStep = (nPoints + plotWidth - 1) / plotWidth;
    }
    int sampledPoints = (nPoints + sampleStep - 1) / sampleStep;
    double xStep = (double)plotWidth / (sampledPoints - 1);

    // Draw line
    QVector<QPoint> points;
    for (int i = 0; i < nPoints; i += sampleStep) {
        double val = drawData[i];
        if (std::isnan(val)) continue;
        int x = plotLeft + (int)((i / sampleStep) * xStep);
        int y = plotBottom - (int)((val - yMin) / yRange * plotHeight);
        y = qBound(plotTop, y, plotBottom);
        points.append(QPoint(x, y));
    }

    if (points.size() >= 2) {
        painter->setPen(QPen(QColor("#00E676"), 2));
        painter->drawPolyline(points);
    }

    // X-axis tick labels at major divisions
    int majorTicks = 10;
    for (int i = 0; i <= majorTicks; ++i) {
        int pointIdx = (nPoints - 1) * i / majorTicks;
        int x = plotLeft + (int)((double)pointIdx / (nPoints - 1) * plotWidth);
        int val = pointIdx + 1; // 1-based
        painter->drawText(x - 15, plotBottom + 18, 30, 12, Qt::AlignHCenter, QString::number(val));
    }

    // Y unit
    painter->drawText(LEFT_MARGIN + 5, TOP_MARGIN - 5, tr("(mm)"));
}
