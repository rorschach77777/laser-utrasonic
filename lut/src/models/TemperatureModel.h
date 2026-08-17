#pragma once

#include <QObject>
#include <QList>
#include <cstdint>

class TemperatureModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QList<double> temperatures READ temperatures NOTIFY temperaturesChanged)

public:
    explicit TemperatureModel(QObject *parent = nullptr);

    QList<double> temperatures() const;

    // Set a single channel from raw INT16 register value (0.01 deg C)
    void setChannelRaw(int index, int16_t raw);

    // Set all 8 channels from raw register values at once
    void setAllChannelsRaw(const int16_t raw[8]);

    Q_INVOKABLE double channel(int index) const;

signals:
    void temperaturesChanged();

private:
    QList<double> m_temperatures;
};
