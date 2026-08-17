#pragma once
#include <QObject>

class DeviceComm;

class CommandController : public QObject {
    Q_OBJECT

public:
    explicit CommandController(DeviceComm *comm, QObject *parent = nullptr);

    Q_INVOKABLE void run();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void singleTrigger();
    Q_INVOKABLE void saveParams();
    Q_INVOKABLE void factoryReset();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void clearFault();
    Q_INVOKABLE void syncToSlave();

signals:
    void commandSent(const QString &cmdName);
    void commandFailed(const QString &cmdName);

private:
    void sendCommand(uint16_t addr, uint16_t value, const QString &cmdName);

    DeviceComm *m_comm = nullptr;
};
