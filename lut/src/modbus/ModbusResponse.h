#pragma once
#include <QByteArray>
#include <QVector>
#include <cstdint>

struct ModbusResponse {
    bool valid = false;
    uint8_t slaveAddr = 0;
    uint8_t functionCode = 0;
    uint8_t exceptionCode = 0;
    QVector<uint16_t> registers;

    bool isException() const { return functionCode & 0x80; }
};
