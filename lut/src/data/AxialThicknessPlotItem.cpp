#include "AxialThicknessPlotItem.h"
#include "DataController.h"
#include "../communication/AxialTailFill.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
QSGGeometryNode *makeNode(const QColor &color)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawLines);
    geometry->setLineWidth(1.5f);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}
}

AxialThicknessPlotItem::AxialThicknessPlotItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_refreshTimer.setInterval(100);
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout,
            this, &AxialThicknessPlotItem::refresh);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (isVisible())
            refresh();
    });
}

void AxialThicknessPlotItem::disconnectSource()
{
    QObject::disconnect(m_appendConnection);
    QObject::disconnect(m_resetConnection);
    QObject::disconnect(m_tailConnection);
    QObject::disconnect(m_finalConnection);
    QObject::disconnect(m_destroyConnection);
    m_appendConnection = {};
    m_resetConnection = {};
    m_tailConnection = {};
    m_finalConnection = {};
    m_destroyConnection = {};
}

void AxialThicknessPlotItem::setSource(QObject *source)
{
    if (m_source == source)
        return;
    disconnectSource();
    m_source = source;
    clear();
    if (auto *controller = qobject_cast<DataController *>(source)) {
        m_appendConnection = connect(controller, &DataController::axialPointAdded,
                                     this, &AxialThicknessPlotItem::appendPoint);
        m_resetConnection = connect(controller, &DataController::axialProfileReset,
                                    this, &AxialThicknessPlotItem::clear);
        m_tailConnection = connect(controller, &DataController::axialTailPointAdded,
                                   this, &AxialThicknessPlotItem::appendTailPoint);
        m_finalConnection = connect(controller, &DataController::axialProfileFinalized,
                                    this, &AxialThicknessPlotItem::showFinalProfile);
        m_destroyConnection = connect(controller, &QObject::destroyed,
                                      this, [this] { disconnectSource(); m_source = nullptr; });
        // One-time snapshot, e.g. when the user opens the measurement page
        // halfway through a pipe. The 100Hz path never calls this snapshot.
        for (const auto &point : controller->axialPointSnapshot())
            appendPoint(point.frameId, point.positionMm, point.thicknessMm);
        for (const auto &p : controller->axialTailSnapshot())
            appendTailPoint(p.positionMm, p.thicknessMm);
        if (controller->pipeProfileFinalized())
            showFinalProfile();
    }
    emit sourceChanged();
}

void AxialThicknessPlotItem::clear()
{
    m_refreshTimer.stop();
    m_points.clear();
    m_tailPoints.clear();
    m_finalized = false;
    emit tailPointCountChanged();
    m_xMin = 0.0;
    m_xMax = 5.0;
    m_yMin = 0.0;
    m_yMax = 1.0;
    m_lastNotifiedCount = 0;
    emit pointCountChanged();
    emit boundsChanged();
    update();
}

void AxialThicknessPlotItem::appendPoint(quint64 frameId,
                                         double positionMm,
                                         double thicknessMm)
{
    if (m_finalized || !frameId || !std::isfinite(positionMm) || positionMm < 0.0 ||
        !std::isfinite(thicknessMm))
        return;

    // Backfill revisions update the original point rather than create a
    // duplicate axial coordinate. This scan is rare for valid backfills.
    bool replaced = false;
    if (!m_points.empty() && frameId <= m_points.back().frameId) {
        for (auto it = m_points.rbegin(); it != m_points.rend(); ++it) {
            if (it->frameId == frameId) {
                *it = {frameId, positionMm, thicknessMm};
                replaced = true;
                break;
            }
        }
        if (!replaced)
            return; // stale revision beyond 2500-point retention
    }
    if (!replaced) {
        m_points.push_back({frameId, positionMm, thicknessMm});
        if (m_points.size() > MaxPoints)
            m_points.pop_front();
    }
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

void AxialThicknessPlotItem::appendTailPoint(double xMm, double hMm)
{
    if (m_finalized || !std::isfinite(xMm) || !std::isfinite(hMm) ||
        xMm < 0.0 || m_points.empty() ||
        m_tailPoints.size() >= AxialTailFill::MaxTailFrames)
        return;
    const double previousX = m_tailPoints.empty()
        ? m_points.back().x : m_tailPoints.back().first;
    if (xMm <= previousX) return;
    m_tailPoints.emplace_back(xMm, hMm);
    emit tailPointCountChanged();
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

void AxialThicknessPlotItem::showFinalProfile()
{
    auto *controller = qobject_cast<DataController *>(m_source.data());
    if (!controller || !controller->pipeProfileFinalized())
        return;
    m_refreshTimer.stop();
    m_points.clear();
    const auto finalPoints = controller->axialFinalPointSnapshot();
    for (const auto &p : finalPoints)
        m_points.push_back({p.frameId, p.positionMm, p.thicknessMm, p.interpolated});
    m_finalized = true;
    refresh(); // one final redraw, with distinct interpolation segments
}

void AxialThicknessPlotItem::recomputeBounds()
{
    if (m_points.empty()) {
        m_xMin = m_yMin = 0.0;
        m_xMax = 5.0;
        m_yMax = 1.0;
        return;
    }
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -minX;
    double minY = minX, maxY = -minX;
    for (const auto &p : m_points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    for (const auto &p : m_tailPoints) {
        minX = std::min(minX, p.first);
        maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second);
        maxY = std::max(maxY, p.second);
    }
    const double spreadX = std::max(5.0, maxX - minX);
    const double spreadY = std::max(0.05, maxY - minY);
    m_xMin = minX;
    m_xMax = minX + spreadX;
    m_yMin = minY - spreadY * 0.12;
    m_yMax = maxY + spreadY * 0.12;
}

void AxialThicknessPlotItem::refresh()
{
    recomputeBounds();
    const int count = pointCount();
    if (count != m_lastNotifiedCount) {
        m_lastNotifiedCount = count;
        emit pointCountChanged();
    }
    emit boundsChanged();
    if (isVisible())
        update(); // only ~10 calls/s; GPU line segments built on scene-graph thread
}

QSGNode *AxialThicknessPlotItem::updatePaintNode(QSGNode *oldNode,
                                                  UpdatePaintNodeData *)
{
    QSGNode *root = oldNode;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(makeNode(QColor(100, 181, 246)));
        // Small square marker makes the first (single) point visible too.
        auto *marker = makeNode(QColor(100, 181, 246));
        marker->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        root->appendChildNode(marker);
        // Estimated positions (not measured) are visually distinguishable.
        auto *tailLine = makeNode(QColor(245, 141, 37));
        tailLine->geometry()->setLineWidth(3.0f);
        root->appendChildNode(tailLine);
        auto *tailTip = makeNode(QColor(245, 141, 37));
        tailTip->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        root->appendChildNode(tailTip);
        // Internal missing thickness frames are estimated and shown separately
        // from the actual measured blue line and orange post-radar-loss tail.
        auto *interpolation = makeNode(QColor(101, 232, 219));
        interpolation->geometry()->setLineWidth(1.5f);
        root->appendChildNode(interpolation);
    }
    auto *segments = static_cast<QSGGeometryNode *>(root->firstChild());
    auto *marker = static_cast<QSGGeometryNode *>(segments->nextSibling());
    auto *estimated = static_cast<QSGGeometryNode *>(marker->nextSibling());
    auto *tailTip = static_cast<QSGGeometryNode *>(estimated->nextSibling());
    auto *interpolation = static_cast<QSGGeometryNode *>(tailTip->nextSibling());
    auto *lineGeom = segments->geometry();
    auto *markerGeom = marker->geometry();
    auto *tailGeom = estimated->geometry();
    auto *tailTipGeom = tailTip->geometry();
    auto *interpGeom = interpolation->geometry();

    const double w = width(), h = height();
    if (w <= 0 || h <= 0 || m_points.empty()) {
        lineGeom->allocate(0);
        markerGeom->allocate(0);
        tailGeom->allocate(0);
        tailTipGeom->allocate(0);
        interpGeom->allocate(0);
        segments->markDirty(QSGNode::DirtyGeometry);
        marker->markDirty(QSGNode::DirtyGeometry);
        estimated->markDirty(QSGNode::DirtyGeometry);
        tailTip->markDirty(QSGNode::DirtyGeometry);
        interpolation->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    const double xRange = std::max(1e-6, m_xMax - m_xMin);
    const double yRange = std::max(1e-6, m_yMax - m_yMin);
    const auto px = [=](double x) -> float {
        return float((x - m_xMin) / xRange * w);
    };
    const auto py = [=](double y) -> float {
        return float(h - (y - m_yMin) / yRange * h);
    };

    int measuredPairs = 0, interpolatedPairs = 0;
    for (size_t i = 1; i < m_points.size(); ++i) {
        const auto &a = m_points[i-1], &b = m_points[i];
        // Never bridge an unreconstructed position gap. The finalizer has
        // already bounded synthetic segments by gap duration and distance.
        const bool inferred = a.interpolated || b.interpolated;
        if (b.frameId != a.frameId + 1 || b.x < a.x ||
            b.x - a.x > (inferred ? 1000.0 : 25.0))
            continue;
        if (inferred) ++interpolatedPairs;
        else ++measuredPairs;
    }
    lineGeom->allocate(measuredPairs * 2);
    interpGeom->allocate(interpolatedPairs * 2);
    auto *vertices = lineGeom->vertexDataAsPoint2D();
    auto *interpolatedVertices = interpGeom->vertexDataAsPoint2D();
    int realIndex = 0, interpolatedIndex = 0;
    for (size_t i = 1; i < m_points.size(); ++i) {
        const auto &a = m_points[i-1], &b = m_points[i];
        const bool inferred = a.interpolated || b.interpolated;
        if (b.frameId != a.frameId + 1 || b.x < a.x ||
            b.x - a.x > (inferred ? 1000.0 : 25.0))
            continue;
        if (inferred) {
            interpolatedVertices[interpolatedIndex++].set(px(a.x), py(a.y));
            interpolatedVertices[interpolatedIndex++].set(px(b.x), py(b.y));
        } else {
            vertices[realIndex++].set(px(a.x), py(a.y));
            vertices[realIndex++].set(px(b.x), py(b.y));
        }
    }
    const auto &latest = m_points.back();
    const float x = px(latest.x), y = py(latest.y);
    const float radius = 2.5f;
    markerGeom->allocate(6);
    auto *dot = markerGeom->vertexDataAsPoint2D();
    dot[0].set(x-radius,y-radius);
    dot[1].set(x+radius,y-radius);
    dot[2].set(x-radius,y+radius);
    dot[3].set(x+radius,y-radius);
    dot[4].set(x+radius,y+radius);
    dot[5].set(x-radius,y+radius);
    // Draw end-only estimated values in orange, connected to the final
    // real point but never registered as a new physical measurement.
    tailGeom->allocate(static_cast<int>(m_tailPoints.size()) * 2);
    auto *tailVertices = tailGeom->vertexDataAsPoint2D();
    double previousX = latest.x, previousY = latest.y;
    int k = 0;
    for (const auto &p : m_tailPoints) {
        tailVertices[k++].set(px(previousX), py(previousY));
        tailVertices[k++].set(px(p.first), py(p.second));
        previousX = p.first;
        previousY = p.second;
    }
    if (!m_tailPoints.empty()) {
        const float tx = px(previousX), ty = py(previousY);
        constexpr float r = 4.0f;
        tailTipGeom->allocate(6);
        auto *v = tailTipGeom->vertexDataAsPoint2D();
        v[0].set(tx-r,ty-r); v[1].set(tx+r,ty-r);
        v[2].set(tx-r,ty+r); v[3].set(tx+r,ty-r);
        v[4].set(tx+r,ty+r); v[5].set(tx-r,ty+r);
    } else {
        tailTipGeom->allocate(0);
    }
    segments->markDirty(QSGNode::DirtyGeometry);
    marker->markDirty(QSGNode::DirtyGeometry);
    estimated->markDirty(QSGNode::DirtyGeometry);
    tailTip->markDirty(QSGNode::DirtyGeometry);
    interpolation->markDirty(QSGNode::DirtyGeometry);
    return root;
}
