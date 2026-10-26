#ifndef THICKNESSHISTORYITEM_H
#define THICKNESSHISTORYITEM_H

#include <QQuickPaintedItem>
#include <QVector>

class ThicknessHistoryItem : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit ThicknessHistoryItem(QQuickItem *parent = nullptr);
    ~ThicknessHistoryItem();

    void paint(QPainter *painter) override;

    Q_INVOKABLE void addThickness(double thickness);
    Q_INVOKABLE void replaceRecentThickness(int framesAgo, double thickness);
    Q_INVOKABLE void clear();

private:
    QVector<double> m_thicknesses;
    static const int MAX_POINTS = 1000;
    double m_minThickness;
    double m_maxThickness;

    void updateYRange();
};

#endif // THICKNESSHISTORYITEM_H