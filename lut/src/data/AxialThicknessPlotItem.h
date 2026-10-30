#pragma once

#include <QPointer>
#include "../communication/LstDataExport.h"
#include <QQuickItem>
#include <QTimer>
#include <deque>
#include <vector>
#include <utility>

// GPU scene-graph plot for the live pipe-axis thickness trend.
// 100Hz measurements enter one-by-one in GUI-thread C++; only one scene
// graph update is scheduled per ~100ms. No QVariantMap list / QML Canvas.
class LST_DATA_API AxialThicknessPlotItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(int tailPointCount READ tailPointCount NOTIFY tailPointCountChanged)
    Q_PROPERTY(double xMin READ xMin NOTIFY boundsChanged)
    Q_PROPERTY(double xMax READ xMax NOTIFY boundsChanged)
    Q_PROPERTY(double yMin READ yMin NOTIFY boundsChanged)
    Q_PROPERTY(double yMax READ yMax NOTIFY boundsChanged)

public:
    explicit AxialThicknessPlotItem(QQuickItem *parent = nullptr);
    QObject *source() const { return m_source.data(); }
    void setSource(QObject *source);
    int pointCount() const { return static_cast<int>(m_points.size()); }
    int tailPointCount() const { return static_cast<int>(m_tailPoints.size()); }
    double xMin() const { return m_xMin; }
    double xMax() const { return m_xMax; }
    double yMin() const { return m_yMin; }
    double yMax() const { return m_yMax; }
    Q_INVOKABLE void clear();
    Q_INVOKABLE void appendPoint(quint64 frameId, double positionMm, double thicknessMm);
    Q_INVOKABLE void appendTailPoint(double positionMm, double thicknessMm);
    void showFinalProfile();

signals:
    void sourceChanged();
    void pointCountChanged();
    void tailPointCountChanged();
    void boundsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    struct Point { quint64 frameId; double x; double y; bool interpolated = false; };
    static constexpr size_t MaxPoints = 2500;
    void refresh();
    void recomputeBounds();
    void disconnectSource();

    QPointer<QObject> m_source;
    QMetaObject::Connection m_appendConnection;
    QMetaObject::Connection m_resetConnection;
    QMetaObject::Connection m_tailConnection;
    QMetaObject::Connection m_finalConnection;
    QMetaObject::Connection m_destroyConnection;
    std::deque<Point> m_points;
    std::vector<std::pair<double,double>> m_tailPoints;
    QTimer m_refreshTimer;
    double m_xMin = 0.0;
    double m_xMax = 5.0;
    double m_yMin = 0.0;
    double m_yMax = 1.0;
    int m_lastNotifiedCount = 0;
    bool m_finalized = false;
};
