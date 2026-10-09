#include "DataController.h"
#include "../algorithm/ThicknessProcessor.h"
#include "bdaqctrl.h"
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
    , m_totalAttempts(0)
    , m_validDetections(0)
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
    flushPendingResultCommits();
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
    return QString("%1%").arg(m_detectionRate, 0, 'f', 1);
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
    if (m_thicknessCount == 0) return "-- mm";
    return QString("%1 mm").arg(m_thicknessMax, 0, 'f', 3);
}

QString DataController::minThicknessText() const
{
    if (m_thicknessCount == 0) return "-- mm";
    return QString("%1 mm").arg(m_thicknessMin, 0, 'f', 3);
}

QString DataController::avgThicknessText() const
{
    if (m_thicknessCount == 0) return "-- mm";
    double avg = m_thicknessSum / m_thicknessCount;
    return QString("%1 mm").arg(avg, 0, 'f', 3);
}

QString DataController::thicknessEchoMode() const
{
    std::lock_guard<std::mutex> locker(m_configMutex);
    return m_configure.thicknessAlgorithm.echoMode == ThicknessEchoMode::T2Only
        ? QStringLiteral("T2-only") : QStringLiteral("T1-only");
}

double DataController::avgThickness() const
{
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
    // 只在处理队列已排空或处理线程内部调用，避免与m_pendingFrameSave并发访问。
    if (!m_hasPendingFrameSave)
        return;
    enqueueFrameSave(std::move(m_pendingFrameSave));
    m_pendingFrameSave = FrameSaveJob{};
    m_hasPendingFrameSave = false;
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
            out << "Index,Original,Filtered,Thickness\n";
            const QString thickStr = std::isnan(job.thickness)
                ? QStringLiteral("NaN") : QString::number(job.thickness, 'f', 6);
            const int count = static_cast<int>((std::min)(job.original.size(), job.filtered.size()));
            for (int i = 0; i < count; ++i) {
                out << i << ","
                    << job.original[static_cast<size_t>(i)] << ","
                    << job.filtered[static_cast<size_t>(i)] << ","
                    << thickStr << "\n";
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
    m_running = false;
    m_paused = false;

    // DAQ已经停止后先排空处理队列；处理线程可能继续产生保存任务，因此随后再排空写盘队列。
    drainProcessingQueue();
    flushPendingFrameSave();
    flushPendingResultCommits();
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
    m_totalAttempts = 0;
    m_validDetections = 0;
    m_detectionRate = 0.0;
    m_snr = 0.0;
    m_thicknessMax = -std::numeric_limits<double>::max();
    m_thicknessMin = std::numeric_limits<double>::max();
    m_thicknessSum = 0.0;
    m_thicknessCount = 0;

    {
        QMutexLocker locker(&m_thicknessMutex);
        m_thicknessHistory.clear();
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
    double rate = 0.0;
    if (m_totalAttempts > 0) {
        rate = (double)m_validDetections / m_totalAttempts * 100.0;
    }
    m_detectionRate = rate;
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
        const double thickness = thicknessResult.valid
            ? thicknessResult.thicknessMm
            : std::numeric_limits<double>::quiet_NaN();
        const double peakTimeSec = thicknessResult.peakTimeSec;
        const double peakVoltage = thicknessResult.peakVoltage;
        const double snr = thicknessResult.snrDb;
        const bool hasBackfill = thicknessResult.hasBackfill;
        const int backfillFramesAgo = thicknessResult.backfillFramesAgo;
        const double backfillThickness = thicknessResult.backfillThicknessMm;
        const double backfillSnr = thicknessResult.backfillSnrDb;

        m_pendingResultCommits.fetch_add(1, std::memory_order_release);
        const bool commitQueued = QMetaObject::invokeMethod(this,
            [this, thickness, peakTimeSec, peakVoltage, snr, refreshWaveform,
             hasBackfill, backfillFramesAgo, backfillThickness, backfillSnr]() {
                // 测试程序在下一帧确认重锁后，会把此前PENDING帧恢复为当时真实测量值。
                // Qt对同一对象的排队调用保持顺序，因此此处目标帧已经写入历史、当前帧尚未追加。
                bool backfillApplied = false;
                double replacedThickness = std::numeric_limits<double>::quiet_NaN();
                if (hasBackfill && backfillFramesAgo > 0
                    && std::isfinite(backfillThickness)) {
                    QMutexLocker locker(&m_thicknessMutex);
                    const int targetIndex = m_thicknessHistory.size() - backfillFramesAgo;
                    if (targetIndex >= 0 && targetIndex < m_thicknessHistory.size()) {
                        replacedThickness = m_thicknessHistory[targetIndex];
                        m_thicknessHistory[targetIndex] = backfillThickness;
                        backfillApplied = true;
                    }
                }

                if (backfillApplied) {
                    // PENDING帧原本必为无效值；回填只补齐一次有效统计，不改变尝试总数。
                    if (!std::isfinite(replacedThickness)) {
                        ++m_validDetections;
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
                ++m_totalAttempts;

                if (!std::isnan(thickness)) {
                    ++m_validDetections;
                    if (thickness > m_thicknessMax) m_thicknessMax = thickness;
                    if (thickness < m_thicknessMin) m_thicknessMin = thickness;
                    m_thicknessSum += thickness;
                    ++m_thicknessCount;
                }

                {
                    QMutexLocker locker(&m_thicknessMutex);
                    m_thicknessHistory.append(thickness);
                }

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
                updateDetectionRate();
                m_pendingResultCommits.fetch_sub(1, std::memory_order_release);
            },
            Qt::QueuedConnection);
        if (!commitQueued)
            m_pendingResultCommits.fetch_sub(1, std::memory_order_release);

        // 写盘延迟一个完整帧：若当前帧确认重锁，可先把上一帧PENDING厚度回填后再入队。
        if (m_hasPendingFrameSave) {
            if (hasBackfill && backfillFramesAgo == 1 && std::isfinite(backfillThickness))
                m_pendingFrameSave.thickness = backfillThickness;
            flushPendingFrameSave();
        }

        if (!job.saveFileName.isEmpty()) {
            m_pendingFrameSave.fileName = job.saveFileName;
            m_pendingFrameSave.original = std::move(job.original);
            m_pendingFrameSave.filtered.assign(filtered.begin(), filtered.end());
            m_pendingFrameSave.thickness = thickness;
            m_hasPendingFrameSave = true;
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