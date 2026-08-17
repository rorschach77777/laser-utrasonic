#pragma once
#include <QObject>
#include <cstdint>
#include "../models/PumpModel.h"

class DeviceComm;

class PumpController : public QObject {
    Q_OBJECT
    Q_PROPERTY(PumpModel* model READ model CONSTANT)

public:
    explicit PumpController(DeviceComm *comm, uint16_t holdingBase, uint16_t inputBase,
                            QObject *parent = nullptr);

    PumpModel* model() const { return m_model; }
    void setModel(PumpModel *model);

    Q_INVOKABLE void readStatus();
    Q_INVOKABLE void readControl();
    Q_INVOKABLE void writeEnable(bool on);
    Q_INVOKABLE void writeMode(int mode);
    Q_INVOKABLE void writeCurrent(int amps);
    Q_INVOKABLE void writePulseWidth(int us);
    Q_INVOKABLE void writeVoltageLimit(int volts);
    Q_INVOKABLE void writeIntFreq(int hz);
    Q_INVOKABLE void writeExtDelay(int us);
    Q_INVOKABLE void writeDelayOut(int us);
    Q_INVOKABLE void writeAddr(int addr);

signals:
    void writeDone();
    void commError(const QString &msg);

private:
    void writeSingleParam(uint16_t offset, uint16_t value, const QString &paramName);

    DeviceComm *m_comm = nullptr;
    PumpModel *m_model = nullptr;
    uint16_t m_holdingBase;
    uint16_t m_inputBase;
    bool m_useFallback = false;
};
