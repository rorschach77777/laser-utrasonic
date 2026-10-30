#include "DataController.h"
#include "../algorithm/ThicknessProcessor.h"
#include "bdaqctrl.h"
#include "../communication/RadarHostClock.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QtMath>
#include <QDebug>
#include <cmath>

using namespace Automation::BDaq;

#define LOCAL_EDGE "Local"

DataController::DataController(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_paused(false)
    , m_thickness(0.0)
    , m_detectionRate(0.0)
    , m_snr(0.0)
    , m_thicknessMax(-std::numeric_limits<double>::max())
    , m_thicknessMin(std::numeric_limits<double>::max())
    , m_thicknessSum(0.0)
    , m_thicknessCount(0)
    , m_autoScale(true)
    , m_showOriginal(true)
    , m_savePerFrame(false)
    , m_historyRecording(false)
    , m_historyManager(nullptr)
    , m_waveformAiCtrl(nullptr)
    , m_scaledData(nullptr)
    , m_rawDataBufferLength(0)
    , m_isTriggerSupported(false)
    , m_filterOrder(50)
    , m_saveFrameCounter(0)
{
    m_configure.hostName = LOCAL_EDGE;
    m_configure.deviceName = "";
    m_configure.channelCount = 1;
    m_configure.channelStart = 0;
    m_configure.valueRange = V_Neg5To5;
    m_configure.clockRatePerChan = 160000000.0;
    m_configure.sectionLength = 1600;
    m_configure.cycles = 0;
    m_configure.profilePath = "/qml/pages/profile.xml";
    m_configure.detectMode = "auto";
    m_configure.thicknessStd = 6.5;
    m_configure.thicknessTolerance = 0.8;
    m_configure.structLen = 9;
    m_configure.minWidth = 1;
    m_configure.filterLowFreqMHz = 3.0;
    m_configure.filterHighFreqMHz = 30.0;
    m_configure.triggerAction = DelayToStart;
    // 与 LSTAlgorithmTest 的固定延时标定保持一致。
    m_configure.delayCount = 240;
    m_configure.triggerLevel = 2.0;
    m_configure.triggerEdge = RisingEdge;
    m_configure.triggerSourceIndex = 0;
    m_configure.actualDelayCount = 0;

    //m_triggerEdgeList << "RisingEdge" << "FallingEdge";
    m_radarPositionSidecar = new RadarPositionSidecar(this);
    connect(m_radarPositionSidecar, &RadarPositionSidecar::axialPointReady,
            this, &DataController::updateAxialPoint);
    connect(m_radarPositionSidecar, &RadarPositionSidecar::axialTailPointReady,
            this, [this](double x, double h) {
        if (m_pipeFinalized || !std::isfinite(x) || !std::isfinite(h)) return;
        m_tailFillPoints.append({x, h});
        emit axialTailPointAdded(x, h);
        emit tailFillPointsChanged();
    });
    connect(m_radarPositionSidecar, &RadarPositionSidecar::tailFillFinished,
            this, [this](int count, double mm, const QString &decision) {
        if (!decision.isEmpty())
            m_tailFillStatus = decision;
        else
            m_tailFillStatus = QStringLiteral(
                "管尾估算结果：%1点，设定长度%2mm").arg(count).arg(mm, 0, 'f', 1);
        if (count > 0 && !m_tailFillPoints.isEmpty())
            m_tailFillStatus += QStringLiteral("；末端坐标%1mm")
                .arg(m_tailFillPoints.last().positionMm, 0, 'f', 2);
        emit tailFillPointsChanged();
        finalizeAxialPlotData();
    });
    connect(m_radarPositionSidecar, &RadarPositionSidecar::headStatusChanged,
            this, [this](const QString &status) {
        const bool urgent = status.contains(QStringLiteral("溢出")) ||
            status.contains(QStringLiteral("失效")) ||
            status.contains(QStringLiteral("不可用"));
        if (m_laserHeadThicknessConfirmed && !urgent &&
            !status.startsWith(QStringLiteral("三帧测厚已确认")) &&
            !status.startsWith(QStringLiteral("三帧厚度已确认")))
            return; // never hide current radar failure behind past thickness lock
        if (m_axialOriginStatus == status) return;
        m_axialOriginStatus = status;
        emit axialOriginStatusChanged();
    });
    connect(m_radarPositionSidecar, &RadarPositionSidecar::statusMessage,
            this, [this](const QString &message) {
                qDebug().noquote() << "[RadarSidecar]" << message;
                m_radarSidecarStatus = message;
                emit radarSidecarStatusChanged();
                if (message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
                    message.contains(QStringLiteral("OVERFLOW")) ||
                    message.contains(QStringLiteral("JOIN_CAPACITY_LIMIT")))
                    emit errorOccurred(message);
            });
}

DataController::~DataController()
{
    // 析构时先停DAQ，确保不会再有新帧进入处理队列；随后按“处理→保存”顺序排空。
    if (m_waveformAiCtrl) {
        WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
        ctrl->Stop();
    }
    stopProcessingWorker();
    flushPendingFrameSave();
    flushPendingResultCommits(); // preserve final Frame ID/backfills for sidecar
    if (m_radarPositionSidecar)
        m_radarPositionSidecar->stopSession();
    stopSaveWorker();

    if (m_waveformAiCtrl) {
        WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
        ctrl->Release();
        ctrl->Logout();
        ctrl->Dispose();
        m_waveformAiCtrl = nullptr;
    }
    if (m_scaledData) {
        delete[] m_scaledData;
        m_scaledData = nullptr;
    }
}

void DataController::initialize()
{
    m_triggerEdgeList << "RisingEdge" << "FallingEdge";
    m_waveformAiCtrl = WaveformAiCtrl::Create();
    if (m_waveformAiCtrl) {
        WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
        ctrl->addDataReadyHandler(OnDataReadyEvent, this);
        //qDebug() << "[DataController] DataReady handler registered";
    }
    refreshDeviceList();
}

void DataController::refreshDeviceList()
{
    //qDebug() << "[DataController] refreshDeviceList() called";

    // 创建临时控制器查询设备列表，避免影响主控制器状态
    WaveformAiCtrl *tmpCtrl = WaveformAiCtrl::Create();
    if (!tmpCtrl) {
        //qDebug() << "[DataController] WaveformAiCtrl::Create() failed";
        return;
    }

    m_deviceList.clear();
    m_triggerSourceList.clear();

    // 如果是远程设备，先登录
    if (m_configure.hostName != LOCAL_EDGE) {
        std::wstring hostName = m_configure.hostName.toStdWString();
        //qDebug() << "[DataController] Login to remote host:" << m_configure.hostName;
        if (Success != tmpCtrl->Login(hostName.c_str())) {
            //qDebug() << "[DataController] Login failed";
            tmpCtrl->Dispose();
            emit deviceListChanged();
            return;
        }
    }

    Array<DeviceTreeNode> *supportedDevice = tmpCtrl->getSupportedDevices();
    if (supportedDevice == nullptr) {
        //qDebug() << "[DataController] getSupportedDevices() returned nullptr - no hardware";
        tmpCtrl->Logout();
        tmpCtrl->Dispose();
        emit deviceListChanged();
        return;
    }

    int count = supportedDevice->getCount();
    //qDebug() << "[DataController] Found" << count << "supported devices";

    DeviceInformation devInfo;
    for (int i = 0; i < count; i++) {
        DeviceTreeNode const &node = supportedDevice->getItem(i);
        devInfo.DeviceNumber = node.DeviceNumber;
        tmpCtrl->setSelectedDevice(devInfo);
        if (tmpCtrl->getFeatures()->getRetriggerable()) {
            QString desc = QString::fromWCharArray(node.Description);
            m_deviceList.append(desc);
            //qDebug() << "[DataController] Device" << i << ":" << desc;
        }
    }

    //qDebug() << "[DataController] Device list count:" << m_deviceList.size();

    tmpCtrl->Logout();
    tmpCtrl->Dispose();
    supportedDevice->Dispose();

    emit deviceListChanged();
}

QStringList DataController::deviceList() const { return m_deviceList; }
QStringList DataController::triggerSourceList() const { return m_triggerSourceList; }
QStringList DataController::triggerEdgeList() const { return m_triggerEdgeList; }

void DataController::setHostName(const QString &name)
{
    if (m_configure.hostName == name) return;
    m_configure.hostName = name;
    if (name == LOCAL_EDGE) {
        refreshDeviceList();
    }
}

QString DataController::hostName() const { return m_configure.hostName; }

void DataController::selectDevice(int index)
{
    //qDebug() << "[DataController] selectDevice(" << index << ") called, device list size:" << m_deviceList.size();

    if (index < 0 || index >= m_deviceList.size()) {
        //qDebug() << "[DataController] selectDevice: index out of range";
        return;
    }

    m_triggerSourceList.clear();
    m_configure.deviceName = m_deviceList[index];
    //qDebug() << "[DataController] Selecting device:" << m_configure.deviceName;

    // 创建临时控制器读取设备信息，用完即销毁
    WaveformAiCtrl *tmpCtrl = WaveformAiCtrl::Create();
    if (!tmpCtrl) {
        //qDebug() << "[DataController] selectDevice: WaveformAiCtrl::Create() failed";
        return;
    }

    // 如果是远程设备，先登录
    if (m_configure.hostName != LOCAL_EDGE) {
        std::wstring hostName = m_configure.hostName.toStdWString();
        //qDebug() << "[DataController] selectDevice: Login to remote host:" << m_configure.hostName;
        if (Success != tmpCtrl->Login(hostName.c_str())) {
            //qDebug() << "[DataController] selectDevice: Login failed";
            emit errorOccurred(tr("Login failed! Please make sure the host name is valid."));
            tmpCtrl->Dispose();
            return;
        }
    }

    wchar_t description[MAX_DEVICE_DESC_LEN] = {0};
    m_configure.deviceName.toWCharArray(description);
    DeviceInformation selected(description);

    ErrorCode errorCode = tmpCtrl->setSelectedDevice(selected);
    //qDebug() << "[DataController] selectDevice: setSelectedDevice errorCode =" << Qt::hex << errorCode;

    if (errorCode != Success) {
        //qDebug() << "[DataController] selectDevice: setSelectedDevice failed";
        emit errorOccurred(QString("Error: the error code is 0x%1").arg(errorCode, 0, 16));
        tmpCtrl->Logout();
        tmpCtrl->Dispose();
        return;
    }

    // 读取触发源
    AiFeatures *features = tmpCtrl->getFeatures();
    m_isTriggerSupported = (features->getTriggerCount() > 0);
    //qDebug() << "[DataController] triggerSupported:" << m_isTriggerSupported
             //<< "triggerCount:" << features->getTriggerCount();

    if (m_isTriggerSupported) {
        wchar_t signalDes[128];
        SignalPosition sigPos;
        Array<SignalDrop>* sources = features->getTriggerSources();
        //qDebug() << "[DataController] Trigger sources:" << (sources ? sources->getLength() : -1);

        if (sources != nullptr) {
            for (int i = 0; i < sources->getLength(); i++) {
                AdxGetSignalConnectionInformation(sources->getItem(i), sizeof(signalDes),
                                                  signalDes, &sigPos);
                QString sigName = QString::fromWCharArray(signalDes);
                m_triggerSourceList.append(sigName);
                //qDebug() << "[DataController] Trigger source" << i << ":" << sigName;
            }
        }
    }

    //qDebug() << "[DataController] Trigger source list count:" << m_triggerSourceList.size();

    tmpCtrl->Logout();
    tmpCtrl->Dispose();
    emit triggerSourceListChanged();
}

void DataController::browseProfile()
{
    emit profileBrowseRequested();
}

void DataController::setProfilePath(const QString &path)
{
    if (!path.isEmpty()) {
        m_configure.profilePath = path;
    }
}

QString DataController::detectionRateText() const
{
    if (!m_dynamicDetectionRateCounter.hasResult())
        return QStringLiteral("--");
    return QString("%1%").arg(m_detectionRate, 0, 'f', 1);
}

QString DataController::detectionRateStateText() const
{
    switch (m_dynamicDetectionRateCounter.phase()) {
    case DynamicDetectionRateCounter::Phase::WaitingForThickness:
        return QStringLiteral("等待3帧稳定厚度");
    case DynamicDetectionRateCounter::Phase::Counting:
        return QStringLiteral("统计中");
    case DynamicDetectionRateCounter::Phase::Frozen:
        return QStringLiteral("最近%1帧内%2个NaN（至少连续%3帧），从首个NaN截止")
            .arg(DynamicDetectionRateCounter::WindowFrames)
            .arg(DynamicDetectionRateCounter::RequiredMissingFrames)
            .arg(DynamicDetectionRateCounter::RequiredConsecutiveMissingFrames);
    }
    return QString();
}

QString DataController::thicknessText() const
{
    return QString("%1 mm").arg(m_thickness, 0, 'f', 3);
}

QString DataController::snrText() const
{
    return QString("%1 dB").arg(m_snr, 0, 'f', 1);
}

QString DataController::maxThicknessText() const
{
    if (m_pipeFinalized)
        return m_pipeFinalSummary.valid
            ? QString("%1 mm").arg(m_pipeFinalSummary.maxMm, 0, 'f', 3)
            : QStringLiteral("-- mm");
    if (m_thicknessCount == 0) return "-- mm";
    return QString("%1 mm").arg(m_thicknessMax, 0, 'f', 3);
}

QString DataController::minThicknessText() const
{
    if (m_pipeFinalized)
        return m_pipeFinalSummary.valid
            ? QString("%1 mm").arg(m_pipeFinalSummary.minMm, 0, 'f', 3)
            : QStringLiteral("-- mm");
    if (m_thicknessCount == 0) return "-- mm";
    return QString("%1 mm").arg(m_thicknessMin, 0, 'f', 3);
}

QString DataController::avgThicknessText() const
{
    if (m_pipeFinalized)
        return m_pipeFinalSummary.valid
            ? QString("%1 mm").arg(m_pipeFinalSummary.meanMm, 0, 'f', 3)
            : QStringLiteral("-- mm");
    if (m_thicknessCount == 0) return "-- mm";
    double avg = m_thicknessSum / m_thicknessCount;
    return QString("%1 mm").arg(avg, 0, 'f', 3);
}

QString DataController::pipeLengthText() const
{
    if (!m_pipeFinalized || !m_pipeFinalSummary.valid) return QStringLiteral("-- m");
    // The axis starts at first effective radar motion, not a calibrated LB2 edge.
    return QStringLiteral("%1 m").arg(m_pipeFinalSummary.lengthMm / 1000.0, 0, 'f', 3);
}

QString DataController::thicknessEchoMode() const
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    return m_configure.thicknessAlgorithm.echoMode == ThicknessEchoMode::T2Only
        ? QStringLiteral("T2-only") : QStringLiteral("T1-only");
}

bool DataController::t2HighPrecisionDetectionEnabled() const
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    return m_configure.thicknessAlgorithm.t2HighPrecisionDetectionEnabled;
}

double DataController::avgThickness() const
{
    if (m_pipeFinalized)
        return m_pipeFinalSummary.valid ? m_pipeFinalSummary.meanMm : 0.0;
    if (m_thicknessCount == 0) return 0.0;
    return m_thicknessSum / m_thicknessCount;
}

void DataController::setAutoScale(bool enabled)
{
    if (m_autoScale != enabled) {
        m_autoScale = enabled;
        emit autoScaleChanged();
    }
}

void DataController::setShowOriginal(bool enabled)
{
    const bool previous = m_showOriginal.exchange(enabled, std::memory_order_relaxed);
    if (previous != enabled)
        emit showOriginalChanged();
}

void DataController::setSavePerFrame(bool enabled)
{
    if (m_savePerFrame.load(std::memory_order_acquire) == enabled)
        return;

    if (enabled) {
        const QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        const QString folder = QString("FrameData_%1").arg(timeStr);
        QDir dir;
        if (!dir.mkpath(folder)) {
            m_savePerFrame.store(false, std::memory_order_release);
            emit errorOccurred(tr("Cannot create save folder."));
            emit savePerFrameChanged();
            return;
        }
        {
            QMutexLocker locker(&m_fileMutex);
            m_saveFolder = folder;
        }
        ensureSaveWorkerStarted();
    }

    // 关闭开关只停止新帧入队，已经入队的帧继续由写盘线程按FIFO完整保存。
    m_savePerFrame.store(enabled, std::memory_order_release);
    emit savePerFrameChanged();
}

void DataController::ensureSaveWorkerStarted()
{
    std::lock_guard<std::mutex> locker(m_saveQueueMutex);
    if (m_saveWorker.joinable())
        return;
    m_saveWorkerStop = false;
    m_saveWorkerBusy = false;
    m_saveWorker = std::thread(&DataController::saveWorkerLoop, this);
}

void DataController::enqueueFrameSave(FrameSaveJob job)
{
    ensureSaveWorkerStarted();
    quint64 queueSize = 0;
    {
        std::lock_guard<std::mutex> locker(m_saveQueueMutex);
        m_saveQueue.push_back(std::move(job));
        queueSize = static_cast<quint64>(m_saveQueue.size());
    }

    quint64 previousPeak = m_saveQueuePeak.load(std::memory_order_relaxed);
    while (queueSize > previousPeak
           && !m_saveQueuePeak.compare_exchange_weak(previousPeak, queueSize,
                                                      std::memory_order_relaxed)) {
    }
    m_saveQueueCv.notify_one();
}

void DataController::drainSaveQueue()
{
    if (!m_saveWorker.joinable())
        return;
    std::unique_lock<std::mutex> locker(m_saveQueueMutex);
    m_saveQueueDrainedCv.wait(locker, [this]() {
        return m_saveQueue.empty() && !m_saveWorkerBusy;
    });
}

void DataController::flushPendingFrameSave()
{
    // Called by the processing thread or after its queue has drained.
    while (!m_pendingFrameSaves.empty()) {
        enqueueFrameSave(std::move(m_pendingFrameSaves.front()));
        m_pendingFrameSaves.pop_front();
    }
}

void DataController::stopSaveWorker()
{
    if (!m_saveWorker.joinable())
        return;
    drainSaveQueue();
    {
        std::lock_guard<std::mutex> locker(m_saveQueueMutex);
        m_saveWorkerStop = true;
    }
    m_saveQueueCv.notify_all();
    m_saveWorker.join();
}

void DataController::saveWorkerLoop()
{
    for (;;) {
        FrameSaveJob job;
        {
            std::unique_lock<std::mutex> locker(m_saveQueueMutex);
            m_saveQueueCv.wait(locker, [this]() {
                return m_saveWorkerStop || !m_saveQueue.empty();
            });
            if (m_saveWorkerStop && m_saveQueue.empty())
                break;

            job = std::move(m_saveQueue.front());
            m_saveQueue.pop_front();
            m_saveWorkerBusy = true;
        }

        QFile file(job.fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "Index,Original,Filtered,TOF(samples)\n";
            const QString tofStr = std::isfinite(job.tofSamples)
                ? QString::number(job.tofSamples, 'f', 6) : QStringLiteral("NaN");
            const int count = static_cast<int>((std::min)(job.original.size(), job.filtered.size()));
            for (int i = 0; i < count; ++i) {
                out << i << ","
                    << job.original[static_cast<size_t>(i)] << ","
                    << job.filtered[static_cast<size_t>(i)] << ","
                    << tofStr << "\n";
            }
            file.close();
        } else {
            qWarning() << "Cannot open file for writing:" << job.fileName;
        }

        {
            std::lock_guard<std::mutex> locker(m_saveQueueMutex);
            m_saveWorkerBusy = false;
            if (m_saveQueue.empty())
                m_saveQueueDrainedCv.notify_all();
        }
    }

    std::lock_guard<std::mutex> locker(m_saveQueueMutex);
    m_saveWorkerBusy = false;
    m_saveQueueDrainedCv.notify_all();
}

void DataController::setWaveformRefreshInterval(int interval)
{
    // 测量界面只提供 1 / 10 / 100 三档。该参数仅控制波形图和峰值标记的刷新抽帧，
    // 不影响采集频率、每帧测厚计算、厚度历史更新和历史数据记录。
    int normalized = 1;
    if (interval == 10)
        normalized = 10;
    else if (interval == 100)
        normalized = 100;

    const int previous = m_waveformRefreshInterval.exchange(normalized, std::memory_order_relaxed);
    if (previous == normalized)
        return;

    // 切换档位后从下一帧重新计数，保证新的刷新周期立即生效。
    m_waveformFrameCounter.store(0, std::memory_order_relaxed);
    emit waveformRefreshIntervalChanged();
}

void DataController::setHistoryRecording(bool enabled)
{
    qDebug() << "[DataController] setHistoryRecording called, enabled:" << enabled
             << "current:" << m_historyRecording
             << "historyMgr:" << (m_historyManager ? "set" : "null");
    if (m_historyRecording != enabled) {
        m_historyRecording = enabled;
        emit historyRecordingChanged();
        qDebug() << "[DataController] historyRecording changed to:" << m_historyRecording;
    }
}

void DataController::setHistoryManager(QObject *manager)
{
    m_historyManager = qobject_cast<HistoryManager*>(manager);
    qDebug() << "[DataController] setHistoryManager called, manager:" << (m_historyManager ? "valid" : "null");
}

void DataController::setTriggerSource(int index)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.triggerSourceIndex = index;
}

void DataController::setTriggerEdge(int index)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.triggerEdge = (index == 0) ? RisingEdge : FallingEdge;
}

void DataController::setDelayCount(int count)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.delayCount = count;
}

void DataController::setTriggerLevel(double level)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.triggerLevel = level;
}

void DataController::setDetectMode(const QString &mode)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.detectMode = mode;
}

void DataController::setThicknessStd(double val)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.thicknessStd = val;
}

void DataController::setThicknessTolerance(double val)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.thicknessTolerance = val;
}

void DataController::setThicknessEchoMode(const QString &mode)
{
    if (m_running) {
        emit errorOccurred(tr("请先停止采集，再切换T1/T2测厚模式。"));
        return;
    }

    const ThicknessEchoMode next = mode.compare(QStringLiteral("T2-only"), Qt::CaseInsensitive) == 0
        ? ThicknessEchoMode::T2Only : ThicknessEchoMode::T1Only;
    {
        std::lock_guard<std::mutex> locker(m_configMutex);
        auto &p = m_configure.thicknessAlgorithm;
        if (p.echoMode == next)
            return;
        p.echoMode = next;
        ++p.revision;
    }
    m_thicknessProcessor.reset();
    emit thicknessEchoModeChanged();
}

void DataController::setT2HighPrecisionDetectionEnabled(bool enabled)
{
    if (m_running) {
        emit errorOccurred(tr("请先停止采集，再切换T2高精度检测。"));
        return;
    }
    {
        std::lock_guard<std::mutex> locker(m_configMutex);
        auto &p = m_configure.thicknessAlgorithm;
        if (p.t2HighPrecisionDetectionEnabled == enabled)
            return;
        // Output-only change; do not change algorithm revision, replay
        // templates, reset the tracker, or modify any SNR thresholds.
        p.t2HighPrecisionDetectionEnabled = enabled;
    }
    emit t2HighPrecisionDetectionEnabledChanged();
}

void DataController::setStructLen(int val)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.structLen = val;
}

void DataController::setMinWidth(int val)
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    m_configure.minWidth = val;
}

void DataController::setThicknessAlgorithmParameter(const QString &name, double value)
{
    // 高级参数涉及跨帧历史、模板和噪声底，只允许停采时修改。
    if (m_running) {
        emit errorOccurred(tr("请先停止采集，再修改核心测厚参数。"));
        return;
    }
    if (!std::isfinite(value))
        return;

    std::lock_guard<std::mutex> locker(m_configMutex);
    auto &p = m_configure.thicknessAlgorithm;
    const auto asInt = [value](int minimum, int maximum) {
        return std::clamp(static_cast<int>(std::lround(value)), minimum, maximum);
    };
    const auto bounded = [value](double minimum, double maximum) {
        return std::clamp(value, minimum, maximum);
    };

    bool recognized = true;
    if (name == QStringLiteral("filterOrder")) {
        m_filterOrder = asInt(1, 1000);
        return;
    } else if (name == QStringLiteral("impulseMadMultiplier")) {
        p.impulseMadMultiplier = bounded(0.1, 1000.0);
    } else if (name == QStringLiteral("impulseMinJumpV")) {
        p.impulseMinJumpV = bounded(0.000001, 100.0);
    } else if (name == QStringLiteral("impulseIsolationRatio")) {
        p.impulseIsolationRatio = bounded(5.0, 100.0);
    } else if (name == QStringLiteral("impulseGuardSamples")) {
        p.impulseGuardSamples = asInt(0, 1000);
    } else if (name == QStringLiteral("energyWindowSamples")) {
        int window = asInt(3, 255);
        if ((window & 1) == 0)
            window = window < 255 ? window + 1 : window - 1;
        p.energyWindowSamples = window;
    } else if (name == QStringLiteral("minLocalSnrDb")) {
        p.minLocalSnrDb = bounded(0.1, 100.0);
        p.lowSnrSoftMinDb = std::min(p.lowSnrSoftMinDb, p.minLocalSnrDb);
    } else if (name == QStringLiteral("lowSnrSoftMinDb")) {
        p.lowSnrSoftMinDb = std::min(bounded(0.1, 30.0), p.minLocalSnrDb);
    } else if (name == QStringLiteral("lowSnrSoftMinTrackingCorrelation")) {
        p.lowSnrSoftMinTrackingCorrelation = bounded(0.1, 1.0);
    } else if (name == QStringLiteral("lowSnrSoftMaxResidualSamples")) {
        p.lowSnrSoftMaxResidualSamples = bounded(0.1, 20.0);
    } else if (name == QStringLiteral("lowSnrSoftMinFitR2")) {
        p.lowSnrSoftMinFitR2 = bounded(0.1, 0.9999);
    } else if (name == QStringLiteral("noiseMadRejectMultiplier")) {
        p.noiseMadRejectMultiplier = bounded(1.0, 10.0);
    } else if (name == QStringLiteral("noiseEwmaAlphaUp")) {
        p.noiseEwmaAlphaUp = bounded(0.01, 1.0);
    } else if (name == QStringLiteral("noiseEwmaAlphaDown")) {
        p.noiseEwmaAlphaDown = bounded(0.001, 1.0);
    } else if (name == QStringLiteral("minCandidateRatio")) {
        p.minCandidateRatio = bounded(1.01, 100.0);
    } else if (name == QStringLiteral("candidateSeparationSamples")) {
        p.candidateSeparationSamples = asInt(1, 1000);
    } else if (name == QStringLiteral("predictionHistorySize")) {
        p.predictionHistorySize = asInt(1, 32);
        p.predictionMinValid = std::min(p.predictionMinValid, p.predictionHistorySize);
    } else if (name == QStringLiteral("predictionMinValid")) {
        p.predictionMinValid = asInt(1, p.predictionHistorySize);
    } else if (name == QStringLiteral("predictionHalfWidth")) {
        p.predictionHalfWidth = asInt(1, 1000);
    } else if (name == QStringLiteral("predictionMaxMisses")) {
        p.predictionMaxMisses = asInt(1, 100);
    } else if (name == QStringLiteral("trackingResidualAcceptSamples")) {
        p.trackingResidualAcceptSamples = bounded(0.5, 100.0);
        p.trackingResidualRejectSamples =
            std::max(p.trackingResidualRejectSamples, p.trackingResidualAcceptSamples);
    } else if (name == QStringLiteral("trackingResidualRejectSamples")) {
        p.trackingResidualRejectSamples =
            std::max(bounded(0.5, 200.0), p.trackingResidualAcceptSamples);
    } else if (name == QStringLiteral("reacquireConfirmFrames")) {
        p.reacquireConfirmFrames = asInt(2, 10);
    } else if (name == QStringLiteral("reacquireConsistencySamples")) {
        p.reacquireConsistencySamples = bounded(0.5, 100.0);
    } else if (name == QStringLiteral("longitudinalVelocityMps")) {
        p.longitudinalVelocityMps = bounded(1.0, 20000.0);
    } else if (name == QStringLiteral("echoMinCorrelation")) {
        p.echoMinCorrelation = bounded(0.1, 1.0);
    } else if (name == QStringLiteral("echoMinSnrDb")) {
        p.echoMinSnrDb = bounded(0.1, 30.0);
    } else if (name == QStringLiteral("echoMinRelativeEnergy")) {
        p.echoMinRelativeEnergy = bounded(0.001, 0.5);
    } else if (name == QStringLiteral("echoPeriodTolerance")) {
        p.echoPeriodTolerance = bounded(0.01, 0.4);
    } else if (name == QStringLiteral("packetMinWidth")) {
        p.packetMinWidth = asInt(5, std::max(5, p.packetMaxWidth - 1));
    } else if (name == QStringLiteral("packetMaxWidth")) {
        p.packetMaxWidth = asInt(p.packetMinWidth + 1, 250);
        p.lockedPacketMaxCoreWidth =
            std::max(p.lockedPacketMaxCoreWidth, p.packetMaxWidth);
    } else if (name == QStringLiteral("packetMergeGap")) {
        p.packetMergeGap = asInt(0, 20);
    } else if (name == QStringLiteral("packetEnergyFraction")) {
        p.packetEnergyFraction = bounded(0.001, 0.5);
    } else if (name == QStringLiteral("lockedPacketMaxCoreWidth")) {
        p.lockedPacketMaxCoreWidth = asInt(p.packetMaxWidth, 250);
    } else if (name == QStringLiteral("lockedBroadMinTrackingCorrelation")) {
        p.lockedBroadMinTrackingCorrelation = bounded(0.1, 1.0);
    } else if (name == QStringLiteral("lockedBroadEnergyPeakToleranceSamples")) {
        p.lockedBroadEnergyPeakToleranceSamples = bounded(0.5, 50.0);
    } else if (name == QStringLiteral("fitFootFraction")) {
        p.fitFootFraction = bounded(0.01, 0.80);
    } else if (name == QStringLiteral("fitFootConfirmSamples")) {
        p.fitFootConfirmSamples = asInt(1, 20);
    } else if (name == QStringLiteral("fitFootSmoothRadius")) {
        p.fitFootSmoothRadius = asInt(0, 8);
    } else if (name == QStringLiteral("fitCenterHalfRange")) {
        p.fitCenterHalfRange = bounded(0.01, 10.0);
    } else if (name == QStringLiteral("fitAmplitudeMinRatio")) {
        p.fitAmplitudeMinRatio = bounded(0.05, 3.0);
        p.fitAmplitudeMaxRatio =
            std::max(p.fitAmplitudeMaxRatio, p.fitAmplitudeMinRatio);
    } else if (name == QStringLiteral("fitAmplitudeMaxRatio")) {
        p.fitAmplitudeMaxRatio =
            std::max(bounded(0.05, 5.0), p.fitAmplitudeMinRatio);
    } else if (name == QStringLiteral("fitMinR2")) {
        p.fitMinR2 = bounded(0.1, 0.9999);
    } else if (name == QStringLiteral("firstEchoPhaseRefinementAlpha")) {
        p.firstEchoPhaseRefinementAlpha = bounded(0.0, 1.0);
    } else if (name == QStringLiteral("adaptiveWidthHistorySize")) {
        p.adaptiveWidthHistorySize = asInt(5, 101);
        p.adaptiveWidthMinHistory =
            std::min(p.adaptiveWidthMinHistory, p.adaptiveWidthHistorySize);
    } else if (name == QStringLiteral("adaptiveWidthMinHistory")) {
        p.adaptiveWidthMinHistory = asInt(3, p.adaptiveWidthHistorySize);
    } else if (name == QStringLiteral("adaptiveWidthMadMultiplier")) {
        p.adaptiveWidthMadMultiplier = bounded(1.0, 12.0);
    } else if (name == QStringLiteral("adaptiveWidthMinToleranceSamples")) {
        p.adaptiveWidthMinToleranceSamples = bounded(0.5, 20.0);
    } else if (name == QStringLiteral("adaptiveWidthOutlierMultiplier")) {
        p.adaptiveWidthOutlierMultiplier = bounded(1.1, 5.0);
    } else if (name == QStringLiteral("trackingMinCorrelation")) {
        p.trackingMinCorrelation = bounded(0.1, 1.0);
    } else if (name == QStringLiteral("trackingCorrelationMargin")) {
        p.trackingCorrelationMargin = bounded(0.001, 1.0);
    } else if (name == QStringLiteral("trackingEmaTemplateAlpha")) {
        p.trackingEmaTemplateAlpha = bounded(0.01, 1.0);
    } else if (name == QStringLiteral("trackingShapeTemplateAlpha")) {
        p.trackingShapeTemplateAlpha = bounded(0.01, 1.0);
    } else if (name == QStringLiteral("trackingMultiTemplateAgreementSamples")) {
        p.trackingMultiTemplateAgreementSamples = bounded(0.25, 10.0);
    } else if (name == QStringLiteral("trackingBackupTemplateMaxResidualSamples")) {
        p.trackingBackupTemplateMaxResidualSamples = bounded(0.25, 6.0);
    } else if (name == QStringLiteral("trackingBackupRecentBlendAlpha")) {
        p.trackingBackupRecentBlendAlpha = bounded(0.01, 1.0);
    } else if (name == QStringLiteral("trackingSoftCorrelation")) {
        p.trackingSoftCorrelation = bounded(0.1, 1.0);
    } else if (name == QStringLiteral("trackingSoftFitPredictionToleranceSamples")) {
        p.trackingSoftFitPredictionToleranceSamples = bounded(0.1, 20.0);
    } else if (name == QStringLiteral("trackingSoftMinFitR2")) {
        p.trackingSoftMinFitR2 = bounded(0.1, 0.9999);
    } else if (name == QStringLiteral("trackingSoftMinMaxSnrDb")) {
        p.trackingSoftMinMaxSnrDb = bounded(0.1, 60.0);
    } else {
        recognized = false;
    }

    if (recognized)
        ++p.revision;
}

void DataController::applySettings()
{
    m_configure.channelStart = 0;
    m_configure.channelCount = 1;
    m_configure.valueRange = V_Neg5To5;
    m_configure.clockRatePerChan = 160000000.0;
    m_configure.sectionLength = 1600;
    m_configure.cycles = 0;
    m_configure.triggerAction = DelayToStart;
}

void DataController::configureDevice()
{
    //qDebug() << "[DataController] configureDevice() entered";

    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl) {
        //qDebug() << "[DataController] configureDevice: ctrl is null";
        return;
    }

    ErrorCode errorCode = Success;

    if (m_scaledData) {
        delete[] m_scaledData;
        m_scaledData = nullptr;
    }

    m_rawDataBufferLength = m_configure.sectionLength * m_configure.channelCount;
    m_scaledData = new double[m_rawDataBufferLength];
    if (!m_scaledData) {
        emit errorOccurred(tr("Sorry! Error in allocating memory...."));
        return;
    }

    errorCode = ctrl->Logout();
    //qDebug() << "[DataController] configureDevice: Logout =" << Qt::hex << errorCode;
    checkError(errorCode);

    if (m_configure.hostName != LOCAL_EDGE) {
        std::wstring hostName = m_configure.hostName.toStdWString();
        //qDebug() << "[DataController] configureDevice: Login to" << m_configure.hostName;
        if (Success != ctrl->Login(hostName.c_str())) {
            //qDebug() << "[DataController] configureDevice: Login failed";
            emit errorOccurred(tr("Login failed! Please make sure the host name is valid."));
            return;
        }
    }

    wchar_t description[MAX_DEVICE_DESC_LEN] = {0};
    m_configure.deviceName.toWCharArray(description);
    DeviceInformation selected(description);
    errorCode = ctrl->setSelectedDevice(selected);
    //qDebug() << "[DataController] configureDevice: setSelectedDevice =" << Qt::hex << errorCode;
    checkError(errorCode);

    std::wstring profile = m_configure.profilePath.toStdWString();
    if (!profile.empty()) {
        errorCode = ctrl->LoadProfile(profile.c_str());
        checkError(errorCode);
    }

    errorCode = ctrl->getConversion()->setChannelCount(m_configure.channelCount);
    checkError(errorCode);
    errorCode = ctrl->getConversion()->setChannelStart(m_configure.channelStart);
    checkError(errorCode);
    errorCode = ctrl->getConversion()->setClockRate(m_configure.clockRatePerChan);
    checkError(errorCode);

    errorCode = ctrl->getRecord()->setSectionLength(m_configure.sectionLength);
    checkError(errorCode);
    errorCode = ctrl->getRecord()->setSectionCount(1);
    checkError(errorCode);
    errorCode = ctrl->getRecord()->setCycles(m_configure.cycles);
    checkError(errorCode);

    for (int i = 0; i < ctrl->getChannels()->getCount(); i++) {
        errorCode = ctrl->getChannels()->getItem(i).setValueRange(
            static_cast<ValueRange>(m_configure.valueRange));
        checkError(errorCode);
    }

    if (ctrl->getFeatures()->getTriggerCount() > 0) {
        Trigger *trigger = ctrl->getTrigger();
        errorCode = trigger->setAction(static_cast<TriggerAction>(m_configure.triggerAction));
        checkError(errorCode);

        Array<SignalDrop>* sources = ctrl->getFeatures()->getTriggerSources();
        if (sources != nullptr && sources->getLength() > 0) {
            int idx = m_configure.triggerSourceIndex;
            if (idx >= 0 && idx < sources->getLength()) {
                SignalDrop src = sources->getItem(idx);
                errorCode = trigger->setSource(src);
                checkError(errorCode);
            }
        }

        double refRate = 80.0e6;
        double factor = refRate / m_configure.clockRatePerChan;
        m_configure.actualDelayCount = (int)qRound(m_configure.delayCount * factor);
        if (m_configure.actualDelayCount < 0) m_configure.actualDelayCount = 0;

        errorCode = trigger->setDelayCount(m_configure.actualDelayCount);
        checkError(errorCode);
        errorCode = trigger->setEdge(static_cast<ActiveSignal>(m_configure.triggerEdge));
        checkError(errorCode);
        errorCode = trigger->setLevel(m_configure.triggerLevel);
        checkError(errorCode);
    }

    errorCode = ctrl->Prepare();
    checkError(errorCode);

    // 计算采样间隔 xInc
    m_xInc = 1.0 / ctrl->getConversion()->getClockRate();
    //qDebug() << "[DataController] configureDevice: xInc =" << m_xInc;
}

void DataController::configureGraph()
{
    // 根据采样率计算时间轴刻度
    double clockRate = m_configure.clockRatePerChan;
    int32 sectionLen = m_configure.sectionLength;
    double totalTimeSec = sectionLen / clockRate;
    double totalTimeUs = totalTimeSec * 1e6;
    double totalTimeDiv = totalTimeUs / 10.0;

    int timeUnit = 1; // Microsecond

    // 获取 Y 轴量程
    ValueUnit unit;
    MathInterval rangeY;
    ErrorCode errorCode = AdxGetValueRangeInformation(
        static_cast<ValueRange>(m_configure.valueRange), 0, nullptr, &rangeY, &unit);
    if (BioFailed(errorCode)) {
        //qDebug() << "[DataController] configureGraph: AdxGetValueRangeInformation failed";
        rangeY.Max = 5.0;
        rangeY.Min = -5.0;
        unit = Volt;
    }

    //qDebug() << "[DataController] configureGraph: xCordTimeDiv =" << totalTimeDiv
             //<< "yMin =" << rangeY.Min << "yMax =" << rangeY.Max
             //<< "timeUnit =" << timeUnit << "yUnit =" << unit;

    emit graphConfigReady(totalTimeDiv, 0.0, timeUnit, rangeY.Min, rangeY.Max, (int)unit);
}

void DataController::checkError(int errorCode)
{
    if (BioFailed(errorCode)) {
        QString message = tr("Sorry, there are some errors occurred, Error Code: 0x") +
            QString::number(errorCode, 16).right(8).toUpper();
        emit errorOccurred(message);
    }
}

void DataController::setSharedRadarSource(RadarUdpSharedService *source)
{
    if (m_radarPositionSidecar)
        m_radarPositionSidecar->setSharedRadarSource(source);
}

void DataController::setRadarPositionSidecarEnabled(bool enabled)
{
    if (m_running) {
        emit errorOccurred(tr("请先停止采集，再修改轴向壁厚曲线CSV保存设置。"));
        return;
    }
    if (m_radarPositionSidecarEnabled == enabled)
        return;
    m_radarPositionSidecarEnabled = enabled;
    m_radarSidecarStatus = enabled
        ? QStringLiteral("下次采集将保存轴向位置、壁厚和SNR曲线CSV（实时曲线始终运行）")
        : QStringLiteral("下次采集只显示实时轴向壁厚曲线，不保存CSV");
    emit radarSidecarStatusChanged();
    emit radarPositionSidecarEnabledChanged();
}

QVariantList DataController::axialThicknessProfile() const
{
    QVariantList result;
    result.reserve(m_axialPoints.size());
    for (const auto &p : m_axialPoints) {
        QVariantMap point;
        point.insert(QStringLiteral("x"), p.positionMm);
        point.insert(QStringLiteral("y"), p.thicknessMm);
        point.insert(QStringLiteral("frameId"), QVariant::fromValue(p.frameId));
        result.append(point);
    }
    return result;
}

void DataController::updateAxialPoint(quint64 frameId, double positionMm,
                                      double thicknessMm, const QString &quality)
{
    if (m_pipeFinalized ||
        quality != QStringLiteral("RADAR_FIRST_MOTION_MINUS_DISTANCE_UNCALIBRATED") ||
        !std::isfinite(positionMm) || positionMm < 0 ||
        !std::isfinite(thicknessMm)) return;
    bool replaced = false;
    for (int i = static_cast<int>(m_axialPoints.size()) - 1; i >= 0; --i) {
        if (m_axialPoints[i].frameId == frameId) {
            m_axialPoints[i] = {frameId, positionMm, thicknessMm};
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_axialPoints.push_back({frameId, positionMm, thicknessMm});
        if (m_axialPoints.size() > 2500) m_axialPoints.pop_front();
    }
    // Retain this pipe's real measured thicknesses across the entire run,
    // independently of the bounded 2500-point live-display window.
    const auto it = std::lower_bound(
        m_pipeMeasuredPoints.begin(), m_pipeMeasuredPoints.end(), frameId,
        [](const auto &p, quint64 id) { return p.frameId < id; });
    const AxialPipeFinalizer::Point p{frameId, positionMm, thicknessMm, false};
    if (it != m_pipeMeasuredPoints.end() && it->frameId == frameId)
        *it = p;
    else
        m_pipeMeasuredPoints.insert(it, p);
    // Transfer just one typed point; no 2500-entry QVariantMap conversion.
    emit axialPointAdded(frameId, positionMm, thicknessMm);
    const qint64 nowUs = RadarHostClock::nowUs();
    if (m_lastAxialUiUs < 0 || nowUs - m_lastAxialUiUs >= 100000) {
        m_lastAxialUiUs = nowUs;
        emit axialThicknessProfileChanged();
    }
}

void DataController::setRadarToLaserDistanceMm(double mm)
{
    // Physical radar -> laser installation distance. Frozen per acquisition:
    // head-arrival gate, BDAQ-aligned axial x, CSV geometry, UI pipe-presence
    // estimate, and the optional end-of-pipe extrapolation share one value.
    // Raw UDP 0.1mm/count and ultrasonic TOF formula are separate calibrations.
    if (m_running || !std::isfinite(mm) ||
        mm <= RadarLaserGeometry::RadarToLb2Mm ||
        mm > RadarLaserGeometry::MaximumRadarToLaserMm) return;
    if (std::abs(mm - m_radarToLaserDistanceMm) < 1e-9) return;
    m_radarToLaserDistanceMm = mm;
    emit radarToLaserDistanceMmChanged();
}

void DataController::clearAxialPlotData()
{
    // Keep the controller and QQuickItem snapshots consistent. Clearing the
    // GPU item alone would let an old 2500-point snapshot reappear on reopen.
    m_axialPoints.clear();
    m_pipeMeasuredPoints.clear();
    m_pipeFinalSummary = {};
    m_pipeFinalized = false;
    emit thicknessStatsChanged();
    m_tailFillPoints.clear();
    m_tailFillStatus = QStringLiteral("等待雷达失效后仍有测厚，最近10帧内5个NaN且至少3帧连续后自动重绘");
    emit tailFillPointsChanged();
    m_lastAxialUiUs = -1;
    emit axialProfileReset();
    emit axialThicknessProfileChanged();
}

void DataController::finalizeAxialPlotData()
{
    std::vector<AxialPipeFinalizer::TailPoint> tail;
    tail.reserve(static_cast<size_t>(m_tailFillPoints.size()));
    for (const auto &p : m_tailFillPoints)
        tail.push_back({p.positionMm, p.thicknessMm});
    m_pipeFinalSummary = AxialPipeFinalizer::build(m_pipeMeasuredPoints, tail);
    m_pipeFinalized = true;
    emit thicknessStatsChanged();
    emit axialProfileFinalized();
}

void DataController::startAcquisition()
{
    qDebug() << "[DataController] startAcquisition() called";

    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl) {
        qDebug() << "[DataController] startAcquisition: m_waveformAiCtrl is null";
        return;
    }

    applySettings();
    configureDevice();
    configureGraph();

    m_running = true;
    m_paused = false;
    {
        QMutexLocker locker(&m_fileMutex);
        m_saveFrameCounter = 0;
    }
    m_saveQueuePeak.store(0, std::memory_order_relaxed);
    m_processingQueuePeak.store(0, std::memory_order_relaxed);
    m_waveformFrameCounter.store(0, std::memory_order_relaxed);
    m_acquiredFrameSerial.store(0, std::memory_order_relaxed);
    clearAxialPlotData();
    m_thicknessMax = -std::numeric_limits<double>::max();
    m_thicknessMin = std::numeric_limits<double>::max();
    m_thicknessSum = 0.0;
    m_thicknessCount = 0;
    emit thicknessStatsChanged();
    m_dynamicDetectionRateCounter.reset();
    updateDetectionRate();
    emit measurementDisplayReset(); // also clear the ordinary thickness and waveform plots
    m_laserHeadThicknessConfirmed = false;
    emit laserHeadThicknessConfirmedChanged();
    m_lastAxialUiUs = -1;
    m_axialOriginStatus = QStringLiteral(
        "新采集已开始：需三帧可靠测厚且首次雷达运动后前进%1mm才能绘制轴向曲线")
        .arg(m_radarToLaserDistanceMm, 0, 'f', 1);
    emit axialOriginStatusChanged();
    // Real-time pipe-axis profile is mandatory during DAQ; this switch
    // controls optional CSV persistence only.
    if (m_radarPositionSidecar)
        m_radarPositionSidecar->startSession(m_radarPositionSidecarEnabled, m_radarToLaserDistanceMm);
    // 新批次不能沿用上一批次的峰位、波包模板和噪声底。
    m_thicknessProcessor.reset();
    ensureProcessingWorkerStarted();

    emit runningChanged();
    emit pausedChanged();
    emit clearMarkersRequested();

    ErrorCode errorCode = ctrl->Start();
    //qDebug() << "[DataController] ctrl->Start() errorCode =" << Qt::hex << errorCode;
    if (BioFailed(errorCode)) {
        m_running = false;
        if (m_radarPositionSidecar)
            m_radarPositionSidecar->stopSession();
        emit runningChanged();
        emit errorOccurred(tr("Start failed! Error Code: 0x%1").arg(errorCode, 0, 16).toUpper());
        return;
    }
}

void DataController::pauseAcquisition()
{
    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl || !m_running || m_paused) return;

    ErrorCode errorCode = ctrl->Stop();
    if (BioFailed(errorCode)) {
        checkError(errorCode);
        return;
    }

    // Stop()返回后不再接收新采集帧；等待已经获取的帧按FIFO处理完，保证暂停点无遗漏。
    drainProcessingQueue();
    flushPendingResultCommits();
    drainSaveQueue();

    m_paused = true;
    emit pausedChanged();
}

void DataController::resumeAcquisition()
{
    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl || !m_running || !m_paused) return;

    ErrorCode errorCode = ctrl->Start();
    if (BioFailed(errorCode)) {
        checkError(errorCode);
        return;
    }

    m_paused = false;
    emit pausedChanged();
}

void DataController::stopAcquisition()
{
    qDebug() << "[DataController] stopAcquisition() called, sessionActive:"
             << (m_historyManager ? (m_historyManager->isSessionActive() ? "true" : "false") : "no mgr");

    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl) return;

    ErrorCode errorCode = ctrl->Stop();
    checkError(errorCode);
    // Capture exactly the last radar UDP received by DAQ Stop(), rather than
    // one arriving later while processing/backfill queues are being drained.
    if (m_radarPositionSidecar)
        m_radarPositionSidecar->markMeasurementStopped();
    m_running = false;
    m_paused = false;

    // DAQ已经停止后先排空处理队列；处理线程可能继续产生保存任务，因此随后再排空写盘队列。
    drainProcessingQueue();
    flushPendingFrameSave();
    // Flush queued GUI thickness commits and T2 backfills before joined CSV closes.
    flushPendingResultCommits();
    if (m_radarPositionSidecar)
        m_radarPositionSidecar->stopSession();
    drainSaveQueue();
    const quint64 processingQueuePeak = m_processingQueuePeak.load(std::memory_order_relaxed);
    if (processingQueuePeak > 0)
        qDebug() << "[DataController] processing peak queue depth:" << processingQueuePeak;
    const quint64 saveQueuePeak = m_saveQueuePeak.load(std::memory_order_relaxed);
    if (saveQueuePeak > 0)
        qDebug() << "[DataController] per-frame save peak queue depth:" << saveQueuePeak;

    // Finish history session
    if (m_historyManager && m_historyManager->isSessionActive()) {
        qDebug() << "[DataController] stopAcquisition: finishing history session, data count:" << m_historyManager->sessionDataCount();
        m_historyManager->finishSession();
    }

    {
        QMutexLocker locker(&m_thicknessMutex);
        m_thicknessHistory.clear();
        m_thicknessFrameIds.clear();
    }

    emit runningChanged();
    emit pausedChanged();
}

void DataController::applyFilter(double lowFreqMHz, double highFreqMHz)
{
    std::lock_guard<std::mutex> configLocker(m_configMutex);
    double sampleRate = m_configure.clockRatePerChan;
    double nyquist = sampleRate / 2.0 / 1e6;

    if (lowFreqMHz <= 0 || highFreqMHz <= lowFreqMHz || highFreqMHz >= nyquist) {
        emit errorOccurred(QString("Please enter valid frequencies (0 < low < high < %1 MHz).").arg(nyquist));
        return;
    }

    m_configure.filterLowFreqMHz = lowFreqMHz;
    m_configure.filterHighFreqMHz = highFreqMHz;
}

void DataController::applyYRange(double yMin, double yMax)
{
    if (yMin >= yMax) {
        emit errorOccurred(tr("Range Error: min >= max"));
        return;
    }
    // Y range is handled by the graph item
}

void DataController::resetDetectionRate()
{
    // The Reset button resets BOTH wall-thickness plots as well as their
    // controller-side caches. When DAQ is running, this is DISPLAY-only for
    // axial positioning: never silently alter the LB2 head origin mid-pipe.
    clearAxialPlotData();
    emit measurementDisplayReset();
    if (!m_running) {
        m_laserHeadThicknessConfirmed = false;
        emit laserHeadThicknessConfirmedChanged();
        m_axialOriginStatus = QStringLiteral(
            "已重置；下次开始采集时重新建立管头候选零点");
        emit axialOriginStatusChanged();
    }
    m_dynamicDetectionRateCounter.reset();
    m_detectionRate = 0.0;
    m_snr = 0.0;
    m_thicknessMax = -std::numeric_limits<double>::max();
    m_thicknessMin = std::numeric_limits<double>::max();
    m_thicknessSum = 0.0;
    m_thicknessCount = 0;

    {
        QMutexLocker locker(&m_thicknessMutex);
        m_thicknessHistory.clear();
        m_thicknessFrameIds.clear();
    }

    emit detectionRateChanged();
    emit snrChanged();
    emit thicknessStatsChanged();
}

void DataController::saveThicknessData(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorOccurred(tr("Cannot create file."));
        return;
    }

    QTextStream out(&file);
    out << "Index,Thickness(mm)\n";

    QMutexLocker locker(&m_thicknessMutex);
    for (int i = 0; i < m_thicknessHistory.size(); ++i) {
        double val = m_thicknessHistory[i];
        if (std::isnan(val)) {
            out << i + 1 << ",NaN\n";
        } else {
            out << i + 1 << "," << QString::number(val, 'f', 6) << "\n";
        }
    }
    file.close();
}

QVector<double> DataController::getThicknessHistory() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_thicknessMutex));
    return m_thicknessHistory;
}

void *DataController::waveformAiCtrl() const { return m_waveformAiCtrl; }

void DataController::updateDetectionRate()
{
    // Only the active confirmed pipe segment contributes to this rate.
    // Before the first stable triple show "--"; freeze at the first NaN
    // of a confirmed 10-frame / 5-NaN / 3-consecutive-NaN end vote.
    m_detectionRate = m_dynamicDetectionRateCounter.percent();
    emit detectionRateChanged();
}

void DataController::ensureProcessingWorkerStarted()
{
    std::lock_guard<std::mutex> locker(m_processingQueueMutex);
    if (m_processingWorker.joinable())
        return;
    m_processingWorkerStop = false;
    m_processingWorkerBusy = false;
    m_processingWorker = std::thread(&DataController::processingWorkerLoop, this);
}

void DataController::enqueueAcquiredFrame(AcquiredFrameJob job)
{
    ensureProcessingWorkerStarted();
    quint64 queueSize = 0;
    {
        std::lock_guard<std::mutex> locker(m_processingQueueMutex);
        m_processingQueue.push_back(std::move(job));
        queueSize = static_cast<quint64>(m_processingQueue.size());
    }
    quint64 previousPeak = m_processingQueuePeak.load(std::memory_order_relaxed);
    while (queueSize > previousPeak
           && !m_processingQueuePeak.compare_exchange_weak(previousPeak, queueSize,
                                                            std::memory_order_relaxed)) {
    }
    m_processingQueueCv.notify_one();
}

void DataController::drainProcessingQueue()
{
    if (!m_processingWorker.joinable())
        return;
    std::unique_lock<std::mutex> locker(m_processingQueueMutex);
    m_processingQueueDrainedCv.wait(locker, [this]() {
        return m_processingQueue.empty() && !m_processingWorkerBusy;
    });
}

void DataController::stopProcessingWorker()
{
    if (!m_processingWorker.joinable())
        return;
    drainProcessingQueue();
    {
        std::lock_guard<std::mutex> locker(m_processingQueueMutex);
        m_processingWorkerStop = true;
    }
    m_processingQueueCv.notify_all();
    m_processingWorker.join();
}

void DataController::flushPendingResultCommits()
{
    // pause/stop由DataController所属Qt线程调用。处理线程已经排空后，不会再新增提交；
    // 定向执行本对象的MetaCall，确保最后几帧先写入厚度历史/HistoryManager，再结束会话。
    if (QThread::currentThread() != thread())
        return;
    while (m_pendingResultCommits.load(std::memory_order_acquire) > 0)
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

void DataController::processingWorkerLoop()
{
    for (;;) {
        AcquiredFrameJob job;
        {
            std::unique_lock<std::mutex> locker(m_processingQueueMutex);
            m_processingQueueCv.wait(locker, [this]() {
                return m_processingWorkerStop || !m_processingQueue.empty();
            });
            if (m_processingWorkerStop && m_processingQueue.empty())
                break;
            job = std::move(m_processingQueue.front());
            m_processingQueue.pop_front();
            m_processingWorkerBusy = true;
        }

        processAcquiredFrame(std::move(job));

        {
            std::lock_guard<std::mutex> locker(m_processingQueueMutex);
            m_processingWorkerBusy = false;
            if (m_processingQueue.empty())
                m_processingQueueDrainedCv.notify_all();
        }
    }

    std::lock_guard<std::mutex> locker(m_processingQueueMutex);
    m_processingWorkerBusy = false;
    m_processingQueueDrainedCv.notify_all();
}

void DataController::processAcquiredFrame(AcquiredFrameJob job)
{
    QElapsedTimer processingTimer;
    processingTimer.start();

    const int pointsPerChan = static_cast<int>(job.original.size());
    if (pointsPerChan <= 0)
        return;
    const ConfigureParameter &cfg = job.configure;

    static thread_local std::vector<double> combined;
    static thread_local std::vector<double> filtered;
    if (combined.capacity() < static_cast<size_t>(2 * cfg.sectionLength))
        combined.reserve(2 * cfg.sectionLength);
    if (filtered.capacity() < static_cast<size_t>(cfg.sectionLength))
        filtered.reserve(cfg.sectionLength);

    // 只有完整采集帧进入测厚；滤波结果由测试程序同源核心一次性生成，
    // 后续界面显示和逐帧保存直接复用，避免实时链存在第二套预处理实现。
    const bool completeWaveform = (pointsPerChan == cfg.sectionLength);
    ThicknessResult thicknessResult;
    if (completeWaveform) {
        ThicknessDetectionMode detectionMode = ThicknessDetectionMode::Auto;
        if (cfg.detectMode == QStringLiteral("peak"))
            detectionMode = ThicknessDetectionMode::Peak;
        else if (cfg.detectMode == QStringLiteral("valley"))
            detectionMode = ThicknessDetectionMode::Valley;

        thicknessResult = m_thicknessProcessor.process(
            job.original,
            &filtered,
            cfg.clockRatePerChan,
            cfg.filterLowFreqMHz,
            cfg.filterHighFreqMHz,
            cfg.delayCount,
            cfg.thicknessStd,
            cfg.thicknessTolerance,
            cfg.structLen,
            cfg.minWidth,
            detectionMode,
            job.filterOrder,
            cfg.thicknessAlgorithm);

        // Raw脉冲门控可能在滤波前直接结束；此时仍用Raw维持显示/保存列长度一致。
        if (filtered.size() != job.original.size())
            filtered = job.original;
    }

    // UI只抽帧显示；每个完整采集帧仍逐帧执行同源核心、SNR和历史记录。
    bool refreshWaveform = false;
    if (completeWaveform) {
        const quint64 waveformFrame =
            m_waveformFrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        const int waveformRefreshInterval = (std::max)(1, job.waveformRefreshInterval);
        refreshWaveform = waveformRefreshInterval == 1
            || waveformFrame == 1
            || ((waveformFrame - 1) % static_cast<quint64>(waveformRefreshInterval) == 0);
    }

    if (refreshWaveform) {
        const double *displayData = filtered.data();
        int plotCount = 1;
        if (job.showOriginal) {
            combined.resize(2 * pointsPerChan);
            for (int i = 0; i < pointsPerChan; ++i) {
                combined[2 * i] = job.original[static_cast<size_t>(i)];
                combined[2 * i + 1] = filtered[static_cast<size_t>(i)];
            }
            displayData = combined.data();
            plotCount = 2;
        }

        const double xInc = job.xInc;
        const bool recordEnd = job.recordEnd;
        QVector<double> qvDisplayData(displayData, displayData + plotCount * pointsPerChan);
        QMetaObject::invokeMethod(this,
            [this, qvDisplayData, plotCount, pointsPerChan, xInc, recordEnd]() {
                emit dataReady(qvDisplayData, plotCount, pointsPerChan, xInc, recordEnd);
            },
            Qt::QueuedConnection);
    }

    if (completeWaveform) {
        // 主程序只消费同源核心的厚度、峰值位置和SNR，不引入第二套寻峰或判定分支。
        const quint64 frameId = job.frameId;
        const double thickness = thicknessResult.valid
            ? thicknessResult.thicknessMm
            : std::numeric_limits<double>::quiet_NaN();
        const double peakTimeSec = thicknessResult.peakTimeSec;
        const double peakVoltage = thicknessResult.peakVoltage;
        const double snr = thicknessResult.snrDb;
        const auto startupBackfills = thicknessResult.startupBackfills;
        const bool hasBackfill = thicknessResult.hasBackfill;
        const int backfillFramesAgo = thicknessResult.backfillFramesAgo;
        const double backfillThickness = thicknessResult.backfillThicknessMm;
        const double backfillSnr = thicknessResult.backfillSnrDb;

        m_pendingResultCommits.fetch_add(1, std::memory_order_release);
        const bool commitQueued = QMetaObject::invokeMethod(this,
            [this, frameId, thickness, peakTimeSec, peakVoltage, snr, refreshWaveform,
             hasBackfill, backfillFramesAgo, backfillThickness, backfillSnr,
             startupBackfills]() {
                // 测试程序在下一帧确认重锁后，会把此前PENDING帧恢复为当时真实测量值。
                // Qt对同一对象的排队调用保持顺序，因此此处目标帧已经写入历史、当前帧尚未追加。
                bool backfillApplied = false;
                double replacedThickness = std::numeric_limits<double>::quiet_NaN();
                quint64 backfillTargetFrameId = 0;
                if (hasBackfill && backfillFramesAgo > 0
                    && std::isfinite(backfillThickness)) {
                    QMutexLocker locker(&m_thicknessMutex);
                    const int targetIndex = m_thicknessHistory.size() - backfillFramesAgo;
                    if (targetIndex >= 0 && targetIndex < m_thicknessHistory.size()) {
                        replacedThickness = m_thicknessHistory[targetIndex];
                        m_thicknessHistory[targetIndex] = backfillThickness;
                        if (targetIndex < m_thicknessFrameIds.size())
                            backfillTargetFrameId = m_thicknessFrameIds[targetIndex];
                        backfillApplied = true;
                    }
                }

                // The T2 cold-start third frame confirms TWO earlier PENDING
                // measurements. Restore each original Frame ID and historical
                // position independently (oldest first), with no extra attempts.
                for (const auto &old : startupBackfills) {
                    if (old.framesAgo < 1 || old.framesAgo > 2 ||
                        !std::isfinite(old.thicknessMm) || old.thicknessMm <= 0)
                        continue;
                    quint64 oldFrameId = 0;
                    double oldThickness = std::numeric_limits<double>::quiet_NaN();
                    {
                        QMutexLocker locker(&m_thicknessMutex);
                        const int index = m_thicknessHistory.size() - old.framesAgo;
                        if (index < 0 || index >= m_thicknessHistory.size() ||
                            index >= m_thicknessFrameIds.size() ||
                            m_thicknessFrameIds[index] + quint64(old.framesAgo) != frameId)
                            continue;
                        oldFrameId = m_thicknessFrameIds[index];
                        oldThickness = m_thicknessHistory[index];
                        m_thicknessHistory[index] = old.thicknessMm;
                    }
                    if (m_radarPositionSidecar)
                        m_radarPositionSidecar->submitBackfill(
                            oldFrameId, old.thicknessMm, old.snrDb);
                    emit thicknessHistoryBackfilled(old.framesAgo, old.thicknessMm);
                    if (!std::isfinite(oldThickness)) {
                        m_dynamicDetectionRateCounter.backfill(oldFrameId);
                        ++m_thicknessCount;
                        m_thicknessSum += old.thicknessMm;
                    } else {
                        m_thicknessSum += old.thicknessMm - oldThickness;
                    }
                    m_thicknessMax = (std::max)(m_thicknessMax, old.thicknessMm);
                    m_thicknessMin = (std::min)(m_thicknessMin, old.thicknessMm);
                    if (m_historyManager && m_historyManager->isSessionActive())
                        m_historyManager->replaceRecentMeasurement(
                            old.framesAgo, old.thicknessMm, old.snrDb, true);
                }

                if (backfillApplied) {                    if (backfillTargetFrameId && m_radarPositionSidecar)
                        m_radarPositionSidecar->submitBackfill(
                            backfillTargetFrameId, backfillThickness, backfillSnr);
                    // Inform the plot about the corrected historical point
                    // before appending the new frame; do not append twice.
                    emit thicknessHistoryBackfilled(backfillFramesAgo, backfillThickness);
                    // PENDING帧原本必为无效值；回填只补齐一次有效统计，不改变尝试总数。
                    if (!std::isfinite(replacedThickness)) {
                        m_dynamicDetectionRateCounter.backfill(backfillTargetFrameId);
                        m_thicknessSum += backfillThickness;
                        ++m_thicknessCount;
                    } else {
                        // 防御性处理：当前状态机不会进入此分支，但替换有效值时仍保持均值正确。
                        m_thicknessSum += backfillThickness - replacedThickness;
                    }
                    m_thicknessMax = (std::max)(m_thicknessMax, backfillThickness);
                    m_thicknessMin = (std::min)(m_thicknessMin, backfillThickness);
                    if (m_historyManager && m_historyManager->isSessionActive()) {
                        m_historyManager->replaceRecentMeasurement(
                            backfillFramesAgo, backfillThickness, backfillSnr, true);
                    }
                }

                m_thickness = thickness;
                m_snr = snr;
                if (std::isfinite(thickness)) {
                    if (thickness > m_thicknessMax) m_thicknessMax = thickness;
                    if (thickness < m_thicknessMin) m_thicknessMin = thickness;
                    m_thicknessSum += thickness;
                    ++m_thicknessCount;
                }

                {
                    QMutexLocker locker(&m_thicknessMutex);
                    m_thicknessHistory.append(thickness);
                    m_thicknessFrameIds.append(frameId);
                }

                // The same confirmed 3-frame gate opens the dynamic rate.
                // Re-evaluate it for every pipe (unlike the one-shot axial
                // head anchor) so a later pipe can start a new rate segment.
                bool stableThicknessTriple = false;
                if (m_thicknessHistory.size() >= 3) {
                    const int n = m_thicknessHistory.size();
                    const double a = m_thicknessHistory[n-3];
                    const double b = m_thicknessHistory[n-2];
                    const double c = m_thicknessHistory[n-1];
                    const bool consecutive = m_thicknessFrameIds[n-3] + 1 == m_thicknessFrameIds[n-2]
                        && m_thicknessFrameIds[n-2] + 1 == m_thicknessFrameIds[n-1];
                    const double gate = (std::max)(0.6, std::abs(c) * 0.05);
                    stableThicknessTriple = consecutive && std::isfinite(a)
                        && std::isfinite(b) && std::isfinite(c)
                        && (std::max)({a,b,c}) - (std::min)({a,b,c}) <= gate;
                    if (stableThicknessTriple && !m_laserHeadThicknessConfirmed) {
                        // One-time conservative axial origin, NOT the
                        // repeated dynamic-detection-rate segment gate.
                        m_laserHeadThicknessConfirmed = true;
                        emit laserHeadThicknessConfirmedChanged();
                        const quint64 firstFrameId = m_thicknessFrameIds[n-3];
                        m_axialOriginStatus = QStringLiteral(
                            "连续3帧测厚确认（首帧Frame ID=%1）；轴向坐标按首次雷达运动后前进%2mm放行，未标定")
                            .arg(firstFrameId).arg(m_radarToLaserDistanceMm, 0, 'f', 1);
                        emit axialOriginStatusChanged();
                        if (m_radarPositionSidecar)
                            m_radarPositionSidecar->confirmLaserHeadFrame(firstFrameId);
                    }
                }
                m_dynamicDetectionRateCounter.observe(frameId, thickness,
                                                      stableThicknessTriple);

                if (m_historyManager && m_historyManager->isSessionActive())
                    m_historyManager->appendMeasurement(thickness, snr, std::isfinite(thickness));

                if (refreshWaveform) {
                    emit clearMarkersRequested();
                    emit markerReady(peakTimeSec, peakVoltage);
                }
                emit thicknessChanged();
                emit snrChanged();
                emit thicknessStatsChanged();
                emit thicknessHistoryUpdated(thickness);
                emit thicknessFrameIdCommitted(frameId, thickness, std::isfinite(thickness));
                if (m_radarPositionSidecar)
                    m_radarPositionSidecar->submitThickness(
                        frameId, thickness, snr, std::isfinite(thickness));
                updateDetectionRate();
                m_pendingResultCommits.fetch_sub(1, std::memory_order_release);
            },
            Qt::QueuedConnection);
        if (!commitQueued)
            m_pendingResultCommits.fetch_sub(1, std::memory_order_release);

        // The two uncommitted raw-frame files remain in FIFO until the
        // third T2 startup vote; restore BOTH prior original TOFs before
        // queueing them to the disk writer. Other one-frame recoveries remain.
        auto patchPendingTof = [&](int framesAgo, double peakTimeSec) {
            if (framesAgo <= 0 || frameId <= quint64(framesAgo))
                return;
            const quint64 targetFrame = frameId - quint64(framesAgo);
            for (auto &saved : m_pendingFrameSaves) {
                if (saved.frameId != targetFrame) continue;
                saved.tofSamples = std::isfinite(peakTimeSec)
                    ? peakTimeSec * cfg.clockRatePerChan
                    : std::numeric_limits<double>::quiet_NaN();
                break;
            }
        };
        if (hasBackfill && std::isfinite(backfillThickness))
            patchPendingTof(backfillFramesAgo, thicknessResult.backfillPeakTimeSec);
        for (const auto &old : startupBackfills)
            patchPendingTof(old.framesAgo, old.peakTimeSec);

        if (!job.saveFileName.isEmpty()) {
            FrameSaveJob save;
            save.frameId = frameId;
            save.fileName = job.saveFileName;
            save.original = std::move(job.original);
            save.filtered.assign(filtered.begin(), filtered.end());
            save.tofSamples = thicknessResult.valid
                && std::isfinite(thicknessResult.peakTimeSec)
                ? thicknessResult.peakTimeSec * cfg.clockRatePerChan
                : std::numeric_limits<double>::quiet_NaN();
            m_pendingFrameSaves.push_back(std::move(save));
        }
        while (m_pendingFrameSaves.size() > 2) {
            enqueueFrameSave(std::move(m_pendingFrameSaves.front()));
            m_pendingFrameSaves.pop_front();
        }
    }

    const qint64 elapsedUs = processingTimer.nsecsElapsed() / 1000;
    static thread_local quint64 timingFrameCount = 0;
    static thread_local qint64 timingAccumulatedUs = 0;
    static thread_local qint64 timingMaxUs = 0;
    ++timingFrameCount;
    timingAccumulatedUs += elapsedUs;
    timingMaxUs = (std::max)(timingMaxUs, elapsedUs);
    if (timingFrameCount % 100 == 0) {
        qDebug() << "[DataController] 100-frame processing timing: avg"
                 << (timingAccumulatedUs / 100.0) << "us, max" << timingMaxUs << "us";
        timingAccumulatedUs = 0;
        timingMaxUs = 0;
    }
}

void DataController::OnDataReadyEvent(void *sender, Automation::BDaq::BfdAiEventArgs *args, void *userParam)
{
    QElapsedTimer timer;
    timer.start();

    BfdAiEventArgs *bfdArgs = (BfdAiEventArgs *)args;
    ErrorCode errorCode = Success;
    int32 remainingCount = bfdArgs->Count;
    int32 getDataCount = 0, returnedCount = 0;
    double timeStamp = 0;
    DataController *p = (DataController *)userParam;
    bool flagForRecordEnd = false;

    // 回调入口一次性固化完整配置；本次硬件取数、帧拆分和核心调用使用同一份参数，
    // 避免实时调参时sectionLength/channelCount与随帧参数来自不同版本。
    ConfigureParameter frameConfigure;
    int frameFilterOrder = 0;
    {
        std::lock_guard<std::mutex> configLocker(p->m_configMutex);
        frameConfigure = p->m_configure;
        frameFilterOrder = p->m_filterOrder;
    }
    const int32 bufSize = frameConfigure.sectionLength * frameConfigure.channelCount;

    // DAQ回调只负责取数和固化参数快照；同源核心测厚、UI提交与保存均在FIFO处理线程执行。
    do
    {
        getDataCount = qMin(bufSize, remainingCount);
        errorCode = ((WaveformAiCtrl*)sender)->GetData(getDataCount, p->m_scaledData, 0, &returnedCount,
                                                       &timeStamp, NULL, NULL);
        remainingCount -= returnedCount;
        if (errorCode != Success && errorCode != WarningRecordEnd)
        {
            const QString message = QString("Sorry, there are some errors occurred, Error Code: 0x") +
                QString::number(errorCode, 16).right(8);
            QMetaObject::invokeMethod(p, [p, message]() {
                emit p->errorOccurred(message);
            }, Qt::QueuedConnection);
            return;
        }

        flagForRecordEnd = (errorCode == WarningRecordEnd);
        const int pointsPerChan = returnedCount / frameConfigure.channelCount;
        if (pointsPerChan > 0) {
            AcquiredFrameJob job;
            job.frameId = p->m_acquiredFrameSerial.fetch_add(1, std::memory_order_relaxed) + 1;
            // Host monotonic + UTC sampled together after GetData. This
            // callback observation is NOT the physical 100 Hz trigger time.
            const auto clockPair = RadarHostClock::samplePair();
            job.daqCallbackHostSteadyUs = clockPair.steadyUs;
            job.daqCallbackUnixUs = clockPair.unixUs;
            job.callbackClockPairSpreadUs = clockPair.spreadUs;
            job.bdaqUnverifiedTimestamp = timeStamp;
            if (pointsPerChan == frameConfigure.sectionLength
                && p->m_radarPositionSidecar) {
                p->m_radarPositionSidecar->submitFrame(job.frameId,
                    job.daqCallbackHostSteadyUs, job.bdaqUnverifiedTimestamp,
                    job.daqCallbackUnixUs, job.callbackClockPairSpreadUs);
            }
            job.original.assign(p->m_scaledData, p->m_scaledData + pointsPerChan);
            // 同一DAQ回调内的所有帧共用入口快照，保证硬件帧长与算法参数严格对应。
            job.configure = frameConfigure;
            job.filterOrder = frameFilterOrder;
            job.xInc = p->m_xInc;
            job.waveformRefreshInterval =
                p->m_waveformRefreshInterval.load(std::memory_order_relaxed);
            job.showOriginal = p->m_showOriginal.load(std::memory_order_relaxed);
            job.recordEnd = flagForRecordEnd;

            // 保存开关也按采集时刻固化；只在回调中生成文件名，不做任何磁盘I/O。
            if (p->m_savePerFrame.load(std::memory_order_acquire)) {
                QMutexLocker locker(&p->m_fileMutex);
                ++p->m_saveFrameCounter;
                job.saveFileName = QString("%1/Frame_%2.csv")
                    .arg(p->m_saveFolder)
                    .arg(p->m_saveFrameCounter, 4, 10, QChar('0'));
            }
            p->enqueueAcquiredFrame(std::move(job));
        }
    } while (remainingCount > 0);

    const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
    static thread_local quint64 timingFrameCount = 0;
    static thread_local qint64 timingAccumulatedUs = 0;
    static thread_local qint64 timingMaxUs = 0;
    ++timingFrameCount;
    timingAccumulatedUs += elapsedUs;
    timingMaxUs = (std::max)(timingMaxUs, elapsedUs);
    if (timingFrameCount % 100 == 0) {
        qDebug() << "[DataController] 100-frame DAQ callback timing: avg"
                 << (timingAccumulatedUs / 100.0) << "us, max" << timingMaxUs
                 << "us, processing queue peak"
                 << p->m_processingQueuePeak.load(std::memory_order_relaxed);
        timingAccumulatedUs = 0;
        timingMaxUs = 0;
    }
}