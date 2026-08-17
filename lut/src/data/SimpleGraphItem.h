#ifndef SIMPLEGRAPHITEM_H
#define SIMPLEGRAPHITEM_H

#include <QQuickPaintedItem>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QMutex>

enum TimeUnit { Nanosecond, Microsecond, Millisecond, Second };
enum ValueUnit { Volt, Millivolt, Microvolt, Ampere, Milliampere, Microampere, CelsiusUnit };

class SimpleGraphItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY yRangeChanged)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY yRangeChanged)
    Q_PROPERTY(double xCordTimeDiv READ xCordTimeDiv WRITE setXCordTimeDiv NOTIFY xCordChanged)
    Q_PROPERTY(double xCordTimeOffset READ xCordTimeOffset WRITE setXCordTimeOffset NOTIFY xCordChanged)

public:
    explicit SimpleGraphItem(QQuickItem *parent = nullptr);
    ~SimpleGraphItem();

    void paint(QPainter *painter) override;

    Q_INVOKABLE void chart(const QVector<double> &data, int plotCount, int pointsPerChan,
                           double xInc, bool recordEnd);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void addMarker(double xTimeSec, double yValue);
    Q_INVOKABLE void clearMarkers();

    bool autoScale() const { return m_autoScale; }
    double yMin() const { return m_yCordRangeMin; }
    double yMax() const { return m_yCordRangeMax; }
    double xCordTimeDiv() const { return m_xCordTimeDiv; }
    double xCordTimeOffset() const { return m_xCordTimeOffset; }

    Q_INVOKABLE void setAutoScale(bool enable);
    Q_INVOKABLE void setYMin(double min);
    Q_INVOKABLE void setYMax(double max);
    Q_INVOKABLE void setYAxisRange(double min, double max);
    Q_INVOKABLE void setXCordTimeDiv(double div);
    Q_INVOKABLE void setXCordTimeOffset(double offset);

    Q_INVOKABLE void setTimeUnit(int unit) { m_timeUnit = static_cast<TimeUnit>(unit); }
    Q_INVOKABLE void setYUnit(int unit) { m_yUnit = static_cast<ValueUnit>(unit); }

signals:
    void autoScaleChanged();
    void yRangeChanged();
    void xCordChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void calcDrawParams(double xIncBySec);
    void mapDataPoints();
    void saveData(const QVector<double> &data, int plotCount, int dataCountPerPlot);
    void updateYAxisRangeFromData();
    void applyYAxisRange();

    static constexpr int LEFT_MARGIN = 50;
    static constexpr int RIGHT_MARGIN = 15;
    static constexpr int TOP_MARGIN = 15;
    static constexpr int BOTTOM_MARGIN = 35;

    int m_plotCount;
    int m_pointCountPerScreen;
    int m_copyDataCountPerChan;
    double m_xIncByTime;
    double m_xCordDividedRate;
    int m_dataCountCachePerPlot;
    int m_mapDataIndexPerPlot;
    int m_dataCountPerPlot;
    double m_shiftCount;
    bool m_recordEndFlag;
    int m_xPosition;
    bool m_drawRecordEnd;
    int m_recordEndPointX;
    bool m_autoScale;

    TimeUnit m_timeUnit;
    ValueUnit m_yUnit;
    double m_yCordRangeMax;
    double m_yCordRangeMin;
    double m_xCordTimeDiv;
    double m_xCordTimeOffset;

    std::vector<double> m_drawDataBuffer;
    int m_drawDataBufferLength;
    std::vector<QPoint> m_dataPointBuffer;

    std::vector<QPointF> m_markers;
    QMutex m_mutex;

    static QColor s_lineColor[16];
};

#endif // SIMPLEGRAPHITEM_H