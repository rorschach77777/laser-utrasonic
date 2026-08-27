#pragma once

#include <QObject>
#include <cstdint>
#include "../models/WaterCoolerModel.h"

class DeviceComm;

class WaterCoolerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WaterCoolerModel* model READ model CONSTANT)

public:
    explicit WaterCoolerController(DeviceComm *comm,
                                   uint16_t statusBase,
                                   uint16_t tempSetAddr,
                                   QObject *parent = nullptr);

    WaterCoolerModel* model() const { return m_model; }
    void setModel(WaterCoolerModel *model);

    Q_INVOKABLE void readStatus();
    Q_INVOKABLE void readControl();
    Q_INVOKABLE void writeTempSet(double tempC);

signals:
    void writeDone();
    void commError(const QString &msg);

private:
    DeviceComm *m_comm = nullptr;
    WaterCoolerModel *m_model = nullptr;
    uint16_t m_statusBase = 0;
    uint16_t m_tempSetAddr = 0;
};
