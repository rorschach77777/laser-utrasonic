#pragma once

#include <QObject>
#include "../models/GateModel.h"

class DeviceComm;

class GateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(GateModel* model READ model CONSTANT)

public:
    explicit GateController(DeviceComm *comm,
                            uint16_t statusBase,
                            uint16_t controlBase,
                            uint16_t enableAddr,
                            uint16_t riseDelayAddr,
                            uint16_t fallDelayAddr,
                            uint16_t polarityAddr,
                            QObject *parent = nullptr);

    GateModel* model() const { return m_model; }
    void setModel(GateModel *model);

    Q_INVOKABLE void readStatus();
    Q_INVOKABLE void readControl();
    Q_INVOKABLE void writeGateEnable(bool enable);
    Q_INVOKABLE void writeRiseDelayUs(int delayUs);
    Q_INVOKABLE void writeFallDelayUs(int delayUs);
    Q_INVOKABLE void writePolarity(int polarity);

signals:
    void writeDone();
    void commError(const QString &msg);

private:
    DeviceComm *m_comm = nullptr;
    GateModel *m_model = nullptr;
    uint16_t m_statusBase = 0;
    uint16_t m_controlBase = 0;
    uint16_t m_enableAddr = 0;
    uint16_t m_riseDelayAddr = 0;
    uint16_t m_fallDelayAddr = 0;
    uint16_t m_polarityAddr = 0;
};
