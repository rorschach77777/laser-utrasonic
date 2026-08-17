#include "GateController.h"
#include "../communication/DeviceComm.h"
#include "../models/RegisterDefs.h"

namespace {
constexpr auto kFieldGateEnable = "gateEnable";
constexpr auto kFieldRiseDelayUs = "riseDelayUs";
constexpr auto kFieldFallDelayUs = "fallDelayUs";
constexpr auto kFieldPolarity = "polarity";

uint16_t boundedRegisterValue(int value, uint16_t maximum)
{
    if (value < 0) return 0;
    if (value > maximum) return maximum;
    return static_cast<uint16_t>(value);
}
}

GateController::GateController(DeviceComm *comm,
                               uint16_t statusBase,
                               uint16_t controlBase,
                               uint16_t enableAddr,
                               uint16_t riseDelayAddr,
                               uint16_t fallDelayAddr,
                               uint16_t polarityAddr,
                               QObject *parent)
    : QObject(parent)
    , m_comm(comm)
    , m_statusBase(statusBase)
    , m_controlBase(controlBase)
    , m_enableAddr(enableAddr)
    , m_riseDelayAddr(riseDelayAddr)
    , m_fallDelayAddr(fallDelayAddr)
    , m_polarityAddr(polarityAddr)
{
}

void GateController::setModel(GateModel *model)
{
    m_model = model;
}

void GateController::readStatus()
{
    if (!m_model) return;
    m_comm->readInputRegisters(m_statusBase, Reg::Input::GATE_STATUS_COUNT,
        [this](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                const auto &r = resp.registers;
                if (r.size() >= Reg::Input::GATE_STATUS_COUNT) {
                    m_model->setGateLevel(r[0]);
                    m_model->setGateState(r[1]);
                }
            } else {
                emit commError("Failed to read Gate status");
            }
        });
}

void GateController::readControl()
{
    if (!m_model) return;
    m_comm->readHoldingRegisters(m_controlBase, Reg::Hold::GATE_CTRL_COUNT,
        [this](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                const auto &r = resp.registers;
                if (r.size() >= Reg::Hold::GATE_CTRL_COUNT) {
                    const bool gateEnable = r[0] != 0;
                    const uint16_t riseDelayUs = r[6];
                    const uint16_t fallDelayUs = r[7];
                    const uint16_t polarity = r[8];

                    if (m_model->shouldAcceptPolledValue(kFieldGateEnable, gateEnable)) {
                        m_model->setGateEnable(gateEnable);
                    }
                    if (m_model->shouldAcceptPolledValue(kFieldRiseDelayUs, riseDelayUs)) {
                        m_model->setRiseDelayUs(riseDelayUs);
                    }
                    if (m_model->shouldAcceptPolledValue(kFieldFallDelayUs, fallDelayUs)) {
                        m_model->setFallDelayUs(fallDelayUs);
                    }
                    if (m_model->shouldAcceptPolledValue(kFieldPolarity, polarity)) {
                        m_model->setPolarity(polarity);
                    }
                }
            } else {
                emit commError("Failed to read Gate control");
            }
        });
}

void GateController::writeGateEnable(bool enable)
{
    if (!m_model) return;
    const bool previous = m_model->gateEnable();
    m_model->setGateEnable(enable);
    m_model->markPending(kFieldGateEnable, enable);
    m_comm->writeSingleRegister(m_enableAddr, enable ? 1 : 0,
        [this, previous](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                emit writeDone();
            } else {
                m_model->clearPending(kFieldGateEnable);
                m_model->setGateEnable(previous);
                emit commError("Failed to write Gate enable");
            }
        });
}

void GateController::writeRiseDelayUs(int delayUs)
{
    if (!m_model) return;
    const uint16_t previous = m_model->riseDelayUs();
    const uint16_t next = boundedRegisterValue(delayUs, 20000);
    m_model->setRiseDelayUs(next);
    m_model->markPending(kFieldRiseDelayUs, next);
    m_comm->writeSingleRegister(m_riseDelayAddr, next,
        [this, previous](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                emit writeDone();
            } else {
                m_model->clearPending(kFieldRiseDelayUs);
                m_model->setRiseDelayUs(previous);
                emit commError("Failed to write Gate rise delay");
            }
        });
}

void GateController::writeFallDelayUs(int delayUs)
{
    if (!m_model) return;
    const uint16_t previous = m_model->fallDelayUs();
    const uint16_t next = boundedRegisterValue(delayUs, 20000);
    m_model->setFallDelayUs(next);
    m_model->markPending(kFieldFallDelayUs, next);
    m_comm->writeSingleRegister(m_fallDelayAddr, next,
        [this, previous](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                emit writeDone();
            } else {
                m_model->clearPending(kFieldFallDelayUs);
                m_model->setFallDelayUs(previous);
                emit commError("Failed to write Gate fall delay");
            }
        });
}

void GateController::writePolarity(int polarity)
{
    if (!m_model) return;
    const uint16_t previous = m_model->polarity();
    const uint16_t next = boundedRegisterValue(polarity, 1);
    m_model->setPolarity(next);
    m_model->markPending(kFieldPolarity, next);
    m_comm->writeSingleRegister(m_polarityAddr, next,
        [this, previous](const ModbusResponse &resp) {
            if (resp.valid && !resp.isException()) {
                emit writeDone();
            } else {
                m_model->clearPending(kFieldPolarity);
                m_model->setPolarity(previous);
                emit commError("Failed to write Gate polarity");
            }
        });
}
