#pragma once
#include <QObject>
#include <QModbusRtuSerialClient>
#include <QModbusDataUnit>
#include <QModbusReply>
#include <functional>
#include "../modbus/ModbusResponse.h"

class DeviceComm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(uint8_t slaveAddress READ slaveAddress WRITE setSlaveAddress NOTIFY slaveAddressChanged)
public:
    explicit DeviceComm(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    uint8_t slaveAddress() const { return m_slaveAddr; }
    void setSlaveAddress(uint8_t addr);

    Q_INVOKABLE bool connectDevice(const QString &port, int baudRate);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE QStringList availablePorts() const;

    void readInputRegisters(uint16_t startAddr, uint16_t count,
                            std::function<void(const ModbusResponse&)> callback);
    void readHoldingRegisters(uint16_t startAddr, uint16_t count,
                              std::function<void(const ModbusResponse&)> callback);
    void writeSingleRegister(uint16_t addr, uint16_t value,
                             std::function<void(const ModbusResponse&)> callback = nullptr);
    void writeMultipleRegisters(uint16_t startAddr, const QVector<uint16_t> &values,
                                std::function<void(const ModbusResponse&)> callback = nullptr);

signals:
    void connectedChanged();
    void slaveAddressChanged();
    void commError(const QString &error);
    void responseTimeout();

private:
    void handleReply(QModbusReply *reply, std::function<void(const ModbusResponse&)> callback);

    QModbusRtuSerialClient *m_modbusClient = nullptr;
    bool m_connected = false;
    uint8_t m_slaveAddr = 1;
};
