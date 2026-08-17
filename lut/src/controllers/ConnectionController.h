#pragma once
#include <QObject>
#include <QTimer>
#include <QStringList>
#include "../models/DeviceInfoModel.h"

class DeviceComm;

class ConnectionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList portList READ portList NOTIFY portListChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort WRITE setSelectedPort NOTIFY selectedPortChanged)
    Q_PROPERTY(int baudRate READ baudRate WRITE setBaudRate NOTIFY baudRateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(DeviceInfoModel* deviceInfo READ deviceInfo CONSTANT)

public:
    explicit ConnectionController(DeviceComm *comm, QObject *parent = nullptr);

    QStringList portList() const { return m_portList; }
    QString selectedPort() const { return m_selectedPort; }
    int baudRate() const { return m_baudRate; }
    bool connected() const;
    DeviceInfoModel* deviceInfo() const { return m_deviceInfo; }

    void setSelectedPort(const QString &port);
    void setBaudRate(int rate);

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void disconnectDevice();

signals:
    void portListChanged();
    void selectedPortChanged();
    void baudRateChanged();
    void connectedChanged();
    void deviceInfoReadFinished(bool success);
    void connectionError(const QString &error);

private:
    void readDeviceInfo();
    void readDeviceUptime();

    DeviceComm *m_comm = nullptr;
    DeviceInfoModel *m_deviceInfo = nullptr;
    QStringList m_portList;
    QString m_selectedPort;
    int m_baudRate = 9600;
    QTimer m_uptimeTimer;
};
