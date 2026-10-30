#include "RadarUdpSharedService.h"

#include <QHostAddress>
#include <QMetaObject>

void RadarUdpStreamWorker::start()
{
    if (m_retryTimer)
        return;
    // Retry bind on a separate timer, never by blocking the UI or DAQ.
    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(2000);
    connect(m_retryTimer, &QTimer::timeout, this, &RadarUdpStreamWorker::tryBind);
    tryBind();
}

void RadarUdpStreamWorker::tryBind()
{
    if (m_receiver)
        return;
    // Allocate socket in its owning thread.
    m_receiver = new RadarUdpReceiver(this);
    connect(m_receiver, &RadarUdpReceiver::radarSampleReceived,
            this, &RadarUdpStreamWorker::sampleReceived);
    connect(m_receiver, &RadarUdpReceiver::receiverError,
            this, &RadarUdpStreamWorker::statusMessage);
    if (!m_receiver->start(QHostAddress(QStringLiteral("192.168.100.15")), 1111,
                           QHostAddress(QStringLiteral("192.168.100.110")))) {
        emit statusMessage(QStringLiteral("雷达UDP绑定失败：请关闭RadarUdpProbe/PCIS，确认网卡192.168.100.15"));
        delete m_receiver;
        m_receiver = nullptr;
        m_retryTimer->start();
        return;
    }
    m_retryTimer->stop();
    emit statusMessage(QStringLiteral("雷达UDP共享接收正常 (192.168.100.15:1111)"));
}

void RadarUdpStreamWorker::stop()
{
    if (m_retryTimer) {
        m_retryTimer->stop();
        delete m_retryTimer;
        m_retryTimer = nullptr;
    }
    if (m_receiver) {
        m_receiver->stop();
        delete m_receiver;
        m_receiver = nullptr;
    }
}

RadarUdpSharedService::RadarUdpSharedService(QObject *parent) : QObject(parent) {}
RadarUdpSharedService::~RadarUdpSharedService() { stop(); }

void RadarUdpSharedService::start()
{
    if (m_running)
        return;
    m_worker = new RadarUdpStreamWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &RadarUdpStreamWorker::start);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    // Forward signal-to-signal synchronously in the worker's emitting thread.
    // Consumers on the GUI thread are still queued automatically by Qt.
    // This also avoids a redundant cross-thread queued relay through this QObject.
    connect(m_worker, &RadarUdpStreamWorker::sampleReceived,
            this, &RadarUdpSharedService::sampleReceived, Qt::DirectConnection);
    connect(m_worker, &RadarUdpStreamWorker::statusMessage,
            this, &RadarUdpSharedService::statusMessage, Qt::DirectConnection);
    m_running = true;
    m_thread.start();
}

void RadarUdpSharedService::stop()
{
    if (!m_running)
        return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->stop(); },
                              Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_worker = nullptr;
    m_running = false;
}
