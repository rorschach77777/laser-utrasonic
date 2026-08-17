#pragma once
#include <QObject>
#include "../models/MultiModePumpModel.h"

class DeviceComm;

class MultiModePumpController : public QObject {
    Q_OBJECT
    Q_PROPERTY(MultiModePumpModel* model READ model CONSTANT)

public:
    explicit MultiModePumpController(DeviceComm *comm, QObject *parent = nullptr);

    MultiModePumpModel* model() const { return m_model; }
    void setModel(MultiModePumpModel *model);

    Q_INVOKABLE void readStatus();
    Q_INVOKABLE void readControl();
    Q_INVOKABLE void writeSwitch(bool on);
    Q_INVOKABLE void writeDriveMode(int mode);
    Q_INVOKABLE void writeCurrentSet(int milliAmps);

signals:
    void writeDone();
    void commError(const QString &msg);

private:
    DeviceComm *m_comm = nullptr;
    MultiModePumpModel *m_model = nullptr;
};
