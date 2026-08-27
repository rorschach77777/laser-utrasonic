#pragma once
#include <QObject>
#include <QTimer>
#include <QQueue>
#include <functional>
#include <cstdint>

#include "../models/SystemStatusModel.h"
#include "../models/SlaveStatusModel.h"
#include "../models/DfbModel.h"
#include "../models/GateModel.h"
#include "../models/MultiModePumpModel.h"
#include "../models/PumpModel.h"
#include "../models/TemperatureModel.h"
#include "../models/WaterCoolerModel.h"
#include "../models/PulseChannelModel.h"

class DeviceComm;

class PollingService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit PollingService(DeviceComm *comm, QObject *parent = nullptr);

    bool active() const { return m_active; }

    // Model accessors (PollingService owns all models)
    SystemStatusModel*  systemStatusModel()  const { return m_sysStatus; }
    SlaveStatusModel*   slaveStatusModel()   const { return m_slaveStatus; }
    DfbModel*           dfbModel()           const { return m_dfb; }
    GateModel*          gateModel()          const { return m_gate; }
    GateModel*          slaveGateModel()     const { return m_slaveGate; }
    MultiModePumpModel* mpumpModel()         const { return m_mpump; }
    PumpModel*          pump1Model()         const { return m_pump1; }
    PumpModel*          pump2Model()         const { return m_pump2; }
    PumpModel*          slavePump1Model()    const { return m_slavePump1; }
    PumpModel*          slavePump2Model()    const { return m_slavePump2; }
    TemperatureModel*   temperatureModel()   const { return m_temperature; }
    WaterCoolerModel*   waterCoolerModel()   const { return m_waterCooler; }
    WaterCoolerModel*   slaveWaterCoolerModel() const { return m_slaveWaterCooler; }
    PulseTimingModel*   mainTimingModel()    const { return m_mainTiming; }
    PulseTimingModel*   slaveTimingModel()   const { return m_slaveTiming; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void activeChanged();

private:
    void onTick();
    void processQueue();

    // Parsing helpers
    void parseSystemStatus(const QVector<uint16_t> &regs);
    void parseSlaveAndDeviceStatus(const QVector<uint16_t> &regs);
    void parsePumpStatus(PumpModel *model, const QVector<uint16_t> &regs, int offset);
    void parseTemperature(const QVector<uint16_t> &regs);
    void parseSlavePumps(const QVector<uint16_t> &regs);
    void parseWaterCoolerStatus(WaterCoolerModel *model, const QVector<uint16_t> &regs);
    void parseWaterCoolerControl(WaterCoolerModel *model, const QVector<uint16_t> &regs);
    void parseDfbControl(const QVector<uint16_t> &regs);
    void parseGateControl(GateModel *model, const QVector<uint16_t> &regs);
    void parseMpumpControl(const QVector<uint16_t> &regs);
    void parsePumpControl(PumpModel *model, const QVector<uint16_t> &regs);
    void parseSlaveHoldingPumps(const QVector<uint16_t> &regs);
    void parseTimingBlock(PulseTimingModel *model, const QVector<uint16_t> &regs, int chOffset);

    struct PendingRead {
        enum Type { InputRegs, HoldingRegs };
        Type type;
        uint16_t startAddr;
        uint16_t count;
        std::function<void(const QVector<uint16_t>&)> parser;
    };

    void enqueue(PendingRead::Type type, uint16_t addr, uint16_t count,
                 std::function<void(const QVector<uint16_t>&)> parser);

    DeviceComm *m_comm = nullptr;
    QTimer m_tickTimer;
    uint32_t m_tickCount = 0;
    bool m_active = false;
    bool m_readInFlight = false;

    QQueue<PendingRead> m_readQueue;

    // Owned models
    SystemStatusModel  *m_sysStatus   = nullptr;
    SlaveStatusModel   *m_slaveStatus = nullptr;
    DfbModel           *m_dfb         = nullptr;
    GateModel          *m_gate        = nullptr;
    GateModel          *m_slaveGate   = nullptr;
    MultiModePumpModel *m_mpump       = nullptr;
    PumpModel          *m_pump1       = nullptr;
    PumpModel          *m_pump2       = nullptr;
    PumpModel          *m_slavePump1  = nullptr;
    PumpModel          *m_slavePump2  = nullptr;
    TemperatureModel   *m_temperature = nullptr;
    WaterCoolerModel   *m_waterCooler = nullptr;
    WaterCoolerModel   *m_slaveWaterCooler = nullptr;
    PulseTimingModel   *m_mainTiming  = nullptr;
    PulseTimingModel   *m_slaveTiming = nullptr;
};
