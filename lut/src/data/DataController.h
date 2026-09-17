#ifndef DATACONTROLLER_H
#define DATACONTROLLER_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include "bdaqctrl.h"
#include "HistoryManager.h"

// 配置参数结构体
struct ConfigureParameter
{
    QString deviceName;
    int channelCount;
    int channelStart;
    int valueRange;          // Automation::BDaq::ValueRange
    double clockRatePerChan;
    int sectionLength;
    int cycles;
    QString profilePath;
    QString hostName;

    // trigger
    int triggerAction;       // Automation::BDaq::TriggerAction
    int triggerSourceIndex;
    int triggerEdge;         // Automation::BDaq::ActiveSignal
    int delayCount;
    double triggerLevel;
    int actualDelayCount;

    // peak finding
    QString detectMode;
    double thicknessStd;
    double thicknessTolerance;
    int structLen;
    int minWidth;
};

class DataController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(double thickness READ thickness NOTIFY thicknessChanged)
    Q_PROPERTY(double detectionRate READ detectionRate NOTIFY detectionRateChanged)
    Q_PROPERTY(double snr READ snr NOTIFY snrChanged)
    Q_PROPERTY(double maxThickness READ maxThickness NOTIFY thicknessStatsChanged)
    Q_PROPERTY(double minThickness READ minThickness NOTIFY thicknessStatsChanged)
    Q_PROPERTY(double avgThickness READ avgThickness NOTIFY thicknessStatsChanged)
    Q_PROPERTY(QString snrText READ snrText NOTIFY snrChanged)
    Q_PROPERTY(QString maxThicknessText READ maxThicknessText NOTIFY thicknessStatsChanged)
    Q_PROPERTY(QString minThicknessText READ minThicknessText NOTIFY thicknessStatsChanged)
    Q_PROPERTY(QString avgThicknessText READ avgThicknessText NOTIFY thicknessStatsChanged)
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged)
    Q_PROPERTY(bool showOriginal READ showOriginal WRITE setShowOriginal NOTIFY showOriginalChanged)
    Q_PROPERTY(bool savePerFrame READ savePerFrame WRITE setSavePerFrame NOTIFY savePerFrameChanged)
    Q_PROPERTY(int waveformRefreshInterval READ waveformRefreshInterval WRITE setWaveformRefreshInterval NOTIFY waveformRefreshIntervalChanged)
    Q_PROPERTY(bool historyRecording READ historyRecording WRITE setHistoryRecording NOTIFY historyRecordingChanged)
    Q_PROPERTY(QString detectionRateText READ detectionRateText NOTIFY detectionRateChanged)
    Q_PROPERTY(QString thicknessText READ thicknessText NOTIFY thicknessChanged)
    Q_PROPERTY(QStringList deviceList READ deviceList NOTIFY deviceListChanged)
    Q_PROPERTY(QStringList triggerSourceList READ triggerSourceList NOTIFY triggerSourceListChanged)
    Q_PROPERTY(QStringList triggerEdgeList READ triggerEdgeList NOTIFY triggerEdgeListChanged)
    Q_PROPERTY(bool triggerSupported READ isTriggerSupported NOTIFY triggerSourceListChanged)

public:
    explicit DataController(QObject *parent = nullptr);
    ~DataController();

    Q_INVOKABLE void initialize();

    // Device management
    Q_INVOKABLE void refreshDeviceList();
    Q_INVOKABLE void selectDevice(int index);
    Q_INVOKABLE void browseProfile();
    Q_INVOKABLE void setProfilePath(const QString &path);

    // Host name
    Q_INVOKABLE void setHostName(const QString &name);
    QString hostName() const;

    // Device list
    Q_INVOKABLE QStringList deviceList() const;
    Q_INVOKABLE QStringList triggerSourceList() const;
    Q_INVOKABLE QStringList triggerEdgeList() const;

    // Acquisition control
    Q_INVOKABLE void startAcquisition();
    Q_INVOKABLE void pauseAcquisition();
    Q_INVOKABLE void resumeAcquisition();
    Q_INVOKABLE void stopAcquisition();

    // Filter
    Q_INVOKABLE void applyFilter(double lowFreqMHz, double highFreqMHz);

    // Y-axis range
    Q_INVOKABLE void applyYRange(double yMin, double yMax);

    // Detection rate
    Q_INVOKABLE void resetDetectionRate();

    // Save
    Q_INVOKABLE void saveThicknessData(const QString &filePath);

    // Properties
    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    double thickness() const { return m_thickness; }
    double detectionRate() const { return m_detectionRate; }
    double snr() const { return m_snr; }
    double maxThickness() const { return m_thicknessMax; }
    double minThickness() const { return m_thicknessMin; }
    double avgThickness() const;
    bool autoScale() const { return m_autoScale; }
    bool showOriginal() const { return m_showOriginal.load(std::memory_order_relaxed); }
    bool savePerFrame() const { return m_savePerFrame.load(std::memory_order_relaxed); }
    int waveformRefreshInterval() const { return m_waveformRefreshInterval.load(std::memory_order_relaxed); }
    bool isTriggerSupported() const { return m_isTriggerSupported; }
    QString detectionRateText() const;
    QString thicknessText() const;
    QString snrText() const;
    QString maxThicknessText() const;
    QString minThicknessText() const;
    QString avgThicknessText() const;

    Q_INVOKABLE void setAutoScale(bool enabled);
    Q_INVOKABLE void setShowOriginal(bool enabled);
    Q_INVOKABLE void setSavePerFrame(bool enabled);
    Q_INVOKABLE void setWaveformRefreshInterval(int interval);

    // History recording
    bool historyRecording() const { return m_historyRecording; }
    Q_INVOKABLE void setHistoryRecording(bool enabled);
    void setHistoryManager(QObject *manager);

    // Configure parameters
    Q_INVOKABLE void setTriggerSource(int index);
    Q_INVOKABLE void setTriggerEdge(int index);
    Q_INVOKABLE void setDelayCount(int count);
    Q_INVOKABLE void setTriggerLevel(double level);
    Q_INVOKABLE void setDetectMode(const QString &mode);
    Q_INVOKABLE void setThicknessStd(double val);
    Q_INVOKABLE void setThicknessTolerance(double val);
    Q_INVOKABLE void setStructLen(int val);
    Q_INVOKABLE void setMinWidth(int val);

    // Graph data access
    Q_INVOKABLE QVector<double> getThicknessHistory() const;

    // Internal access for the graph items
    void *waveformAiCtrl() const;
    const ConfigureParameter &configure() const { return m_configure; }
    double *scaledData() const { return m_scaledData; }
    int rawDataBufferLength() const { return m_rawDataBufferLength; }

    // BDAQ static callback
    static void OnDataReadyEvent(void *sender, Automation::BDaq::BfdAiEventArgs *args, void *userParam);

signals:
    void runningChanged();
    void pausedChanged();
    void thicknessChanged();
    void detectionRateChanged();
    void snrChanged();
    void thicknessStatsChanged();
    void autoScaleChanged();
    void showOriginalChanged();
    void savePerFrameChanged();
    void waveformRefreshIntervalChanged();

    // Data ready signal for graph update
    void dataReady(const QVector<double> &data, int plotCount, int pointsPerChan,
                   double xInc, bool recordEnd);

    // Marker signal
    void markerReady(double peakTimeSec, double peakVoltage);
    void clearMarkersRequested();

    // Thickness history update
    void thicknessHistoryUpdated(double thickness);

    // History recording
    void historyRecordingChanged();

    // Device list updated
    void deviceListChanged();
    void triggerSourceListChanged();
    void triggerEdgeListChanged();

    // Error
    void errorOccurred(const QString &message);

    // Graph configuration
    void graphConfigReady(double xCordTimeDiv, double xCordTimeOffset, int timeUnit,
                          double yMin, double yMax, int yUnit);

    // Profile browse
    void profileBrowseRequested();

private:
    bool m_running;
    bool m_paused;
    double m_thickness;
    double m_detectionRate;
    double m_snr;
    double m_thicknessMax;
    double m_thicknessMin;
    double m_thicknessSum;
    int m_thicknessCount;
    bool m_autoScale;
    std::atomic<bool> m_showOriginal{true};
    std::atomic<bool> m_savePerFrame{false};
    // 波形图只负责显示，可按1/10/100帧抽帧刷新；采集、测厚和厚度历史仍逐帧处理。
    std::atomic<int> m_waveformRefreshInterval{1};
    std::atomic<quint64> m_waveformFrameCounter{0};
    bool m_historyRecording; // Whether to record for history - default false
    HistoryManager *m_historyManager;

    ConfigureParameter m_configure;
    void *m_waveformAiCtrl;  // Automation::BDaq::WaveformAiCtrl*
    double *m_scaledData;
    double m_xInc;
    int m_rawDataBufferLength;

    QStringList m_deviceList;
    QStringList m_triggerSourceList;
    QStringList m_triggerEdgeList;

    int m_totalAttempts;
    int m_validDetections;
    QVector<double> m_thicknessHistory;
    QMutex m_thicknessMutex;

    bool m_isTriggerSupported;
    int m_filterOrder;
    std::vector<double> m_filterCoeffs;
    QMutex m_filterMutex;
    // FIR参数变化版本号：采集线程仅在版本变化时重新拷贝系数，避免100 Hz下每帧加锁+复制。
    std::atomic<quint64> m_filterRevision{0};

    int m_saveFrameCounter;
    QString m_saveFolder;
    QMutex m_fileMutex;

    // 逐帧CSV写盘与DAQ回调解耦。队列不主动丢帧，停止采集后排空再结束。
    struct FrameSaveJob {
        QString fileName;
        std::vector<double> original;
        std::vector<double> filtered;
        double thickness = std::numeric_limits<double>::quiet_NaN();
    };
    std::thread m_saveWorker;
    std::mutex m_saveQueueMutex;
    std::condition_variable m_saveQueueCv;
    std::condition_variable m_saveQueueDrainedCv;
    std::deque<FrameSaveJob> m_saveQueue;
    bool m_saveWorkerStop = false;
    bool m_saveWorkerBusy = false;
    std::atomic<quint64> m_saveQueuePeak{0};

    // DAQ回调与现有数据处理链解耦：回调只复制采集帧，单独线程按FIFO执行滤波/旧寻峰/SNR。
    struct AcquiredFrameJob {
        std::vector<double> original;
        ConfigureParameter configure;
        QString saveFileName;
        double xInc = 0.0;
        int filterOrder = 0;
        int waveformRefreshInterval = 1;
        bool showOriginal = true;
        bool recordEnd = false;
    };
    std::thread m_processingWorker;
    std::mutex m_processingQueueMutex;
    std::condition_variable m_processingQueueCv;
    std::condition_variable m_processingQueueDrainedCv;
    std::deque<AcquiredFrameJob> m_processingQueue;
    bool m_processingWorkerStop = false;
    bool m_processingWorkerBusy = false;
    std::atomic<quint64> m_processingQueuePeak{0};
    // 已计算但尚未在DataController所属Qt线程提交的厚度/历史结果数。
    std::atomic<int> m_pendingResultCommits{0};

    void applySettings();
    void configureDevice();
    void configureGraph();
    void checkError(int errorCode);
    void updateFilterCoefficients(double lowFreqHz, double highFreqHz);
    void designBandpassFilter(double lowFreq, double highFreq, double sampleRate,
                              int order, std::vector<double> &coeffs);
    void applyBandpassFilter(const double *input, double *output, int length);
    void detrendSignal(std::vector<double> &signal);
    double detectFirstPeak(const std::vector<double> &signal, double clockRate,
                           const QString &mode, double thicknessStd,
                           double tolerance, int structLen, int minWidth,
                           int delayCount, double &peakTime, double &peakVoltage);
    void updateDetectionRate();
    void ensureSaveWorkerStarted();
    void enqueueFrameSave(FrameSaveJob job);
    void drainSaveQueue();
    void stopSaveWorker();
    void saveWorkerLoop();
    void ensureProcessingWorkerStarted();
    void enqueueAcquiredFrame(AcquiredFrameJob job);
    void drainProcessingQueue();
    void stopProcessingWorker();
    void processingWorkerLoop();
    void processAcquiredFrame(AcquiredFrameJob job);
    void flushPendingResultCommits();
};

#endif // DATACONTROLLER_H