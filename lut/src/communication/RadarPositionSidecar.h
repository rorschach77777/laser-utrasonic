#pragma once

#include "RadarFrameMatcher.h"
#include "BdaqHostTimeAligner.h"
#include "PipeHeadAxialTracker.h"
#include "AxialTailFill.h"
#include "RadarUdpReceiver.h"
#include "RadarUdpSharedService.h"

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <map>
#include <deque>
#include <mutex>
#include <atomic>

// Lives entirely on a dedicated Qt thread. Does NOT modify thickness data.
class RadarPositionWorker final : public QObject
{
    Q_OBJECT
public:
    enum class CsvMode { Diagnostics, AxialThickness };
    explicit RadarPositionWorker(const QString &csvPath, bool sharedRadar = false,
                                 bool saveCsv = true,
                                 CsvMode csvMode = CsvMode::Diagnostics);
    void start();
    void addRadarSample(const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs);
    void addFrame(RadarFrameMatcher::Frame frame);
    void addThickness(quint64 frameId, double thicknessMm, double snrDb, bool valid);
    void setLaserHeadFrame(quint64 firstStableFrameId);
    void addBackfill(quint64 frameId, double thicknessMm, double snrDb);
    void noteRadarQueueOverflow(quint64 dropped);
    void noteMetadataQueueOverflow(quint64 dropped);
    void stop();
    void setRadarToLaserDistanceMm(double mm) {
        m_headTracker.setRadarToLaserDistanceMm(mm);
    }
    const std::vector<AxialTailFill::EstimatedPoint> &tailFillPoints() const {
        return m_tailFillPoints;
    }
    QString tailFillDecision() const { return m_tailFillDecision; }
    bool hasCompletedPipe() const { return m_pipeCompleted; }
    const std::deque<AxialTailFill::RealPoint> &recentRealAxialPoints() const {
        return m_recentRealAxialPoints;
    }
    size_t pendingJoinCount() const { return m_joinRows.size(); } // worker-thread only
    quint64 expiredJoinCount() const { return m_joinTimedOut; }
    quint64 capacityJoinCount() const { return m_joinForced; }

signals:
    void statusMessage(const QString &message);
    void axialPointReady(quint64 frameId, double laserHeadMm, double thicknessMm,
                         const QString &quality);
    void headStatusChanged(const QString &status);
    void pipeCompleted(int estimatedCount, double distanceMm, const QString &decision);

private:
    void save(const RadarFrameMatcher::Match &match);
    void resolvePending();
    struct JoinedRow {
        RadarFrameMatcher::Frame frame;
        RadarFrameMatcher::Match callbackMatch;
        RadarFrameMatcher::Match bdaqMatch;
        PipeHeadAxialTracker::Position callbackHead;
        PipeHeadAxialTracker::Position bdaqHead;
        double thicknessMm = std::numeric_limits<double>::quiet_NaN();
        double snrDb = std::numeric_limits<double>::quiet_NaN();
        bool hasPosition = false;
        bool hasThickness = false;
        bool valid = false;
        bool backfilled = false;
        qint64 receivedUs = 0; // monotonic time of first metadata, not BDAQ epoch
        QString joinFailure;
    };
    void finishJoined(bool finalizeAll = false);
    void tryResolveLaserHead();
    void writeJoined(const JoinedRow &row, const QString &revision);
    void flushAxialRowsThrough(quint64 lastFrameId);
    void writeFinalRadarUdpSummary();
    void rememberTailThickness(const JoinedRow &row, bool hasRadarPosition);
    qint64 effectiveRadarLossUs(qint64 nowUs) const;
    void tryCompletePipe(); // called by live worker timer, independent of DAQ Stop

    QString m_csvPath;
    QFile *m_csvFile = nullptr; // created in worker thread
    QTextStream m_csvStream;
    QFile *m_joinFile = nullptr; // created in worker thread
    QTextStream m_joinStream;
    QFile *m_axialFile = nullptr; // ONLY the plotted valid axial points
    QTextStream m_axialStream;
    quint64 m_axialWritten = 0;
    std::deque<AxialTailFill::RealPoint> m_recentRealAxialPoints;
    // Retain all recent ultrasonic frames, including those with NO radar x.
    // Bounded to ~82 seconds at 100 Hz for the 510mm installation;
    // late Frame ID revisions overwrite.
    std::deque<AxialTailFill::TailSample> m_recentThicknessFrames;
    std::vector<AxialTailFill::EstimatedPoint> m_tailFillPoints;
    QString m_tailFillDecision;
    bool m_pipeCompleted = false;
    qint64 m_lastActiveRadarUs = -1;
    qint64 m_radarLossUs = -1; // first inactive UDP after valid pipe motion
    // Last actual UDP as consumed before DAQ stop. Never confuse its
    // TotalLength with the last valid ultrasonic thickness coordinate.
    bool m_haveRadarUdp = false;
    quint32 m_firstRadarLengthRaw = 0;
    quint32 m_finalRadarLengthRaw = 0;
    qint32 m_finalRadarSpeedRaw = 0;
    quint8 m_finalRadarGoodRaw = 0;
    qint64 m_finalRadarHostReceiveUs = -1;
    quint64 m_radarQueueDrops = 0;
    struct AxialCsvRow {
        double positionMm = 0.0;
        double thicknessMm = 0.0;
        double snrDb = 0.0;
        double bdaqStartTime = 0.0;
        QString revision;
        QString bdaqStatus;
        QString positionQuality;
        bool backfilled = false;
    };
    // Same bounded 256-Frame ID revision lifetime as m_writtenRows.
    // Keep the FINAL result, then write each Frame ID exactly once.
    std::map<quint64, AxialCsvRow> m_axialPendingRows;
    BdaqHostTimeAligner m_bdaqAligner;
    PipeHeadAxialTracker m_headTracker;
    std::map<quint64, JoinedRow> m_joinRows;
    std::map<quint64, JoinedRow> m_writtenRows;
    static constexpr qint64 JoinTimeoutUs = 300000;
    static constexpr size_t MaxJoinRows = 64;
    quint64 m_joinTimedOut = 0;
    quint64 m_joinForced = 0;
    quint64 m_latestThicknessId = 0;
    quint64 m_lastFinalizedFrameId = 0; // reject late metadata beyond revision retention
    quint64 m_laserHeadFrameId = 0;
    bool m_geometryHeadOriginReady = false; // first-radar-motion minus configured D
    bool m_laserArrivalWaitReported = false; // do not emit 211/s duplicate status updates
    RadarUdpReceiver *m_receiver = nullptr;
    QTimer *m_expiryTimer = nullptr;
    RadarFrameMatcher m_matcher;
    quint64 m_completed = 0;
    quint64 m_valid = 0;
    bool m_started = false;
    bool m_sharedRadar = false;
    bool m_saveCsv = true;
    CsvMode m_csvMode = CsvMode::Diagnostics;
};

// Thread-owning, opt-in sidecar interface. Only DAQ metadata is queued;
// heavy waveform processing, history, prediction and thickness are unchanged.
class RadarPositionSidecar final : public QObject
{
    Q_OBJECT
public:
    explicit RadarPositionSidecar(QObject *parent = nullptr);
    ~RadarPositionSidecar() override;

    void setSharedRadarSource(RadarUdpSharedService *source);
    bool startSession(bool saveCsv = true,
                      double radarToLaserDistanceMm = RadarLaserGeometry::DefaultRadarToLaserMm);
    void submitFrame(quint64 frameId, qint64 callbackSteadyUs, double bdaqTimestamp,
                     qint64 callbackUnixUs, qint64 clockPairSpreadUs);
    void submitThickness(quint64 frameId, double thicknessMm, double snrDb, bool valid);
    void confirmLaserHeadFrame(quint64 firstStableFrameId);
    void submitBackfill(quint64 targetFrameId, double thicknessMm, double snrDb);
    void markMeasurementStopped(); // freeze final UDP at actual DAQ stop, before result drains
    void stopSession();
    bool active() const { return m_active; }
    QString outputPath() const { return m_outputPath; }
    struct MailboxStats {
        size_t radarQueued = 0;
        size_t metadataQueued = 0;
        size_t peakRadarQueued = 0;
        size_t peakMetadataQueued = 0;
        quint64 droppedRadar = 0;
        quint64 droppedMetadata = 0;
    };
    MailboxStats mailboxStats() const;

signals:
    void statusMessage(const QString &message);
    void axialPointReady(quint64 frameId, double laserHeadMm, double thicknessMm,
                         const QString &quality);
    void headStatusChanged(const QString &status);
    void axialTailPointReady(double positionMm, double estimatedThicknessMm);
    void tailFillFinished(int estimatedCount, double requestedLengthMm,
                          const QString &decision);

private:
    struct MailEvent {
        enum class Kind { Radar, Frame, Thickness, Backfill, Head } kind = Kind::Radar;
        RadarUdpProtocol::Packet radar;
        RadarFrameMatcher::Frame frame;
        qint64 radarUs = 0;
        quint64 frameId = 0;
        double thickness = 0.0;
        double snr = 0.0;
        bool valid = false;
    };
    static constexpr size_t MaxRadarQueued = 512;
    static constexpr size_t MaxMetadataQueued = 256;
    static constexpr size_t DrainBatch = 128;
    void enqueueEvent(MailEvent event);
    void drainMailbox(RadarPositionWorker *worker);
    mutable std::mutex m_mailMutex;
    std::deque<MailEvent> m_mailbox;
    size_t m_radarQueued = 0;
    size_t m_metadataQueued = 0;
    size_t m_peakRadarQueued = 0;
    size_t m_peakMetadataQueued = 0;
    quint64 m_totalRadarDropped = 0;
    quint64 m_totalMetadataDropped = 0;
    quint64 m_radarDroppedPending = 0;
    quint64 m_metadataDroppedPending = 0;
    bool m_drainPosted = false;
    bool m_mailOpen = false;
    std::atomic<qint64> m_radarStopCutoffUs{0}; // zero means capture ongoing
    QThread m_thread;
    RadarPositionWorker *m_worker = nullptr;
    QString m_outputPath;
    double m_sessionRadarToLaserMm = PipeHeadAxialTracker::DefaultRadarToLaserMm;
    bool m_active = false; // accessed only on the owning (GUI) thread
    quint64 m_sessionGeneration = 0; // ignore queued signals from the previous pipe
    bool m_completionDelivered = false; // avoid repeating auto-finalization at DAQ Stop
    RadarUdpSharedService *m_sharedRadarSource = nullptr;
};
