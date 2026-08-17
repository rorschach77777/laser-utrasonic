#include "DeviceComm.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QVariant>
#include <QDebug>

DeviceComm::DeviceComm(QObject *parent)
    : QObject(parent)
    , m_modbusClient(new QModbusRtuSerialClient(this))
{
    m_modbusClient->setTimeout(500);
    m_modbusClient->setNumberOfRetries(2);

    connect(m_modbusClient, &QModbusClient::errorOccurred, this, [this](QModbusDevice::Error err) {
        // qDebug() << "[DeviceComm] errorOccurred:" << err << m_modbusClient->errorString();
        emit commError(m_modbusClient->errorString());
    });

    connect(m_modbusClient, &QModbusClient::stateChanged, this,
            [this](QModbusDevice::State state) {
        // qDebug() << "[DeviceComm] stateChanged:" << state;
        bool nowConnected = (state == QModbusDevice::ConnectedState);
        if (m_connected != nowConnected) {
            m_connected = nowConnected;
            // 延迟到下一个事件循环再通知，让串口完全就绪后再开始发请求
            QMetaObject::invokeMethod(this, &DeviceComm::connectedChanged,
                                      Qt::QueuedConnection);
        }
    });
}

void DeviceComm::setSlaveAddress(uint8_t addr)
{
    if (m_slaveAddr != addr) {
        m_slaveAddr = addr;
        emit slaveAddressChanged();
    }
}

bool DeviceComm::connectDevice(const QString &port, int baudRate)
{
    if (m_modbusClient->state() != QModbusDevice::UnconnectedState)
        m_modbusClient->disconnectDevice();

    m_modbusClient->setConnectionParameter(
        QModbusDevice::SerialPortNameParameter, QVariant(port));
    m_modbusClient->setConnectionParameter(
        QModbusDevice::SerialBaudRateParameter, QVariant(baudRate));
    m_modbusClient->setConnectionParameter(
        QModbusDevice::SerialDataBitsParameter, QVariant::fromValue(QSerialPort::Data8));
    m_modbusClient->setConnectionParameter(
        QModbusDevice::SerialParityParameter, QVariant::fromValue(QSerialPort::NoParity));
    m_modbusClient->setConnectionParameter(
        QModbusDevice::SerialStopBitsParameter, QVariant::fromValue(QSerialPort::OneStop));

    // qDebug() << "[DeviceComm] connectDevice:" << port << baudRate;
    bool ok = m_modbusClient->connectDevice();
    // qDebug() << "[DeviceComm] connectDevice result:" << ok
    //          << (ok ? "" : m_modbusClient->errorString());
    return ok;
}

void DeviceComm::disconnectDevice()
{
    m_modbusClient->disconnectDevice();
}

QStringList DeviceComm::availablePorts() const
{
    QStringList list;
    for (const auto &info : QSerialPortInfo::availablePorts())
        list.append(info.portName());
    return list;
}

void DeviceComm::readInputRegisters(uint16_t startAddr, uint16_t count,
                                     std::function<void(const ModbusResponse&)> callback)
{
    // qDebug() << "[DeviceComm] readInputRegisters addr:" << Qt::hex << startAddr
    //          << "count:" << Qt::dec << count << "slave:" << m_slaveAddr;
    QModbusDataUnit request(QModbusDataUnit::InputRegisters, startAddr, count);
    if (auto *reply = m_modbusClient->sendReadRequest(request, m_slaveAddr)) {
        handleReply(reply, callback);
    } else {
        // qDebug() << "[DeviceComm] sendReadRequest returned nullptr:"
        //          << m_modbusClient->errorString();
        if (callback)
            callback(ModbusResponse{});
    }
}

void DeviceComm::readHoldingRegisters(uint16_t startAddr, uint16_t count,
                                       std::function<void(const ModbusResponse&)> callback)
{
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, startAddr, count);
    if (auto *reply = m_modbusClient->sendReadRequest(request, m_slaveAddr))
        handleReply(reply, callback);
    else if (callback)
        callback(ModbusResponse{});
}

void DeviceComm::writeSingleRegister(uint16_t addr, uint16_t value,
                                      std::function<void(const ModbusResponse&)> callback)
{
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, addr, 1);
    request.setValue(0, value);
    if (auto *reply = m_modbusClient->sendWriteRequest(request, m_slaveAddr))
        handleReply(reply, callback);
    else if (callback)
        callback(ModbusResponse{});
}

void DeviceComm::writeMultipleRegisters(uint16_t startAddr, const QVector<uint16_t> &values,
                                         std::function<void(const ModbusResponse&)> callback)
{
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, startAddr,
                            static_cast<quint16>(values.size()));
    for (int i = 0; i < values.size(); ++i)
        request.setValue(i, values[i]);
    if (auto *reply = m_modbusClient->sendWriteRequest(request, m_slaveAddr))
        handleReply(reply, callback);
    else if (callback)
        callback(ModbusResponse{});
}

void DeviceComm::handleReply(QModbusReply *reply,
                              std::function<void(const ModbusResponse&)> callback)
{
    auto processReply = [this, reply, callback]() {
        ModbusResponse resp;
        if (reply->error() == QModbusDevice::NoError) {
            resp.valid = true;
            resp.slaveAddr = static_cast<uint8_t>(reply->serverAddress());
            const auto unit = reply->result();
            resp.registers.reserve(unit.valueCount());
            for (uint i = 0; i < unit.valueCount(); ++i)
                resp.registers.append(unit.value(i));
            // qDebug() << "[DeviceComm] Reply OK, slave:" << resp.slaveAddr
            //          << "registerCount:" << resp.registers.size()
            //          << "data:" << resp.registers;
        } else if (reply->error() == QModbusDevice::ProtocolError) {
            resp.valid = true;
            resp.functionCode = 0x80;
            resp.exceptionCode = static_cast<uint8_t>(reply->rawResult().exceptionCode());
            // qDebug() << "[DeviceComm] Protocol error, exceptionCode:" << resp.exceptionCode;
        } else {
            // qDebug() << "[DeviceComm] Reply error:" << reply->error()
            //          << reply->errorString();
            emit responseTimeout();
        }
        if (callback)
            callback(resp);
        reply->deleteLater();
    };

    if (reply->isFinished()) {
        // qDebug() << "[DeviceComm] Reply already finished (sync)";
        processReply();
        return;
    }

    connect(reply, &QModbusReply::finished, this, processReply);
}
