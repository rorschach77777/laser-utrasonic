#pragma once

#include <QObject>
#include <cstdint>

class SystemStatusModel : public QObject
{
    Q_OBJECT

    // Raw register properties
    Q_PROPERTY(uint16_t sysStatus READ sysStatus NOTIFY sysStatusChanged)
    Q_PROPERTY(uint16_t sysFault READ sysFault NOTIFY sysFaultChanged)
    Q_PROPERTY(uint16_t mainSyncStep READ mainSyncStep NOTIFY mainSyncStepChanged)
    Q_PROPERTY(uint16_t mainSyncStatus READ mainSyncStatus NOTIFY mainSyncStatusChanged)
    Q_PROPERTY(uint32_t mainTdMeasured READ mainTdMeasured NOTIFY mainTdMeasuredChanged)

    // Derived from sysStatus bits
    Q_PROPERTY(bool initOk READ initOk NOTIFY sysStatusChanged)
    Q_PROPERTY(bool dfbReady READ dfbReady NOTIFY sysStatusChanged)
    Q_PROPERTY(bool pumpReady READ pumpReady NOTIFY sysStatusChanged)
    Q_PROPERTY(bool slaveReady READ slaveReady NOTIFY sysStatusChanged)
    Q_PROPERTY(bool syncOk READ syncOk NOTIFY sysStatusChanged)

    // Derived from sysFault bits
    Q_PROPERTY(bool faultDfb READ faultDfb NOTIFY sysFaultChanged)
    Q_PROPERTY(bool faultPump READ faultPump NOTIFY sysFaultChanged)
    Q_PROPERTY(bool faultSlaveComm READ faultSlaveComm NOTIFY sysFaultChanged)
    Q_PROPERTY(bool faultSync READ faultSync NOTIFY sysFaultChanged)
    Q_PROPERTY(bool faultTemp READ faultTemp NOTIFY sysFaultChanged)
    Q_PROPERTY(bool faultTtlIn READ faultTtlIn NOTIFY sysFaultChanged)
    Q_PROPERTY(bool hasFault READ hasFault NOTIFY sysFaultChanged)

public:
    explicit SystemStatusModel(QObject *parent = nullptr);

    uint16_t sysStatus() const;
    void setSysStatus(uint16_t v);

    uint16_t sysFault() const;
    void setSysFault(uint16_t v);

    uint16_t mainSyncStep() const;
    void setMainSyncStep(uint16_t v);

    uint16_t mainSyncStatus() const;
    void setMainSyncStatus(uint16_t v);

    uint32_t mainTdMeasured() const;
    void setMainTdMeasured(uint32_t v);

    // sysStatus bit accessors
    bool initOk() const;
    bool dfbReady() const;
    bool pumpReady() const;
    bool slaveReady() const;
    bool syncOk() const;

    // sysFault bit accessors
    bool faultDfb() const;
    bool faultPump() const;
    bool faultSlaveComm() const;
    bool faultSync() const;
    bool faultTemp() const;
    bool faultTtlIn() const;
    bool hasFault() const;

signals:
    void sysStatusChanged();
    void sysFaultChanged();
    void mainSyncStepChanged();
    void mainSyncStatusChanged();
    void mainTdMeasuredChanged();

private:
    uint16_t m_sysStatus = 0;
    uint16_t m_sysFault = 0;
    uint16_t m_mainSyncStep = 0;
    uint16_t m_mainSyncStatus = 0;
    uint32_t m_mainTdMeasured = 0;
};
