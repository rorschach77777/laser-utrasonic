#pragma once

#include "RadarUdpReceiver.h"
#include "LstDataExport.h"

#include <QObject>
#include <QThread>
#include <QTimer>

// Always-on (while app runs) reader; ONE exclusive UDP listener on port 1111.
// App UI and optional measurement CSV sidecar subscribe to same stream.
class LST_DATA_API RadarUdpStreamWorker final : public QObject
{
    Q_OBJECT
public:
    explicit RadarUdpStreamWorker(QObject *parent = nullptr) : QObject(parent) {}
    void start();
    void stop();
signals:
    void sampleReceived(const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs);
    void statusMessage(const QString &status);
private:
    void tryBind();
    RadarUdpReceiver *m_receiver = nullptr;
    QTimer *m_retryTimer = nullptr;
};

class LST_DATA_API RadarUdpSharedService final : public QObject
{
    Q_OBJECT
public:
    explicit RadarUdpSharedService(QObject *parent = nullptr);
    ~RadarUdpSharedService() override;
    void start();
    void stop();
signals:
    void sampleReceived(const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs);
    void statusMessage(const QString &status);
private:
    QThread m_thread;
    RadarUdpStreamWorker *m_worker = nullptr;
    bool m_running = false;
};
