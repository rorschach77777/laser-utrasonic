#pragma once

#include <QObject>
#include <cstdint>

class DeviceInfoModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(uint16_t deviceType READ deviceType NOTIFY deviceTypeChanged)
    Q_PROPERTY(uint16_t hwVersion READ hwVersion NOTIFY hwVersionChanged)
    Q_PROPERTY(uint16_t swVersion READ swVersion NOTIFY swVersionChanged)
    Q_PROPERTY(uint32_t serialNumber READ serialNumber NOTIFY serialNumberChanged)
    Q_PROPERTY(uint32_t uptimeSeconds READ uptimeSeconds NOTIFY uptimeSecondsChanged)
    Q_PROPERTY(QString hwVersionString READ hwVersionString NOTIFY hwVersionChanged)
    Q_PROPERTY(QString swVersionString READ swVersionString NOTIFY swVersionChanged)

public:
    explicit DeviceInfoModel(QObject *parent = nullptr);

    uint16_t deviceType() const;
    void setDeviceType(uint16_t v);

    uint16_t hwVersion() const;
    void setHwVersion(uint16_t v);

    uint16_t swVersion() const;
    void setSwVersion(uint16_t v);

    uint32_t serialNumber() const;
    void setSerialNumber(uint32_t v);

    uint32_t uptimeSeconds() const;
    void setUptimeSeconds(uint32_t v);

    QString hwVersionString() const;
    QString swVersionString() const;

signals:
    void deviceTypeChanged();
    void hwVersionChanged();
    void swVersionChanged();
    void serialNumberChanged();
    void uptimeSecondsChanged();

private:
    uint16_t m_deviceType = 0;
    uint16_t m_hwVersion = 0;
    uint16_t m_swVersion = 0;
    uint32_t m_serialNumber = 0;
    uint32_t m_uptimeSeconds = 0;
};
