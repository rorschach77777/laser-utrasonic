#pragma once

#include <QObject>
#include <cstdint>

class SlaveStatusModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(uint16_t sysStatus READ sysStatus NOTIFY sysStatusChanged)
    Q_PROPERTY(uint16_t sysFault READ sysFault NOTIFY sysFaultChanged)
    Q_PROPERTY(uint16_t syncStep READ syncStep NOTIFY syncStepChanged)
    Q_PROPERTY(uint16_t syncStatusCode READ syncStatusCode NOTIFY syncStatusCodeChanged)
    Q_PROPERTY(uint16_t syncErrorCount READ syncErrorCount NOTIFY syncErrorCountChanged)
    Q_PROPERTY(uint32_t uptimeSeconds READ uptimeSeconds NOTIFY uptimeSecondsChanged)

public:
    explicit SlaveStatusModel(QObject *parent = nullptr);

    bool online() const;
    void setOnline(bool v);

    uint16_t sysStatus() const;
    void setSysStatus(uint16_t v);

    uint16_t sysFault() const;
    void setSysFault(uint16_t v);

    uint16_t syncStep() const;
    void setSyncStep(uint16_t v);

    uint16_t syncStatusCode() const;
    void setSyncStatusCode(uint16_t v);

    uint16_t syncErrorCount() const;
    void setSyncErrorCount(uint16_t v);

    uint32_t uptimeSeconds() const;
    void setUptimeSeconds(uint32_t v);

signals:
    void onlineChanged();
    void sysStatusChanged();
    void sysFaultChanged();
    void syncStepChanged();
    void syncStatusCodeChanged();
    void syncErrorCountChanged();
    void uptimeSecondsChanged();

private:
    bool m_online = false;
    uint16_t m_sysStatus = 0;
    uint16_t m_sysFault = 0;
    uint16_t m_syncStep = 0;
    uint16_t m_syncStatusCode = 0;
    uint16_t m_syncErrorCount = 0;
    uint32_t m_uptimeSeconds = 0;
};
