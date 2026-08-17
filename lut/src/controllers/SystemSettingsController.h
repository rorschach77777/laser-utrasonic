#pragma once
#include <QObject>
#include <cstdint>

class DeviceComm;

class SystemSettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int slaveAddr READ slaveAddr NOTIFY slaveAddrChanged)
    Q_PROPERTY(int baudRateCode READ baudRateCode NOTIFY baudRateCodeChanged)
    Q_PROPERTY(uint16_t subModuleEnable READ subModuleEnable NOTIFY subModuleEnableChanged)
    Q_PROPERTY(uint16_t slaveSubModuleEnable READ slaveSubModuleEnable NOTIFY slaveSubModuleEnableChanged)
    Q_PROPERTY(uint16_t pulseChEnable READ pulseChEnable NOTIFY pulseChEnableChanged)
    Q_PROPERTY(uint16_t slavePulseChEnable READ slavePulseChEnable NOTIFY slavePulseChEnableChanged)
    Q_PROPERTY(uint16_t pulsePeriod READ pulsePeriod NOTIFY pulsePeriodChanged)
    Q_PROPERTY(uint16_t pulseCount READ pulseCount NOTIFY pulseCountChanged)
    Q_PROPERTY(uint16_t slavePulsePeriod READ slavePulsePeriod NOTIFY slavePulsePeriodChanged)
    Q_PROPERTY(uint16_t slavePulseCount READ slavePulseCount NOTIFY slavePulseCountChanged)
    Q_PROPERTY(bool mainSyncEnable READ mainSyncEnable NOTIFY mainSyncEnableChanged)
    Q_PROPERTY(bool slaveSyncEnable READ slaveSyncEnable NOTIFY slaveSyncEnableChanged)
    Q_PROPERTY(quint32 mainTw READ mainTw NOTIFY mainTwChanged)
    Q_PROPERTY(quint32 slaveTw READ slaveTw NOTIFY slaveTwChanged)

public:
    explicit SystemSettingsController(DeviceComm *comm, QObject *parent = nullptr);

    int slaveAddr() const { return m_slaveAddr; }
    int baudRateCode() const { return m_baudRateCode; }
    uint16_t subModuleEnable() const { return m_subModuleEnable; }
    uint16_t slaveSubModuleEnable() const { return m_slaveSubModuleEnable; }
    uint16_t pulseChEnable() const { return m_pulseChEnable; }
    uint16_t slavePulseChEnable() const { return m_slavePulseChEnable; }
    uint16_t pulsePeriod() const { return m_pulsePeriod; }
    uint16_t pulseCount() const { return m_pulseCount; }
    uint16_t slavePulsePeriod() const { return m_slavePulsePeriod; }
    uint16_t slavePulseCount() const { return m_slavePulseCount; }
    bool mainSyncEnable() const { return m_mainSyncEnable; }
    bool slaveSyncEnable() const { return m_slaveSyncEnable; }
    quint32 mainTw() const { return (static_cast<quint32>(m_mainTwHigh) << 16) | m_mainTwLow; }
    quint32 slaveTw() const { return (static_cast<quint32>(m_slaveTwHigh) << 16) | m_slaveTwLow; }

    Q_INVOKABLE void readAll();
    Q_INVOKABLE void writeSlaveAddr(int addr);
    Q_INVOKABLE void writeBaudRate(int code);
    Q_INVOKABLE void writeSubModuleEnable(uint16_t mask);
    Q_INVOKABLE void writeSlaveSubModuleEnable(uint16_t mask);
    Q_INVOKABLE void writePulseChEnable(uint16_t mask);
    Q_INVOKABLE void writeSlavePulseChEnable(uint16_t mask);
    Q_INVOKABLE void writePulsePeriod(uint16_t ms);
    Q_INVOKABLE void writePulseCount(uint16_t count);
    Q_INVOKABLE void writeSlavePulsePeriod(uint16_t ms);
    Q_INVOKABLE void writeSlavePulseCount(uint16_t count);
    Q_INVOKABLE void writeMainSyncEnable(bool enable);
    Q_INVOKABLE void writeSlaveSyncEnable(bool enable);
    Q_INVOKABLE void writeMainTw(quint32 value);
    Q_INVOKABLE void writeSlaveTw(quint32 value);

signals:
    void slaveAddrChanged();
    void baudRateCodeChanged();
    void subModuleEnableChanged();
    void slaveSubModuleEnableChanged();
    void pulseChEnableChanged();
    void slavePulseChEnableChanged();
    void pulsePeriodChanged();
    void pulseCountChanged();
    void slavePulsePeriodChanged();
    void slavePulseCountChanged();
    void mainSyncEnableChanged();
    void slaveSyncEnableChanged();
    void mainTwChanged();
    void slaveTwChanged();
    void readAllFinished(bool success);
    void writeDone();
    void commError(const QString &msg);

private:
    DeviceComm *m_comm = nullptr;

    int m_slaveAddr = 1;
    int m_baudRateCode = 1;
    uint16_t m_subModuleEnable = 0x003F;
    uint16_t m_slaveSubModuleEnable = 0x0003;
    uint16_t m_pulseChEnable = 0x00FF;
    uint16_t m_slavePulseChEnable = 0x00FF;
    uint16_t m_pulsePeriod = 1000;
    uint16_t m_pulseCount = 1;
    uint16_t m_slavePulsePeriod = 1000;
    uint16_t m_slavePulseCount = 1;
    bool m_mainSyncEnable = false;
    bool m_slaveSyncEnable = false;
    uint16_t m_mainTwHigh = 0;
    uint16_t m_mainTwLow = 10000;
    uint16_t m_slaveTwHigh = 0;
    uint16_t m_slaveTwLow = 10000;
};
