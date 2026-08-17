#include "DataController.h"
#include "bdaqctrl.h"
#include <QDateTime>
#include <QDir>
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
    m_configure.profilePath = "";
    m_configure.detectMode = "auto";
    m_configure.thicknessStd = 5.0;
    m_configure.thicknessTolerance = 0.5;
    m_configure.structLen = 7;
    m_configure.minWidth = 1;
    m_configure.triggerAction = DelayToStart;
    m_configure.delayCount = 160;
    m_configure.triggerLevel = 2.0;
    m_configure.triggerEdge = RisingEdge;
    m_configure.triggerSourceIndex = 0;
    m_configure.actualDelayCount = 0;

    //m_triggerEdgeList << "RisingEdge" << "FallingEdge";

    updateFilterCoefficients(1e6, 30e6);
}

DataController::~DataController()
{
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
    updateFilterCoefficients(1e6, 30e6);
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
    if (m_showOriginal != enabled) {
        m_showOriginal = enabled;
        emit showOriginalChanged();
    }
}

void DataController::setSavePerFrame(bool enabled)
{
    if (m_savePerFrame != enabled) {
        m_savePerFrame = enabled;
        if (enabled) {
            QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            m_saveFolder = QString("FrameData_%1").arg(timeStr);
            QDir dir;
            if (!dir.mkpath(m_saveFolder)) {
                m_savePerFrame = false;
                emit errorOccurred(tr("Cannot create save folder."));
            }
        }
        emit savePerFrameChanged();
    }
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
    //qDebug() << "[DataController] startAcquisition() called";

    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl) {
        //qDebug() << "[DataController] startAcquisition: m_waveformAiCtrl is null";
        return;
    }

    applySettings();
    configureDevice();
    configureGraph();

    m_running = true;
    m_paused = false;
    m_saveFrameCounter = 0;

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
    if (!ctrl) return;

    ErrorCode errorCode = ctrl->Stop();
    checkError(errorCode);
    m_paused = true;
    emit pausedChanged();
}

void DataController::stopAcquisition()
{
    WaveformAiCtrl *ctrl = static_cast<WaveformAiCtrl*>(m_waveformAiCtrl);
    if (!ctrl) return;

    ErrorCode errorCode = ctrl->Stop();
    checkError(errorCode);
    m_running = false;
    m_paused = false;

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

void DataController::onDataReady(const QVector<double> &displayData, int plotCount, int pointsPerChan,
                                  double xInc, bool recordEnd, double peakTimeSec, double peakVoltage,
                                  double thickness)
{
    m_thickness = thickness;
    emit thicknessChanged();

    m_totalAttempts++;
    if (!std::isnan(thickness)) {
        m_validDetections++;
        // Thickness statistics
        if (thickness > m_thicknessMax) m_thicknessMax = thickness;
        if (thickness < m_thicknessMin) m_thicknessMin = thickness;
        m_thicknessSum += thickness;
        m_thicknessCount++;
        emit thicknessStatsChanged();
    }

    m_detectionRate = m_totalAttempts > 0 ? (double)m_validDetections / m_totalAttempts * 100.0 : 0.0;
    emit detectionRateChanged();

    {
        QMutexLocker locker(&m_thicknessMutex);
        m_thicknessHistory.append(thickness);
    }

    emit dataReady(displayData, plotCount, pointsPerChan, xInc, recordEnd);
    emit markerReady(peakTimeSec, peakVoltage);
    emit thicknessHistoryUpdated(thickness);

    // Save per frame
    if (m_savePerFrame) {
        QMutexLocker locker(&m_fileMutex);
        m_saveFrameCounter++;
        QString fileName = QString("%1/Frame_%2.csv")
            .arg(m_saveFolder)
            .arg(m_saveFrameCounter, 4, 10, QChar('0'));
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "Index,Data,Thickness\n";
            QString thickStr = std::isnan(thickness) ? "NaN" : QString::number(thickness, 'f', 6);
            for (int i = 0; i < pointsPerChan; ++i) {
                out << i << "," << displayData[i] << "," << thickStr << "\n";
            }
            file.close();
        }
    }
}

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
}

void DataController::applyBandpassFilter(const double* input, double* output, int len)
{
    // 优化(锁范围): 仅在拷贝系数时持锁，O(N) 计算在锁外进行，避免阻塞其他线程
    std::vector<double> coeffsCopy;
    {
        QMutexLocker locker(&m_filterMutex);
        if (m_filterCoeffs.empty()) {
            memcpy(output, input, len * sizeof(double));
            return;
        }
        coeffsCopy = m_filterCoeffs;   // 拷贝后立即释放锁
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

    std::vector<double> feature(morph_len);
    if (usePeakMode) {
        // 腐蚀: eroded[k] = min(signal[erode_left+k-half_struct .. +half_struct])
        std::vector<double> eroded(erode_len);
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
        std::vector<double> opened(morph_len);
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
        std::vector<double> dilated(erode_len);
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
        std::vector<double> closed(morph_len);
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

    std::vector<int> cand_local;
    for (int i = win_left_local + 1; i < win_right_local; ++i) {
        if (feature[i] > feature[i-1] && feature[i] > feature[i+1])
            cand_local.push_back(i);
    }
    if (cand_local.empty()) return NAN;

    std::vector<double> sorted(feature.begin() + win_left_local, feature.begin() + win_right_local + 1);
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[win_len/2];
    std::vector<double> abs_dev;
    for (int i = win_left_local; i <= win_right_local; ++i)
        abs_dev.push_back(std::fabs(feature[i] - median));
    std::sort(abs_dev.begin(), abs_dev.end());
    double mad = abs_dev[win_len/2];
    double dynamic_thresh = median + 2.0 * mad;
    double feat_max = 0.0;
    for (int i = win_left_local; i <= win_right_local; ++i)
        if (feature[i] > feat_max) feat_max = feature[i];
    dynamic_thresh = (std::max)(dynamic_thresh, 0.01 * feat_max);

    std::vector<int> valid_local;
    for (int idx : cand_local) {
        if (feature[idx] >= dynamic_thresh)
            valid_local.push_back(idx);
    }
    if (valid_local.empty()) return NAN;

    int best_local = valid_local[0];
    double best_val = feature[best_local];
    for (int idx : valid_local) {
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

    // 预先分配最大容量（每个通道最大点数 = sectionLength）
    static thread_local std::vector<double> combined;   // 线程局部，避免多线程冲突
    static thread_local std::vector<double> filtered;   // 用于存储滤波结果
    if (combined.capacity() < 2 * p->m_configure.sectionLength) {
        combined.reserve(2 * p->m_configure.sectionLength);
        filtered.reserve(p->m_configure.sectionLength);
    }

    do
    {
        getDataCount = qMin(bufSize, remainingCount);
        errorCode = ((WaveformAiCtrl*)sender)->GetData(getDataCount, p->m_scaledData, 0, &returnedCount,
                                                       &timeStamp, NULL, NULL);
        remainingCount -= returnedCount;
        if (errorCode != Success && errorCode != WarningRecordEnd)
        {
            QString message = QString("Sorry, there are some errors occurred, Error Code: 0x") +
                QString::number(errorCode, 16).right(8);
            QMetaObject::invokeMethod(p, [p, message]() {
                emit p->errorOccurred(message);
            }, Qt::QueuedConnection);
            return;
        }

        flagForRecordEnd = (errorCode == WarningRecordEnd) ? true : false;
        int pointsPerChan = returnedCount / p->m_configure.channelCount;
        double* displayData = p->m_scaledData;
        int plotCount = 1;

        // 复用 filtered 向量
        filtered.resize(pointsPerChan);
        p->applyBandpassFilter(p->m_scaledData, filtered.data(), pointsPerChan);
        p->detrendSignal(filtered);

        // 根据开关决定显示内容
        bool showOriginal = p->m_showOriginal;
        if (showOriginal) {
            combined.resize(2 * pointsPerChan);
            for (int i = 0; i < pointsPerChan; ++i) {
                combined[2 * i]     = p->m_scaledData[i];
                combined[2 * i + 1] = filtered[i];
            }
            displayData = combined.data();
            plotCount = 2;
        } else {
            displayData = filtered.data();
            plotCount = 1;
        }

        double xInc = p->m_xInc;
        QVector<double> qvDisplayData(displayData, displayData + plotCount * pointsPerChan);
        QMetaObject::invokeMethod(p, [p, qvDisplayData, plotCount, pointsPerChan, xInc, flagForRecordEnd]() {
            emit p->dataReady(qvDisplayData, plotCount, pointsPerChan, xInc, flagForRecordEnd);
        }, Qt::QueuedConnection);

        if (pointsPerChan == p->m_configure.sectionLength)
        {
            std::vector<double> signalSegment(filtered.begin(), filtered.begin() + pointsPerChan);
            double peakTimeSec = 0.0, peakVoltage = 0.0;
            double thickness = p->detectFirstPeak(signalSegment,
                                                   p->m_configure.clockRatePerChan,
                                                   p->m_configure.detectMode,
                                                   p->m_configure.thicknessStd,
                                                   p->m_configure.thicknessTolerance,
                                                   p->m_configure.structLen,
                                                   p->m_configure.minWidth,
                                                   p->m_configure.delayCount,
                                                   peakTimeSec,
                                                   peakVoltage);

            // Compute SNR: noise RMS from first part of signal (delay region)
            int noiseLen = std::min(200, pointsPerChan / 4);
            if (noiseLen > 0 && std::fabs(peakVoltage) > 1e-9) {
                double noiseSumSq = 0.0;
                for (int i = 0; i < noiseLen; ++i) {
                    noiseSumSq += filtered[i] * filtered[i];
                }
                double noiseRms = std::sqrt(noiseSumSq / noiseLen);
                if (noiseRms > 1e-12) {
                    p->m_snr = 20.0 * std::log10(std::fabs(peakVoltage) / noiseRms);
                } else {
                    p->m_snr = 0.0;
                }
            } else {
                p->m_snr = 0.0;
            }


            QMetaObject::invokeMethod(p, [p]() {
                emit p->clearMarkersRequested();
            }, Qt::QueuedConnection);

            QMetaObject::invokeMethod(p, [p, thickness]() {
                p->m_thickness = thickness;
                emit p->thicknessChanged();
            }, Qt::QueuedConnection);

            QMetaObject::invokeMethod(p, [p, peakTimeSec, peakVoltage]() {
                emit p->markerReady(peakTimeSec, peakVoltage);
            }, Qt::QueuedConnection);

            p->m_totalAttempts++;
            if (!std::isnan(thickness)) {
                p->m_validDetections++;
                if (thickness > p->m_thicknessMax) p->m_thicknessMax = thickness;
                if (thickness < p->m_thicknessMin) p->m_thicknessMin = thickness;
                p->m_thicknessSum += thickness;
                p->m_thicknessCount++;
            }

            {
                QMutexLocker locker(&p->m_thicknessMutex);
                p->m_thicknessHistory.append(thickness);
            }

            QMetaObject::invokeMethod(p, [p]() {
                p->updateDetectionRate();
            }, Qt::QueuedConnection);

            QMetaObject::invokeMethod(p, [p]() {
                emit p->snrChanged();
            }, Qt::QueuedConnection);

            QMetaObject::invokeMethod(p, [p]() {
                emit p->thicknessStatsChanged();
            }, Qt::QueuedConnection);

            // 波形数据保存
            if (p->m_savePerFrame) {
                QMutexLocker locker(&p->m_fileMutex);
                p->m_saveFrameCounter++;
                int frameNum = p->m_saveFrameCounter;

                QString fileName = QString("%1/Frame_%2.csv")
                    .arg(p->m_saveFolder)
                    .arg(frameNum, 4, 10, QChar('0'));
                QFile file(fileName);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out << "Index,Original,Filtered,Thickness\n";
                    QString thickStr = std::isnan(thickness) ? "NaN" : QString::number(thickness, 'f', 6);
                    for (int i = 0; i < pointsPerChan; ++i) {
                        out << i << ","
                            << p->m_scaledData[i] << ","
                            << filtered[i] << ","
                            << thickStr << "\n";
                    }
                    file.close();
                } else {
                    qWarning() << "Cannot open file for writing:" << fileName;
                }
            }

            // 厚度历史更新
            QMetaObject::invokeMethod(p, [p, thickness]() {
                emit p->thicknessHistoryUpdated(thickness);
            }, Qt::QueuedConnection);
        }

    } while (remainingCount > 0);

    qint64 elapsed_us = timer.nsecsElapsed() / 1000;
    qDebug() << "Data processing took" << elapsed_us << "us";
}