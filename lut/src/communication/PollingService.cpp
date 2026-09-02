#include "PollingService.h"
#include "DeviceComm.h"
#include "../models/RegisterDefs.h"

namespace {
constexpr auto kFieldDfbSwitch = "dfbSwitch";
constexpr auto kFieldLdSwitch = "ldSwitch";
constexpr auto kFieldTempTarget = "tempTarget";
constexpr auto kFieldDriveMode = "driveMode";
constexpr auto kFieldCurrentSet = "currentSet";
constexpr auto kFieldCurrentMax = "currentMax";

constexpr auto kFieldGateEnable = "gateEnable";
constexpr auto kFieldRiseDelayUs = "riseDelayUs";
constexpr auto kFieldFallDelayUs = "fallDelayUs";
constexpr auto kFieldPolarity = "polarity";

constexpr auto kFieldPumpSwitch = "pumpSwitch";
constexpr auto kFieldMpumpDriveMode = "driveMode";
constexpr auto kFieldMpumpCurrentSet = "currentSet";

constexpr auto kFieldEnable = "enable";
constexpr auto kFieldMode = "mode";
constexpr auto kFieldCurrent = "current";
constexpr auto kFieldPulseWidth = "pulseWidth";
constexpr auto kFieldVoltageLimit = "voltageLimit";
constexpr auto kFieldIntFreq = "intFreq";
constexpr auto kFieldExtDelay = "extDelay";
constexpr auto kFieldDelayOut = "delayOut";
constexpr auto kFieldAddr = "addr";
}

PollingService::PollingService(DeviceComm *comm, QObject *parent)
    : QObject(parent)
    , m_comm(comm)
    , m_sysStatus(new SystemStatusModel(this))
    , m_slaveStatus(new SlaveStatusModel(this))
    , m_dfb(new DfbModel(this))
    , m_gate(new GateModel(this))
    , m_slaveGate(new GateModel(this))
    , m_mpump(new MultiModePumpModel(this))
    , m_pump1(new PumpModel(this))
    , m_pump2(new PumpModel(this))
    , m_slavePump1(new PumpModel(this))
    , m_slavePump2(new PumpModel(this))
    , m_temperature(new TemperatureModel(this))
    , m_waterCooler(new WaterCoolerModel(this))
    , m_slaveWaterCooler(new WaterCoolerModel(this))
    , m_mainTiming(new PulseTimingModel(this))
    , m_slaveTiming(new PulseTimingModel(this))
{
    connect(&m_tickTimer, &QTimer::timeout, this, &PollingService::onTick);
}

void PollingService::start()
{
    if (m_active) return;
    m_active = true;
    m_tickCount = 0;
    m_readQueue.clear();
    m_readInFlight = false;
    emit activeChanged();

    // Execute first tick immediately
    onTick();
    m_tickTimer.start(500);
}

void PollingService::stop()
{
    if (!m_active) return;
    m_tickTimer.stop();
    m_readQueue.clear();
    m_readInFlight = false;
    m_active = false;
    emit activeChanged();
}

void PollingService::enqueue(PendingRead::Type type, uint16_t addr, uint16_t count,
                             std::function<void(const QVector<uint16_t>&)> parser)
{
    m_readQueue.enqueue({type, addr, count, std::move(parser)});
}

void PollingService::onTick()
{
    // If previous round hasn't finished, skip this tick (bus overload protection)
    if (m_readInFlight || !m_readQueue.isEmpty()) return;

    m_tickCount++;

    // === Every tick (500ms): System status ===
    enqueue(PendingRead::InputRegs, Reg::Input::SYS_STATUS_BASE, Reg::Input::SYS_STATUS_COUNT,
            [this](const QVector<uint16_t> &r) { parseSystemStatus(r); });

    // === Every 2nd tick (1000ms): Device status + Temperature + Holding readback ===
    if (m_tickCount % 2 == 0) {
        // Slave + DFB + MPump + Pump1 + Pump2 status (input 0x0020, 80 regs)
        enqueue(PendingRead::InputRegs, Reg::Input::SLAVE_STATUS_BASE, 80,
                [this](const QVector<uint16_t> &r) { parseSlaveAndDeviceStatus(r); });

        // Temperature (input 0x0080, 8 regs)
        enqueue(PendingRead::InputRegs, Reg::Input::TEMP_BASE, Reg::Input::TEMP_COUNT,
                [this](const QVector<uint16_t> &r) { parseTemperature(r); });

        // Slave pump1+2 status (input 0x0400, 32 regs)
        enqueue(PendingRead::InputRegs, Reg::Input::SLAVE_PUMP1_STATUS_BASE, 32,
                [this](const QVector<uint16_t> &r) { parseSlavePumps(r); });

        // Main / slave water cooler status
        enqueue(PendingRead::InputRegs,
                Reg::Input::WATER_COOLER_STATUS_BASE,
                Reg::Input::WATER_COOLER_STATUS_COUNT,
                [this](const QVector<uint16_t> &r) { parseWaterCoolerStatus(m_waterCooler, r); });
        enqueue(PendingRead::InputRegs,
                Reg::Input::SLAVE_WATER_COOLER_STATUS_BASE,
                Reg::Input::SLAVE_WATER_COOLER_STATUS_COUNT,
                [this](const QVector<uint16_t> &r) { parseWaterCoolerStatus(m_slaveWaterCooler, r); });

        // Water cooler target-temperature readback
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::WATER_COOLER_TEMP_SET,
                1,
                [this](const QVector<uint16_t> &r) { parseWaterCoolerControl(m_waterCooler, r); });
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::SLAVE_WATER_COOLER_TEMP_SET,
                1,
                [this](const QVector<uint16_t> &r) { parseWaterCoolerControl(m_slaveWaterCooler, r); });

        // Holding readback: DFB control (0x0030, 6 regs)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::DFB_CTRL_BASE, 6,
                [this](const QVector<uint16_t> &r) { parseDfbControl(r); });

        // Holding readback: Gate control (0x0017-0x001F)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::GATE_CTRL_BASE, Reg::Hold::GATE_CTRL_COUNT,
                [this](const QVector<uint16_t> &r) { parseGateControl(m_gate, r); });

        // Holding readback: MultiMode pump control (0x0040, 3 regs)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::MPUMP_CTRL_BASE, 3,
                [this](const QVector<uint16_t> &r) { parseMpumpControl(r); });

        // Holding readback: Slave Gate control (0x0027-0x002F)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::SLAVE_GATE_CTRL_BASE, Reg::Hold::SLAVE_GATE_CTRL_COUNT,
                [this](const QVector<uint16_t> &r) { parseGateControl(m_slaveGate, r); });

        // Holding readback: Pump1 control (0x0050, 9 regs)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::PUMP1_CTRL_BASE, 9,
                [this](const QVector<uint16_t> &r) { parsePumpControl(m_pump1, r); });

        // Holding readback: Pump2 control (0x0060, 9 regs)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::PUMP2_CTRL_BASE, 9,
                [this](const QVector<uint16_t> &r) { parsePumpControl(m_pump2, r); });

        // Holding readback: Slave pump1+2 control (0x0400, 32 regs)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::SLAVE_PUMP1_CTRL_BASE, 32,
                [this](const QVector<uint16_t> &r) { parseSlaveHoldingPumps(r); });
    }

    // === Every 10th tick (5000ms): Timing configurations ===
    if (m_tickCount % 10 == 0) {
        // Main timing: 3 batch reads (ch0-5, ch6-11, ch12-15)
        enqueue(PendingRead::HoldingRegs, Reg::Hold::MAIN_TIMING_BASE, 108,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_mainTiming, r, 0); });
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::MAIN_TIMING_BASE + 6 * Reg::Hold::TIMING_CH_STRIDE, 108,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_mainTiming, r, 6); });
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::MAIN_TIMING_BASE + 12 * Reg::Hold::TIMING_CH_STRIDE, 72,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_mainTiming, r, 12); });

        // Slave timing: 3 batch reads
        enqueue(PendingRead::HoldingRegs, Reg::Hold::SLAVE_TIMING_BASE, 108,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_slaveTiming, r, 0); });
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::SLAVE_TIMING_BASE + 6 * Reg::Hold::TIMING_CH_STRIDE, 108,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_slaveTiming, r, 6); });
        enqueue(PendingRead::HoldingRegs,
                Reg::Hold::SLAVE_TIMING_BASE + 12 * Reg::Hold::TIMING_CH_STRIDE, 72,
                [this](const QVector<uint16_t> &r) { parseTimingBlock(m_slaveTiming, r, 12); });
    }

    processQueue();
}

void PollingService::processQueue()
{
    if (m_readInFlight || m_readQueue.isEmpty()) return;

    auto req = m_readQueue.dequeue();
    m_readInFlight = true;

    auto callback = [this, parser = std::move(req.parser)](const ModbusResponse &resp) {
        m_readInFlight = false;
        if (resp.valid && !resp.isException()) {
            parser(resp.registers);
        }
        // Continue processing queue regardless of success/failure
        processQueue();
    };

    if (req.type == PendingRead::InputRegs) {
        m_comm->readInputRegisters(req.startAddr, req.count, callback);
    } else {
        m_comm->readHoldingRegisters(req.startAddr, req.count, callback);
    }
}

// ============================================================
// Parsing methods
// ============================================================

void PollingService::parseSystemStatus(const QVector<uint16_t> &regs)
{
    if (regs.size() < 16) return;

    m_sysStatus->setSysStatus(regs[0]);        // 0x0010
    m_sysStatus->setSysFault(regs[1]);          // 0x0011
    m_gate->setGateLevel(regs[2]);              // 0x0012
    m_gate->setGateState(regs[3]);              // 0x0013
    m_sysStatus->setMainSyncStep(regs[8]);      // 0x0018
    m_sysStatus->setMainSyncStatus(regs[9]);    // 0x0019
    uint32_t tdMeas = (static_cast<uint32_t>(regs[10]) << 16) | regs[11];
    m_sysStatus->setMainTdMeasured(tdMeas);     // 0x001A-0x001B
}

void PollingService::parseSlaveAndDeviceStatus(const QVector<uint16_t> &regs)
{
    if (regs.size() < 80) return;

    // --- Slave Status (offset 0-15, addresses 0x0020-0x002F) ---
    m_slaveStatus->setOnline(regs[0] != 0);
    m_slaveStatus->setSysStatus(regs[1]);
    m_slaveStatus->setSysFault(regs[2]);
    m_slaveGate->setGateLevel(regs[3]);
    m_slaveGate->setGateState(regs[4]);
    m_slaveStatus->setSyncStep(regs[8]);
    m_slaveStatus->setSyncStatusCode(regs[9]);
    m_slaveStatus->setSyncErrorCount(regs[10]);
    uint32_t slaveUptime = (static_cast<uint32_t>(regs[14]) << 16) | regs[15];
    m_slaveStatus->setUptimeSeconds(slaveUptime);

    // --- DFB Status (offset 16-31, addresses 0x0030-0x003F) ---
    m_dfb->setOnline(regs[16] != 0);
    m_dfb->setFault(regs[17]);
    m_dfb->setTecStatus(regs[18]);
    m_dfb->setTemperatureRaw(regs[19]);
    m_dfb->setCurrentRaw(regs[20]);
    m_dfb->setPdVoltageRaw(regs[21]);

    // --- MultiMode Pump Status (offset 32-47, addresses 0x0040-0x004F) ---
    m_mpump->setOnline(regs[32] != 0);
    m_mpump->setFault(regs[33]);
    m_mpump->setCurrentActual(regs[34]);

    // --- Pump1 Status (offset 48-63, addresses 0x0050-0x005F) ---
    parsePumpStatus(m_pump1, regs, 48);

    // --- Pump2 Status (offset 64-79, addresses 0x0060-0x006F) ---
    parsePumpStatus(m_pump2, regs, 64);
}

void PollingService::parsePumpStatus(PumpModel *model, const QVector<uint16_t> &regs, int offset)
{
    if (regs.size() < offset + 13) return;

    model->setOnline(regs[offset + 0] != 0);
    model->setEnableRb(regs[offset + 1] != 0);
    model->setStatusCode(regs[offset + 2]);
    model->setAddrRb(regs[offset + 3]);
    model->setCurrentActual(regs[offset + 4]);
    model->setVoltageActualRaw(regs[offset + 5]);
    model->setFreqActual(regs[offset + 6]);
    model->setCurrentRb(regs[offset + 7]);
    model->setPulseWidthRb(regs[offset + 8]);
    model->setVoltageLimitRb(regs[offset + 9]);
    model->setIntFreqRb(regs[offset + 10]);
    model->setExtDelayRb(regs[offset + 11]);
    model->setDelayOutRb(regs[offset + 12]);
}

void PollingService::parseTemperature(const QVector<uint16_t> &regs)
{
    int count = qMin(regs.size(), qsizetype(8));
    for (int i = 0; i < count; ++i) {
        m_temperature->setChannelRaw(i, static_cast<int16_t>(regs[i]));
    }
}

void PollingService::parseSlavePumps(const QVector<uint16_t> &regs)
{
    if (regs.size() < 32) return;
    parsePumpStatus(m_slavePump1, regs, 0);
    parsePumpStatus(m_slavePump2, regs, 16);
}

void PollingService::parseWaterCoolerStatus(WaterCoolerModel *model,
                                            const QVector<uint16_t> &regs)
{
    if (!model || regs.size() < Reg::Input::WATER_COOLER_STATUS_COUNT) return;
    model->setOnline(regs[0] != 0);
    model->setTemperatureRaw(static_cast<int16_t>(regs[1]));
}

void PollingService::parseWaterCoolerControl(WaterCoolerModel *model,
                                             const QVector<uint16_t> &regs)
{
    if (!model || regs.isEmpty()) return;
    model->setTempSetRaw(static_cast<int16_t>(regs[0]));
}

void PollingService::parseDfbControl(const QVector<uint16_t> &regs)
{
    if (regs.size() < 6) return;

    const bool dfbSwitch = regs[0] != 0;
    const bool ldSwitch = regs[1] != 0;
    const double tempTarget = static_cast<int16_t>(regs[2]) / 100.0;
    const int driveMode = regs[3];
    const double currentSet = regs[4] / 10.0;
    const double currentMax = regs[5] / 10.0;

    if (m_dfb->shouldAcceptPolledValue(kFieldDfbSwitch, dfbSwitch)) {
        m_dfb->setDfbSwitch(dfbSwitch);
    }
    if (m_dfb->shouldAcceptPolledValue(kFieldLdSwitch, ldSwitch)) {
        m_dfb->setLdSwitch(ldSwitch);
    }
    if (m_dfb->shouldAcceptPolledValue(kFieldTempTarget, tempTarget)) {
        m_dfb->setTempTarget(tempTarget);
    }
    if (m_dfb->shouldAcceptPolledValue(kFieldDriveMode, driveMode)) {
        m_dfb->setDriveMode(driveMode);
    }
    if (m_dfb->shouldAcceptPolledValue(kFieldCurrentSet, currentSet)) {
        m_dfb->setCurrentSet(currentSet);
    }
    if (m_dfb->shouldAcceptPolledValue(kFieldCurrentMax, currentMax)) {
        m_dfb->setCurrentMax(currentMax);
    }
}

void PollingService::parseMpumpControl(const QVector<uint16_t> &regs)
{
    if (regs.size() < 3) return;

    const bool pumpSwitch = regs[0] != 0;
    const uint16_t driveMode = regs[1];
    const uint16_t currentSet = regs[2];

    if (m_mpump->shouldAcceptPolledValue(kFieldPumpSwitch, pumpSwitch)) {
        m_mpump->setPumpSwitch(pumpSwitch);
    }
    if (m_mpump->shouldAcceptPolledValue(kFieldMpumpDriveMode, driveMode)) {
        m_mpump->setDriveMode(driveMode);
    }
    if (m_mpump->shouldAcceptPolledValue(kFieldMpumpCurrentSet, currentSet)) {
        m_mpump->setCurrentSet(currentSet);
    }
}

void PollingService::parseGateControl(GateModel *model, const QVector<uint16_t> &regs)
{
    if (!model || regs.size() < Reg::Hold::GATE_CTRL_COUNT) return;

    const bool gateEnable = regs[0] != 0;
    const uint16_t riseDelayUs = regs[6];
    const uint16_t fallDelayUs = regs[7];
    const uint16_t polarity = regs[8];

    if (model->shouldAcceptPolledValue(kFieldGateEnable, gateEnable)) {
        model->setGateEnable(gateEnable);
    }
    if (model->shouldAcceptPolledValue(kFieldRiseDelayUs, riseDelayUs)) {
        model->setRiseDelayUs(riseDelayUs);
    }
    if (model->shouldAcceptPolledValue(kFieldFallDelayUs, fallDelayUs)) {
        model->setFallDelayUs(fallDelayUs);
    }
    if (model->shouldAcceptPolledValue(kFieldPolarity, polarity)) {
        model->setPolarity(polarity);
    }
}

void PollingService::parsePumpControl(PumpModel *model, const QVector<uint16_t> &regs)
{
    if (regs.size() < 9) return;
    using namespace Reg::Hold::PumpOff;

    const bool enable = regs[ENABLE] != 0;
    const int mode = regs[MODE];
    const uint16_t current = regs[CURRENT];
    const uint16_t pulseWidth = regs[PULSE_WIDTH];
    const uint16_t voltageLimit = regs[VOLTAGE_LIMIT];
    const uint16_t intFreq = regs[INT_FREQ];
    const uint16_t extDelay = regs[EXT_DELAY];
    const uint16_t delayOut = regs[DELAY_OUT];
    const uint16_t addr = regs[ADDR];

    if (model->shouldAcceptPolledValue(kFieldEnable, enable)) {
        model->setEnable(enable);
    }
    if (model->shouldAcceptPolledValue(kFieldMode, mode)) {
        model->setMode(mode);
    }
    if (model->shouldAcceptPolledValue(kFieldCurrent, current)) {
        model->setCurrent(current);
    }
    if (model->shouldAcceptPolledValue(kFieldPulseWidth, pulseWidth)) {
        model->setPulseWidth(pulseWidth);
    }
    if (model->shouldAcceptPolledValue(kFieldVoltageLimit, voltageLimit)) {
        model->setVoltageLimit(voltageLimit);
    }
    if (model->shouldAcceptPolledValue(kFieldIntFreq, intFreq)) {
        model->setIntFreq(intFreq);
    }
    if (model->shouldAcceptPolledValue(kFieldExtDelay, extDelay)) {
        model->setExtDelay(extDelay);
    }
    if (model->shouldAcceptPolledValue(kFieldDelayOut, delayOut)) {
        model->setDelayOut(delayOut);
    }
    if (model->shouldAcceptPolledValue(kFieldAddr, addr)) {
        model->setAddr(addr);
    }
}

void PollingService::parseSlaveHoldingPumps(const QVector<uint16_t> &regs)
{
    if (regs.size() < 32) return;
    // Slave pump1: offset 0-8 (0x0400-0x0408)
    parsePumpControl(m_slavePump1, regs.mid(0, 9));
    // Slave pump2: offset 16-24 (0x0410-0x0418)
    parsePumpControl(m_slavePump2, regs.mid(16, 9));
}

void PollingService::parseTimingBlock(PulseTimingModel *model, const QVector<uint16_t> &regs, int chOffset)
{
    int stride = Reg::Hold::TIMING_CH_STRIDE;  // 18
    int numChannels = regs.size() / stride;

    for (int i = 0; i < numChannels; ++i) {
        int ch = chOffset + i;
        auto *channel = model->channel(ch);
        if (!channel) continue;

        QVector<uint16_t> chRegs = regs.mid(i * stride, stride);
        if (chRegs.size() >= stride) {
            channel->fromRegisters(chRegs);
        }
    }
}
