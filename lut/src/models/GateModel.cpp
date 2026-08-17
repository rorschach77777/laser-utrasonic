#include "GateModel.h"

GateModel::GateModel(QObject *parent)
    : QObject(parent)
{
}

uint16_t GateModel::gateLevel() const { return m_gateLevel; }
void GateModel::setGateLevel(uint16_t v)
{
    if (m_gateLevel != v) {
        m_gateLevel = v;
        emit gateLevelChanged();
    }
}

uint16_t GateModel::gateState() const { return m_gateState; }
void GateModel::setGateState(uint16_t v)
{
    if (m_gateState != v) {
        m_gateState = v;
        emit gateStateChanged();
    }
}

bool GateModel::gateEnable() const { return m_gateEnable; }
void GateModel::setGateEnable(bool v)
{
    if (m_gateEnable != v) {
        m_gateEnable = v;
        emit gateEnableChanged();
    }
}

uint16_t GateModel::riseDelayUs() const { return m_riseDelayUs; }
void GateModel::setRiseDelayUs(uint16_t v)
{
    if (m_riseDelayUs != v) {
        m_riseDelayUs = v;
        emit riseDelayUsChanged();
    }
}

uint16_t GateModel::fallDelayUs() const { return m_fallDelayUs; }
void GateModel::setFallDelayUs(uint16_t v)
{
    if (m_fallDelayUs != v) {
        m_fallDelayUs = v;
        emit fallDelayUsChanged();
    }
}

uint16_t GateModel::polarity() const { return m_polarity; }
void GateModel::setPolarity(uint16_t v)
{
    if (m_polarity != v) {
        m_polarity = v;
        emit polarityChanged();
    }
}

void GateModel::markPending(const QString &fieldKey, const QVariant &targetValue, int timeoutMs)
{
    m_pendingWrites.markPending(fieldKey, targetValue, timeoutMs);
}

void GateModel::clearPending(const QString &fieldKey)
{
    m_pendingWrites.clearPending(fieldKey);
}

bool GateModel::shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue)
{
    return m_pendingWrites.shouldAcceptPolledValue(fieldKey, polledValue);
}

bool GateModel::hasPending(const QString &fieldKey) const
{
    return m_pendingWrites.hasPending(fieldKey);
}
