#pragma once
#include <QObject>
#include <QVariant>
#include <cstdint>
#include "../models/PulseChannelModel.h"

class DeviceComm;

class TimingController : public QObject {
    Q_OBJECT
    Q_PROPERTY(PulseTimingModel* timingModel READ timingModel CONSTANT)

public:
    explicit TimingController(DeviceComm *comm, uint16_t baseAddr, QObject *parent = nullptr);

    PulseTimingModel* timingModel() const { return m_model; }
    void setModel(PulseTimingModel *model);

    Q_INVOKABLE void readChannel(int ch);
    Q_INVOKABLE void writeChannel(int ch);
    Q_INVOKABLE void writeChannelRegisters(int ch, const QVariantList &values);

signals:
    void commError(const QString &msg);

private:
    uint16_t channelAddr(int ch) const;

    DeviceComm *m_comm = nullptr;
    PulseTimingModel *m_model = nullptr;
    uint16_t m_baseAddr;
};
