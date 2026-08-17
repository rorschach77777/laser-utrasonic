#pragma once
#include <QObject>
#include "../models/SystemStatusModel.h"
#include "../models/SlaveStatusModel.h"
#include "../models/DfbModel.h"
#include "../models/MultiModePumpModel.h"
#include "../models/PumpModel.h"
#include "../models/TemperatureModel.h"

class DashboardController : public QObject {
    Q_OBJECT
    Q_PROPERTY(SystemStatusModel* systemStatus READ systemStatus CONSTANT)
    Q_PROPERTY(SlaveStatusModel* slaveStatus READ slaveStatus CONSTANT)
    Q_PROPERTY(DfbModel* dfbStatus READ dfbStatus CONSTANT)
    Q_PROPERTY(MultiModePumpModel* mpumpStatus READ mpumpStatus CONSTANT)
    Q_PROPERTY(PumpModel* pump1Status READ pump1Status CONSTANT)
    Q_PROPERTY(PumpModel* pump2Status READ pump2Status CONSTANT)
    Q_PROPERTY(TemperatureModel* temperature READ temperature CONSTANT)
    Q_PROPERTY(PumpModel* slavePump1Status READ slavePump1Status CONSTANT)
    Q_PROPERTY(PumpModel* slavePump2Status READ slavePump2Status CONSTANT)

public:
    explicit DashboardController(QObject *parent = nullptr);

    SystemStatusModel* systemStatus() const { return m_systemStatus; }
    SlaveStatusModel* slaveStatus() const { return m_slaveStatus; }
    DfbModel* dfbStatus() const { return m_dfbStatus; }
    MultiModePumpModel* mpumpStatus() const { return m_mpumpStatus; }
    PumpModel* pump1Status() const { return m_pump1Status; }
    PumpModel* pump2Status() const { return m_pump2Status; }
    TemperatureModel* temperature() const { return m_temperature; }
    PumpModel* slavePump1Status() const { return m_slavePump1Status; }
    PumpModel* slavePump2Status() const { return m_slavePump2Status; }

    void setSystemStatusModel(SystemStatusModel *model);
    void setSlaveStatusModel(SlaveStatusModel *model);
    void setDfbStatusModel(DfbModel *model);
    void setMpumpStatusModel(MultiModePumpModel *model);
    void setPump1StatusModel(PumpModel *model);
    void setPump2StatusModel(PumpModel *model);
    void setTemperatureModel(TemperatureModel *model);
    void setSlavePump1StatusModel(PumpModel *model);
    void setSlavePump2StatusModel(PumpModel *model);

private:
    SystemStatusModel *m_systemStatus = nullptr;
    SlaveStatusModel *m_slaveStatus = nullptr;
    DfbModel *m_dfbStatus = nullptr;
    MultiModePumpModel *m_mpumpStatus = nullptr;
    PumpModel *m_pump1Status = nullptr;
    PumpModel *m_pump2Status = nullptr;
    TemperatureModel *m_temperature = nullptr;
    PumpModel *m_slavePump1Status = nullptr;
    PumpModel *m_slavePump2Status = nullptr;
};
