#pragma once
#include <QObject>
#include "../models/DfbModel.h"

class DeviceComm;

class DfbController : public QObject {
    Q_OBJECT
    Q_PROPERTY(DfbModel* model READ model CONSTANT)

public:
    explicit DfbController(DeviceComm *comm, QObject *parent = nullptr);

    DfbModel* model() const { return m_model; }
    void setModel(DfbModel *model);

    Q_INVOKABLE void readStatus();
    Q_INVOKABLE void readControl();
    Q_INVOKABLE void writeSwitch(bool on);
    Q_INVOKABLE void writeLdSwitch(bool on);
    Q_INVOKABLE void writeTempTarget(double tempC);
    Q_INVOKABLE void writeDriveMode(int mode);
    Q_INVOKABLE void writeCurrentSet(double currentMa);
    Q_INVOKABLE void writeCurrentMax(double currentMa);

signals:
    void writeDone();
    void commError(const QString &msg);

private:
    DeviceComm *m_comm = nullptr;
    DfbModel *m_model = nullptr;
};
