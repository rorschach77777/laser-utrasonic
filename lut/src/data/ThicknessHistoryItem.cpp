#include "ThicknessHistoryItem.h"
#include <QPainter>
#include <cmath>

ThicknessHistoryItem::ThicknessHistoryItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_minThickness(0)
    , m_maxThickness(1)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setFillColor(Qt::black);
}

ThicknessHistoryItem::~ThicknessHistoryItem()
{
}

void ThicknessHistoryItem::updateYRange()
{
    if (m_thicknesses.isEmpty()) {
        m_minThickness = 0;
        m_maxThickness = 1;
        return;
    }

    double minVal = 0.0, maxVal = 0.0;
    bool first = true;
    for (double v : m_thicknesses) {
        if (std::isnan(v)) continue;
        if (first) { minVal = maxVal = v; first = false; }
        else {
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }
    }
    if (first) { m_minThickness = -0.5; m_maxThickness = 0.5; return; }
    if (minVal > 0) minVal = 0;
    if (maxVal < 0) maxVal = 0;
    double margin = (maxVal - minVal) * 0.1;
    if (margin < 0.1) margin = 0.1;
    m_minThickness = minVal;
    m_maxThickness = maxVal + margin;
}

void ThicknessHistoryItem::addThickness(double thickness)
{
    if (m_thicknesses.size() >= MAX_POINTS)
        m_thicknesses.removeFirst();
    m_thicknesses.append(thickness);
    updateYRange();
    update();
}

void ThicknessHistoryItem::clear()
{
    m_thicknesses.clear();
    updateYRange();
    update();
}

void ThicknessHistoryItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);

    int w = (int)width();
    int h = (int)height();
    if (w <= 0 || h <= 0) return;

    painter->fillRect(QRect(0, 0, w, h), Qt::black);

    if (m_thicknesses.isEmpty()) {
        painter->setPen(Qt::gray);
        painter->drawText(QRect(0, 0, w, h), Qt::AlignCenter, "No thickness data");
        return;
    }

    int n = m_thicknesses.size();
    int leftMargin = 45;
    int topMargin = 10;
    int bottomMargin = 40;
    int plotWidth = w - leftMargin - 10;
    int plotHeight = h - topMargin - bottomMargin;

    double xStep = plotWidth / (double)(MAX_POINTS - 1);
    double yRange = m_maxThickness - m_minThickness;
    if (yRange < 0.001) yRange = 1.0;

    // Grid
    painter->setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
    for (int i = 1; i <= 6; ++i) {
        double yVal = m_minThickness + i * yRange / 6.0;
        int y = topMargin + plotHeight - (int)((yVal - m_minThickness) / yRange * plotHeight);
        painter->drawLine(leftMargin, y, w - 10, y);
    }

    // Axis
    painter->setPen(QPen(Qt::white, 1));
    painter->drawLine(leftMargin, topMargin, leftMargin, topMargin + plotHeight);
    painter->drawLine(leftMargin, topMargin + plotHeight, w - 10, topMargin + plotHeight);

    // Y-axis labels
    QFont font = painter->font();
    font.setPointSize(10);
    painter->setFont(font);
    painter->setPen(Qt::white);
    for (int i = 0; i <= 6; ++i) {
        double yVal = m_minThickness + i * yRange / 6.0;
        int y = topMargin + plotHeight - (int)((yVal - m_minThickness) / yRange * plotHeight);
        painter->drawText(leftMargin - 35, y - 6, 35, 16, Qt::AlignRight, QString::number(yVal, 'f', 2));
    }

    // X-axis labels
    int startIndex = qMax(0, n - MAX_POINTS);
    for (int i = 0; i <= 10; ++i) {
        int seq = startIndex + i * (MAX_POINTS / 10);
        int x = leftMargin + i * plotWidth / 10;
        painter->drawText(x - 20, topMargin + plotHeight + 15, 40, 12, Qt::AlignHCenter, QString::number(seq));
    }

    // Data points
    for (int i = 0; i < n; ++i) {
        double val = m_thicknesses[i];
        int x = leftMargin + (int)(i * xStep);
        int y;
        bool valid = !std::isnan(val);
        if (valid) {
            y = topMargin + plotHeight - (int)((val - m_minThickness) / yRange * plotHeight);
        } else {
            y = topMargin + plotHeight - (int)((0.0 - m_minThickness) / yRange * plotHeight);
        }
        if (y < topMargin) y = topMargin;
        if (y > topMargin + plotHeight) y = topMargin + plotHeight;
        if (valid) {
            painter->setPen(Qt::green);
            painter->setBrush(QBrush(Qt::green));
            painter->drawEllipse(QPoint(x, y), 4, 4);
        } else {
            painter->setPen(QPen(Qt::red, 2));
            painter->setBrush(Qt::NoBrush);
            int r = 5;
            painter->drawLine(x - r, y - r, x + r, y + r);
            painter->drawLine(x + r, y - r, x - r, y + r);
        }
    }
}