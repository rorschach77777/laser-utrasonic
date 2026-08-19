#ifndef HISTORYGRAPHITEM_H
#define HISTORYGRAPHITEM_H

#include <QQuickPaintedItem>
#include <QVector>
#include <QMutex>

class HistoryGraphItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVector<double> data READ data WRITE setData NOTIFY dataChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)

public:
    explicit HistoryGraphItem(QQuickItem *parent = nullptr);
    ~HistoryGraphItem();

    void paint(QPainter *painter) override;

    QVector<double> data() const { return m_data; }
    QString title() const { return m_title; }

    Q_INVOKABLE void setData(const QVector<double> &data);
    Q_INVOKABLE void setTitle(const QString &title);
    Q_INVOKABLE void setInterpolatedData(const QVector<double> &interpolated);

signals:
    void dataChanged();
    void titleChanged();

private:
    QVector<double> m_data;
    QVector<double> m_interpolated;
    QString m_title;
    mutable QMutex m_mutex;

    static constexpr int LEFT_MARGIN = 55;
    static constexpr int RIGHT_MARGIN = 20;
    static constexpr int TOP_MARGIN = 35;
    static constexpr int BOTTOM_MARGIN = 45;

    void updateYRange(double &yMin, double &yMax) const;
};

#endif // HISTORYGRAPHITEM_H