#include "DataController.h"
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LOCAL_EDGE "Local"

namespace {

// 与 LSTAlgorithmTest 保持一致：用 7 点局部交流能量计算信号/噪声功率，
// 并用搜索窗外多个背景窗口能量的中位数作为噪声基线。
constexpr int SNR_ENERGY_WINDOW_SAMPLES = 7;

double snrLocalEnergy(const std::vector<double> &signal, int center, int halfWidth)
{
    if (center - halfWidth < 0
        || center + halfWidth >= static_cast<int>(signal.size()))
        return std::numeric_limits<double>::quiet_NaN();

    double mean = 0.0;
    for (int i = center - halfWidth; i <= center + halfWidth; ++i)
        mean += signal[i];
    const int count = 2 * halfWidth + 1;
    mean /= count;

    double energy = 0.0;
    for (int i = center - halfWidth; i <= center + halfWidth; ++i) {
        const double value = signal[i] - mean;
        energy += value * value;
    }
    return energy / count;
}

double snrMedian(std::vector<double> &values)
{
    if (values.empty())
        return std::numeric_limits<double>::quiet_NaN();
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if (values.size() % 2 != 0)
        return upper;
    const double lower = *std::max_element(values.begin(), values.begin() + middle);
    return 0.5 * (lower + upper);
}

double calculateAlgorithmTestSnr(const std::vector<double> &signal,
                                 int peakIndex,
                                 int searchLeft,
                                 int searchRight,
                                 int filterOrder)
{
    const int half = SNR_ENERGY_WINDOW_SAMPLES / 2;
    const int edge = filterOrder + half;
    // 100 Hz实时路径复用背景能量缓冲区，避免每帧重新申请/释放堆内存。
    // snrMedian() 会原地重排该临时数组，但不会改变输入波形或计算结果。
    static thread_local std::vector<double> background;
    background.clear();
    const int estimatedWindows = (std::max)(1,
        static_cast<int>(signal.size()) / SNR_ENERGY_WINDOW_SAMPLES);
    if (static_cast<int>(background.capacity()) < estimatedWindows)
        background.reserve(estimatedWindows);

    // 与 LSTAlgorithmTest 的 estimateNoiseEnergy() 一致：
    // 排除 FIR 边界和理论搜索窗，在剩余背景区按相同17点窗口取局部交流能量，
    // 再以多个背景窗口能量的中位数作为 noiseEnergy。
    for (int center = edge; center + edge < static_cast<int>(signal.size());
         center += SNR_ENERGY_WINDOW_SAMPLES) {
        if (center + half >= searchLeft - filterOrder
            && center - half <= searchRight + filterOrder)
            continue;

        const double energy = snrLocalEnergy(signal, center, half);
        if (std::isfinite(energy))
            background.push_back(energy);
    }

    // LSTAlgorithmTest 在不足4个背景窗口时判 INSUFFICIENT_NOISE；
    // 主 LST 此处只负责显示/记录 SNR，不改变原测厚判定，因此返回0 dB。
    if (background.size() < 4)
        return 0.0;

    const double noiseEnergy = (std::max)(snrMedian(background), 1e-24);
    const double signalEnergy = snrLocalEnergy(signal, peakIndex, half);
    if (!std::isfinite(signalEnergy))
        return 0.0;

    return 10.0 * std::log10((std::max)(signalEnergy, 1e-24) / noiseEnergy);
}

} // namespace

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
    , m_filterOrder(30)
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
    m_configure.triggerAction = DelayToStart;
    m_configure.delayCount = 160;
    m_configure.triggerLevel = 2.0;
    m_configure.triggerEdge = RisingEdge;
    m_configure.triggerSourceIndex = 0;
    m_configure.actualDelayCount = 0;

    //m_triggerEdgeList << "RisingEdge" << "FallingEdge";

    updateFilterCoefficients(3e6, 30e6);
}

DataController::~DataController()
{
    // 析构时先停DAQ，确保不会再有新帧进入处理队列；随后按“处理→保存”顺序排空。
    if (m_waveformAiCtrl) {
        WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
        ctrl->Stop();
    }
    stopProcessingWorker();
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
    updateFilterCoefficients(3e6, 30e6);
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

void DataController::setTriggerSource(int index) { m_configure.triggerSourceIndex = index; }
void DataController::setTriggerEdge(int index) { m_configure.triggerEdge = (index == 0) ? RisingEdge : FallingEdge; }
void DataController::setDelayCount(int count) { m_configure.delayCount = count; }
void DataController::setTriggerLevel(double level) { m_configure.triggerLevel = level; }
void DataController::setDetectMode(const QString &mode) { m_configure.detectMode = mode; }
void DataController::setThicknessStd(double val) { m_configure.thicknessStd = val; }
void DataController::setThicknessTolerance(double val) { m_configure.thicknessTolerance = val; }
void DataController::setStructLen(int val) { m_configure.structLen = val; }
void DataController::setMinWidth(int val) { m_configure.minWidth = val; }

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
    double sampleRate = m_configure.clockRatePerChan;
    double nyquist = sampleRate / 2.0 / 1e6;

    if (lowFreqMHz <= 0 || highFreqMHz <= lowFreqMHz || highFreqMHz >= nyquist) {
        emit errorOccurred(QString("Please enter valid frequencies (0 < low < high < %1 MHz).").arg(nyquist));
        return;
    }

    updateFilterCoefficients(lowFreqMHz * 1e6, highFreqMHz * 1e6);
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

void DataController::designBandpassFilter(double lowFreq, double highFreq,
                                           double sampleRate, int order,
                                           std::vector<double> &coeffs)
{
    int taps = order + 1;
    double fc1 = lowFreq / sampleRate;
    double fc2 = highFreq / sampleRate;
    int M = order / 2;
    coeffs.resize(taps);
    for (int n = 0; n < taps; ++n) {
        double window = 0.54 - 0.46 * cos(2 * M_PI * n / order);
        int k = n - M;
        if (k == 0) {
            coeffs[n] = 2 * (fc2 - fc1) * window;
        } else {
            double sinc = (sin(2 * M_PI * fc2 * k) - sin(2 * M_PI * fc1 * k)) / (M_PI * k);
            coeffs[n] = sinc * window;
        }
    }
}

void DataController::updateFilterCoefficients(double lowFreqHz, double highFreqHz)
{
    double sampleRate = m_configure.clockRatePerChan;
    if (sampleRate <= 0) sampleRate = 80e6;

    std::vector<double> newCoeffs;
    if (m_filterOrder > 0) {
        designBandpassFilter(lowFreqHz, highFreqHz, sampleRate, m_filterOrder, newCoeffs);
    }

    QMutexLocker locker(&m_filterMutex);
    m_filterCoeffs.swap(newCoeffs);
    m_filterRevision.fetch_add(1, std::memory_order_release);
}

void DataController::applyBandpassFilter(const double* input, double* output, int len)
{
    // FIR参数通常在采集开始前设置，100 Hz采集过程中不会逐帧变化。
    // 用“对象+版本号”的线程局部快照避免每帧进入QMutex并复制系数；
    // 用户修改滤波参数后版本号变化，下一帧自动刷新，数值系数保持完全一致。
    static thread_local const DataController *cachedOwner = nullptr;
    static thread_local quint64 cachedRevision = std::numeric_limits<quint64>::max();
    static thread_local std::vector<double> coeffsCopy;

    const quint64 revision = m_filterRevision.load(std::memory_order_acquire);
    if (cachedOwner != this || cachedRevision != revision) {
        QMutexLocker locker(&m_filterMutex);
        coeffsCopy = m_filterCoeffs;
        cachedOwner = this;
        cachedRevision = m_filterRevision.load(std::memory_order_relaxed);
    }
    if (coeffsCopy.empty()) {
        memcpy(output, input, len * sizeof(double));
        return;
    }
    int order = (int)coeffsCopy.size() - 1;
    if (order < 1) {
        memcpy(output, input, len * sizeof(double));
        return;
    }
    const double* coeffs = coeffsCopy.data();

    // ---- 边界延拓：反射/镜像，使得边界连续 ----
    // 修复(越界): padLen 上限为 len，避免 padLen > len 时 srcIdx 越界读取 input[]
    int padLen = (std::min)(order * 2, len);    // 延拓长度，不超过信号长度
    int nPadded = 2 * padLen + len;

    // 优化(缓冲区复用): 使用 thread_local 复用 padded/forward/backward，
    // 避免每帧调用都进行 3 次堆分配（原实现每次 new/delete 3 个 vector）
    static thread_local std::vector<double> padded;
    static thread_local std::vector<double> forward;
    static thread_local std::vector<double> backward;
    if ((int)padded.size() < nPadded) {
        padded.resize(nPadded);
        forward.resize(nPadded);
        backward.resize(nPadded);
    }

    // 左侧反射延拓: 镜像围绕 input[0]，即 padded[padLen-1-i] = input[i]
    for (int i = 0; i < padLen; ++i) {
        int srcIdx = i;
        if (srcIdx >= len) srcIdx = len - 1;    // 钳位防止越界
        padded[padLen - 1 - i] = input[srcIdx];
    }
    // 原始数据
    for (int i = 0; i < len; ++i) {
        padded[padLen + i] = input[i];
    }
    // 右侧反射延拓: 镜像围绕 input[len-1]，即 padded[padLen+len+i] = input[len-1-i]
    for (int i = 0; i < padLen; ++i) {
        int srcIdx = len - 1 - i;
        if (srcIdx < 0) srcIdx = 0;             // 钳位防止越界
        padded[padLen + len + i] = input[srcIdx];
    }

    // 优化(消除分支): 将内层循环拆分为热身/主体两段，
    // 主体段无 if(i-j>=0) / if(i+j<nPadded) 分支判断，减少分支预测失败
    // ---- 正向滤波（零相位第一步） ----
    // 热身段: i < order，j 上界为 i（仅 order 个元素）
    int warmupEnd = (std::min)(order, nPadded);
    for (int i = 0; i < warmupEnd; ++i) {
        double sum = 0.0;
        for (int j = 0; j <= i; ++j) {
            sum += coeffs[j] * padded[i - j];
        }
        forward[i] = sum;
    }
    // 主体段: order <= i < nPadded，j 全程 [0,order] 有效，无分支
    for (int i = warmupEnd; i < nPadded; ++i) {
        double sum = 0.0;
        const double* p = padded.data() + i;
        for (int j = 0; j <= order; ++j) {
            sum += coeffs[j] * p[-j];
        }
        forward[i] = sum;
    }

    // ---- 反向滤波（零相位第二步） ----
    // 主体段: i < nPadded - order，j 全程 [0,order] 有效，无分支
    int mainEnd = nPadded - order - 1;
    for (int i = mainEnd; i >= 0; --i) {
        double sum = 0.0;
        const double* p = forward.data() + i;
        for (int j = 0; j <= order; ++j) {
            sum += coeffs[j] * p[j];
        }
        backward[i] = sum;
    }
    // 收尾段(高索引): i >= nPadded - order，j 上界为 nPadded-1-i
    int tailEnd = (std::max)(0, nPadded - order);
    for (int i = nPadded - 1; i >= tailEnd; --i) {
        double sum = 0.0;
        int jMax = nPadded - 1 - i;
        if (jMax > order) jMax = order;
        const double* p = forward.data() + i;
        for (int j = 0; j <= jMax; ++j) {
            sum += coeffs[j] * p[j];
        }
        backward[i] = sum;
    }

    // ---- 裁剪掉延拓部分，得到输出 ----
    for (int i = 0; i < len; ++i) {
        output[i] = backward[padLen + i];
    }
}

void DataController::detrendSignal(std::vector<double>& signal)
{
    int n = (int)signal.size();
    if (n < 2) return;

    // 计算均值（索引均值固定）
    double mean_x = (n - 1) / 2.0;
    double mean_y = 0.0;
    for (double v : signal) mean_y += v;
    mean_y /= n;

    // 计算斜率
    double numerator = 0.0, denominator = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx = i - mean_x;
        double dy = signal[i] - mean_y;
        numerator += dx * dy;
        denominator += dx * dx;
    }
    double slope = (denominator != 0.0) ? numerator / denominator : 0.0;
    double intercept = mean_y - slope * mean_x;

    // 去除线性趋势
    for (int i = 0; i < n; ++i) {
        signal[i] -= (slope * i + intercept);
    }
}

void DataController::updateDetectionRate()
{
    double rate = 0.0;
    if (m_totalAttempts > 0) {
        rate = (double)m_validDetections / m_totalAttempts * 100.0;
    }
    m_detectionRate = rate;
    emit detectionRateChanged();
}

double DataController::detectFirstPeak(const std::vector<double>& signal,
                                       double clockRate,
                                       const QString& mode,
                                       double thicknessStd,
                                       double tolerance,
                                       int structLen,
                                       int minWidth,
                                       int delayCount,
                                       double& peakTime,
                                       double& peakVoltage)
{
    int N = signal.size();
    if (N == 0) return NAN;

    // 1. 理论位置计算
    double bias = 1.1;
    thicknessStd += bias;
    double thickness_m = thicknessStd * 1e-3;
    double velocity = 6459.7;
    double t_ref = 2.0 * thickness_m / velocity;
    int interval_theory = (int)round(t_ref * clockRate);
    int first_center = (std::max)(1, interval_theory - delayCount);
    int search_half = (int)(clockRate / 80000000 * 27);   // 转换为整数

    int left_win = (std::max)(0, first_center - search_half);
    int right_win = (std::min)(N-1, first_center + search_half);
    if (left_win >= right_win) return NAN;

    // 2. 极性判断
    bool usePeakMode = true;
    if (mode == "auto") {
        double maxVal = signal[left_win], minVal = signal[left_win];
        for (int i = left_win + 1; i <= right_win; ++i) {
            if (signal[i] > maxVal) maxVal = signal[i];
            if (signal[i] < minVal) minVal = signal[i];
        }
        usePeakMode = (maxVal > -minVal);
    } else if (mode == "peak") {
        usePeakMode = true;
    } else if (mode == "valley") {
        usePeakMode = false;
    } else {
        return NAN;
    }

    // 3. 形态学变换
    int half_struct = structLen / 2;
    int margin = (std::max)(structLen * 2, minWidth * 4);  // 半高宽游走边距
    int morph_left = (std::max)(0, left_win - margin);
    int morph_right = (std::min)(N - 1, right_win + margin);
    int morph_len = morph_right - morph_left + 1;
    // 腐蚀/膨胀需额外 structLen/2 边距以保证开/闭运算边界正确
    int erode_left = (std::max)(0, morph_left - half_struct);
    int erode_right = (std::min)(N - 1, morph_right + half_struct);
    int erode_len = erode_right - erode_left + 1;

    // 100 Hz路径复用形态学/统计工作区；所有元素仍按原顺序逐一覆盖，计算结果不变。
    static thread_local std::vector<double> feature;
    static thread_local std::vector<double> morphologyA;
    static thread_local std::vector<double> morphologyB;
    static thread_local std::vector<double> sorted;
    static thread_local std::vector<double> absDev;
    static thread_local std::vector<int> candidates;
    static thread_local std::vector<int> validCandidates;
    feature.resize(morph_len);

    if (usePeakMode) {
        // 腐蚀: eroded[k] = min(signal[erode_left+k-half_struct .. +half_struct])
        morphologyA.resize(erode_len);
        std::vector<double> &eroded = morphologyA;
        for (int k = 0; k < erode_len; ++k) {
            int gi = erode_left + k;  // 全局索引
            int left = (std::max)(0, gi - half_struct);
            int right = (std::min)(N - 1, gi + half_struct);
            double minVal = signal[left];
            for (int j = left + 1; j <= right; ++j)
                if (signal[j] < minVal) minVal = signal[j];
            eroded[k] = minVal;
        }
        // 开运算: opened[k] = max(eroded[对应窗口])
        morphologyB.resize(morph_len);
        std::vector<double> &opened = morphologyB;
        for (int k = 0; k < morph_len; ++k) {
            int gi = morph_left + k;
            int left = (std::max)(erode_left, gi - half_struct) - erode_left;
            int right = (std::min)(erode_right, gi + half_struct) - erode_left;
            double maxVal = eroded[left];
            for (int j = left + 1; j <= right; ++j)
                if (eroded[j] > maxVal) maxVal = eroded[j];
            opened[k] = maxVal;
        }
        for (int k = 0; k < morph_len; ++k)
            feature[k] = signal[morph_left + k] - opened[k];
    } else {
        // 膨胀: dilated[k] = max(signal[窗口])
        morphologyA.resize(erode_len);
        std::vector<double> &dilated = morphologyA;
        for (int k = 0; k < erode_len; ++k) {
            int gi = erode_left + k;
            int left = (std::max)(0, gi - half_struct);
            int right = (std::min)(N - 1, gi + half_struct);
            double maxVal = signal[left];
            for (int j = left + 1; j <= right; ++j)
                if (signal[j] > maxVal) maxVal = signal[j];
            dilated[k] = maxVal;
        }
        // 闭运算: closed[k] = min(dilated[对应窗口])
        morphologyB.resize(morph_len);
        std::vector<double> &closed = morphologyB;
        for (int k = 0; k < morph_len; ++k) {
            int gi = morph_left + k;
            int left = (std::max)(erode_left, gi - half_struct) - erode_left;
            int right = (std::min)(erode_right, gi + half_struct) - erode_left;
            double minVal = dilated[left];
            for (int j = left + 1; j <= right; ++j)
                if (dilated[j] < minVal) minVal = dilated[j];
            closed[k] = minVal;
        }
        for (int k = 0; k < morph_len; ++k)
            feature[k] = closed[k] - signal[morph_left + k];
    }

    // 4. 窗口内峰检测 (优化: 使用局部索引直接访问 feature，避免 feat_win 子向量拷贝)
    int win_left_local = left_win - morph_left;
    int win_right_local = right_win - morph_left;
    int win_len = win_right_local - win_left_local + 1;

    candidates.clear();
    for (int i = win_left_local + 1; i < win_right_local; ++i) {
        if (feature[i] > feature[i-1] && feature[i] > feature[i+1])
            candidates.push_back(i);
    }
    if (candidates.empty()) return NAN;

    sorted.assign(feature.begin() + win_left_local, feature.begin() + win_right_local + 1);
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[win_len/2];
    absDev.resize(win_len);
    for (int i = 0; i < win_len; ++i)
        absDev[static_cast<size_t>(i)] = std::fabs(feature[win_left_local + i] - median);
    std::sort(absDev.begin(), absDev.end());
    double mad = absDev[win_len/2];
    double dynamic_thresh = median + 2.0 * mad;
    double feat_max = 0.0;
    for (int i = win_left_local; i <= win_right_local; ++i)
        if (feature[i] > feat_max) feat_max = feature[i];
    dynamic_thresh = (std::max)(dynamic_thresh, 0.01 * feat_max);

    validCandidates.clear();
    for (int idx : candidates) {
        if (feature[idx] >= dynamic_thresh)
            validCandidates.push_back(idx);
    }
    if (validCandidates.empty()) return NAN;

    int best_local = validCandidates[0];
    double best_val = feature[best_local];
    for (int idx : validCandidates) {
        if (feature[idx] > best_val) {
            best_val = feature[idx];
            best_local = idx;
        }
    }
    int first_peak = morph_left + best_local;  // 转换为全局索引

    // 5. 半高宽计算（不插值，仅用整数索引）
    double half = feature[best_local] / 2.0;

    int left_idx = best_local;
    while (left_idx > 0 && feature[left_idx] > half) --left_idx;

    int right_idx = best_local;
    while (right_idx < morph_len - 1 && feature[right_idx] > half) ++right_idx;

    int width = right_idx - left_idx;   // 整数宽度（不插值）
    if (width < minWidth) return NAN;

    // 6. 获取峰值幅值
    peakVoltage = signal[first_peak];

    // 7. 计算厚度
    peakTime = first_peak / clockRate;
    double thickness = velocity * ((delayCount / clockRate) + peakTime) / 2.0 * 1000.0;
    if (std::abs(thickness - thicknessStd) > tolerance)
        thickness = NAN;
    thickness -= bias;
    return thickness;
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

    filtered.resize(pointsPerChan);
    applyBandpassFilter(job.original.data(), filtered.data(), pointsPerChan);
    detrendSignal(filtered);

    // UI只抽帧显示；每个完整采集帧仍然逐帧执行滤波、寻峰、SNR和历史记录。
    const bool completeWaveform = (pointsPerChan == cfg.sectionLength);
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
        double peakTimeSec = 0.0, peakVoltage = 0.0;
        const double thickness = detectFirstPeak(filtered,
                                                 cfg.clockRatePerChan,
                                                 cfg.detectMode,
                                                 cfg.thicknessStd,
                                                 cfg.thicknessTolerance,
                                                 cfg.structLen,
                                                 cfg.minWidth,
                                                 cfg.delayCount,
                                                 peakTimeSec,
                                                 peakVoltage);

        const double biasForSnr = 1.1;
        const double thicknessForSnr = cfg.thicknessStd + biasForSnr;
        const double thicknessMeterForSnr = thicknessForSnr * 1e-3;
        const double velocityForSnr = 6459.7;
        const double referenceTimeForSnr = 2.0 * thicknessMeterForSnr / velocityForSnr;
        const int theoreticalIntervalForSnr = static_cast<int>(
            std::round(referenceTimeForSnr * cfg.clockRatePerChan));
        const int firstCenterForSnr = (std::max)(
            1, theoreticalIntervalForSnr - cfg.delayCount);
        const int searchHalfForSnr = static_cast<int>(
            cfg.clockRatePerChan / 80000000.0 * 27.0);
        const int searchLeftForSnr = (std::max)(0, firstCenterForSnr - searchHalfForSnr);
        const int searchRightForSnr = (std::min)(
            pointsPerChan - 1, firstCenterForSnr + searchHalfForSnr);
        const int peakIndexForSnr = static_cast<int>(
            std::round(peakTimeSec * cfg.clockRatePerChan));
        const double snr = calculateAlgorithmTestSnr(
            filtered, peakIndexForSnr, searchLeftForSnr, searchRightForSnr, job.filterOrder);

        m_pendingResultCommits.fetch_add(1, std::memory_order_release);
        const bool commitQueued = QMetaObject::invokeMethod(this,
            [this, thickness, peakTimeSec, peakVoltage, snr, refreshWaveform]() {
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

        if (!job.saveFileName.isEmpty()) {
            FrameSaveJob saveJob;
            saveJob.fileName = job.saveFileName;
            saveJob.original = std::move(job.original);
            saveJob.filtered.assign(filtered.begin(), filtered.end());
            saveJob.thickness = thickness;
            enqueueFrameSave(std::move(saveJob));
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
    int32 bufSize = (p->m_configure.sectionLength * p->m_configure.channelCount);

    // DAQ回调只负责取数并复制到处理FIFO。滤波、旧寻峰、SNR、UI和保存均在后续线程执行。
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
        const int pointsPerChan = returnedCount / p->m_configure.channelCount;
        if (pointsPerChan > 0) {
            AcquiredFrameJob job;
            job.original.assign(p->m_scaledData, p->m_scaledData + pointsPerChan);
            // 将采集时刻的处理配置随帧固化，避免FIFO存在短暂积压时被后续UI设置“穿越”影响旧帧。
            job.configure = p->m_configure;
            job.xInc = p->m_xInc;
            job.filterOrder = p->m_filterOrder;
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