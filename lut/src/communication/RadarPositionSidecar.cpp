#include "RadarPositionSidecar.h"
#include "RadarHostClock.h"

#include <QDateTime>
#include <QDir>
#include <QHostAddress>
#include <QMetaObject>
#include <QNetworkProxy>
#include <QStringList>

#include <cmath>

RadarPositionWorker::RadarPositionWorker(const QString &csvPath, bool sharedRadar,
                                         bool saveCsv, CsvMode csvMode)
    : m_csvPath(csvPath), m_sharedRadar(sharedRadar), m_saveCsv(saveCsv),
      m_csvMode(csvMode)
{
}

void RadarPositionWorker::start()
{
    if (m_started)
        return;
    m_matcher.clear();
    m_bdaqAligner.clear();
    m_headTracker.clear();
    m_joinRows.clear();
    m_writtenRows.clear();
    m_latestThicknessId = 0;
    m_lastFinalizedFrameId = 0;
    m_joinTimedOut = m_joinForced = 0;
    m_laserHeadFrameId = 0;
    m_geometryHeadOriginReady = false;
    m_laserArrivalWaitReported = false;
    m_completed = m_valid = 0;
    m_axialWritten = 0;
    m_axialPendingRows.clear();
    m_recentRealAxialPoints.clear();
    m_recentThicknessFrames.clear();
    m_tailFillPoints.clear();
    m_tailFillDecision.clear();
    m_pipeCompleted = false;
    m_lastActiveRadarUs = -1;
    m_radarLossUs = -1;
    m_haveRadarUdp = false;
    m_firstRadarLengthRaw = m_finalRadarLengthRaw = 0;
    m_finalRadarSpeedRaw = 0;
    m_finalRadarGoodRaw = 0;
    m_finalRadarHostReceiveUs = -1;
    m_radarQueueDrops = 0;
    if (m_saveCsv && m_csvMode == CsvMode::AxialThickness) {
        m_axialFile = new QFile(m_csvPath, this);
        if (!m_axialFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            emit statusMessage(QStringLiteral("轴向壁厚CSV打开失败：") + m_axialFile->errorString());
            delete m_axialFile;
            m_axialFile = nullptr;
            // The real-time axial curve must remain active even if disk fails.
        } else {
            m_axialStream.setDevice(m_axialFile);
            m_axialStream << "frame_id,axial_position_mm,thickness_mm,snr_db,revision,"
                             "backfilled,bdaq_start_time_unverified,bdaq_time_status,"
                             "position_quality,origin_reference,radar_to_laser_mm,"
                             "lb2_to_laser_mm,record_type,"
                             "radar_final_total_length_raw,radar_final_total_length_mm,"
                             "radar_session_delta_mm,radar_first_total_length_raw,"
                             "radar_final_host_receive_us,radar_final_good_reading_raw,"
                             "radar_final_instant_speed_raw,radar_final_status\n";
        }
    }
    if (m_saveCsv && m_csvMode == CsvMode::Diagnostics) {
    m_csvFile = new QFile(m_csvPath, this);
    if (!m_csvFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit statusMessage(QStringLiteral("Radar sidecar CSV open failed: ") + m_csvFile->errorString());
        delete m_csvFile;
        m_csvFile = nullptr;
        return;
    }
    m_csvStream.setDevice(m_csvFile);
    m_csvStream << "frame_id,daq_callback_host_steady_us,bdaq_timestamp_unverified,"
                   "radar_before_host_steady_us,radar_after_host_steady_us,bracket_gap_us,"
                   "radar_before_length_raw,radar_after_length_raw,"
                   "radar_before_good_raw,radar_after_good_raw,"
                   "radar_interpolated_length_raw,radar_relative_mm,quality,"
                   "time_source,position_reference,match_resolved_host_steady_us,match_latency_us,"
                   "bdaq_estimated_host_steady_us,bdaq_period_us,bdaq_callback_lag_us,"
                   "bdaq_model_status,bdaq_position_mm,bdaq_match_quality,"
                   "bdaq_minus_callback_position_mm,daq_callback_unix_us,clock_pair_spread_us\n";

    QString joinPath = m_csvPath;
    joinPath.replace(QStringLiteral("FramePosition_"), QStringLiteral("FrameThicknessPosition_"));
    m_joinFile = new QFile(joinPath, this);
    if (!m_joinFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit statusMessage(QStringLiteral("Thickness-position CSV open failed: ") + m_joinFile->errorString());
        delete m_joinFile;
        m_joinFile = nullptr;
        m_csvStream.setDevice(nullptr);
        m_csvFile->close();
        delete m_csvFile;
        m_csvFile = nullptr;
        return;
    }
    m_joinStream.setDevice(m_joinFile);
    m_joinStream << "frame_id,revision,daq_callback_host_steady_us,bdaq_start_time_raw,"
                    "bdaq_estimated_host_steady_us,bdaq_period_us,bdaq_callback_lag_us,"
                    "bdaq_status,callback_position_mm,callback_quality,"
                    "bdaq_position_mm,bdaq_quality,bdaq_minus_callback_mm,"
                    "thickness_mm,snr_db,thickness_valid,backfilled,position_reference,"
                    "daq_callback_unix_us,clock_pair_spread_us,"
                    "laser_head_callback_mm,laser_head_bdaq_mm,head_origin_quality,head_layout,"
                    "laser_head_thickness_confirmed_mm,laser_head_thickness_origin_frame,"
                    "laser_head_thickness_origin_quality,"
                    "axial_origin_reference,radar_to_laser_mm,lb2_to_laser_mm,join_quality\n";
    } // CSV output is optional. The live matcher and axial signal always run.

    if (!m_sharedRadar) {
    // Standalone probe/test mode. In appLST, the shared radar service owns UDP 1111.
    m_receiver = new RadarUdpReceiver(this);
    connect(m_receiver, &RadarUdpReceiver::radarSampleReceived,
            this, [this](const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs) {
        addRadarSample(packet, hostSteadyUs);
    });
    connect(m_receiver, &RadarUdpReceiver::receiverError,
            this, [this](const QString &message) {
        emit statusMessage(QStringLiteral("Radar UDP: ") + message);
    });

    // No overlap with RadarUdpProbe or PCIS on port 1111.
    if (!m_receiver->start(QHostAddress(QStringLiteral("192.168.100.15")), 1111,
                            QHostAddress(QStringLiteral("192.168.100.110")))) {
        emit statusMessage(QStringLiteral("Radar sidecar UDP bind failed; close RadarUdpProbe/PCIS"));
        delete m_receiver;
        m_receiver = nullptr;
        if (m_csvFile) {
            m_csvStream.flush();
            m_csvStream.setDevice(nullptr);
            m_csvFile->close();
            delete m_csvFile;
            m_csvFile = nullptr;
        }
        if (m_joinFile) {
            m_joinStream.setDevice(nullptr);
            m_joinFile->close();
            delete m_joinFile;
            m_joinFile = nullptr;
        }
        if (m_axialFile) {
            m_axialStream.setDevice(nullptr);
            m_axialFile->close();
            delete m_axialFile;
            m_axialFile = nullptr;
        }
        return;
    }
    }

    m_expiryTimer = new QTimer(this);
    m_expiryTimer->setInterval(20);
    connect(m_expiryTimer, &QTimer::timeout, this, &RadarPositionWorker::resolvePending);
    m_expiryTimer->start();
    m_started = true;
    emit statusMessage(m_axialFile || m_csvFile
        ? QStringLiteral("轴向壁厚实时关联运行，CSV保存：") + m_csvPath
        : QStringLiteral("轴向壁厚实时关联运行，不保存CSV"));
}

void RadarPositionWorker::noteRadarQueueOverflow(quint64 dropped)
{
    m_radarQueueDrops += dropped;
    m_matcher.invalidateRadarSamples();
    m_headTracker.invalidateContinuity();
    emit headStatusChanged(QStringLiteral(
        "雷达跨线程队列溢出：管头轴向位置已失效，需要新钢管采集会话"));
    emit statusMessage(QStringLiteral(
        "RADAR_QUEUE_OVERFLOW: dropped %1 radar packets; position invalidated")
        .arg(dropped));
}

void RadarPositionWorker::noteMetadataQueueOverflow(quint64 dropped)
{
    emit headStatusChanged(QStringLiteral(
        "Frame ID关联队列溢出：部分轴向位置缺失，厚度主链保持正常"));
    emit statusMessage(QStringLiteral(
        "POSITION_METADATA_OVERFLOW: lost %1 sidecar messages; core thickness intact")
        .arg(dropped));
}

void RadarPositionWorker::addRadarSample(const RadarUdpProtocol::Packet &packet,
                                         qint64 hostSteadyUs)
{
    if (!m_started)
        return;
    RadarFrameMatcher::RadarSample sample;
    sample.hostReceiveUs = hostSteadyUs;
    sample.lengthRaw = packet.totalLengthBits;
    sample.speedRaw = packet.instantSpeedSigned();
    sample.goodRaw = packet.goodReadingRaw;
    // Capture all actual UDP packets, including GoodReading=0 or stopped
    // motion. The last radar TotalLength is NOT the last plotted x-coordinate.
    if (!m_haveRadarUdp) {
        m_firstRadarLengthRaw = packet.totalLengthBits;
        m_haveRadarUdp = true;
    }
    if (hostSteadyUs >= m_finalRadarHostReceiveUs) {
        m_finalRadarLengthRaw = packet.totalLengthBits;
        m_finalRadarSpeedRaw = packet.instantSpeedSigned();
        m_finalRadarGoodRaw = packet.goodReadingRaw;
        m_finalRadarHostReceiveUs = hostSteadyUs;
    }
    m_matcher.addRadarSample(sample);
    const QString beforeState = m_headTracker.state();
    m_headTracker.add(sample);
    // Detect the end of EFFECTIVE radar motion, not the end of UDP packets:
    // a device may continue sending frozen TotalLength / GoodReading=0.
    if (m_headTracker.hasReachedLaserGate()) {
        m_lastActiveRadarUs = hostSteadyUs;
    } else if (m_lastActiveRadarUs >= 0 && m_radarLossUs < 0) {
        m_radarLossUs = hostSteadyUs;
    }
    if (m_laserHeadFrameId && !m_geometryHeadOriginReady)
        tryResolveLaserHead();
    if (beforeState != m_headTracker.state())
        emit headStatusChanged(m_headTracker.state());
    resolvePending();
}

void RadarPositionWorker::addFrame(RadarFrameMatcher::Frame frame)
{
    if (!m_started)
        return;
    if (frame.frameId <= m_lastFinalizedFrameId) {
        emit statusMessage(QStringLiteral(
            "迟到Frame元数据超出联结窗口：Frame ID=%1，拒绝重复生成测厚位置行")
            .arg(frame.frameId));
        return;
    }
    const auto observed = m_bdaqAligner.observe(frame.bdaqTimestamp, frame.daqCallbackUs,
                                                 frame.daqCallbackUnixUs,
                                                 frame.callbackClockPairSpreadUs);
    frame.bdaqEstimatedHostUs = observed.estimatedHostUs;
    frame.bdaqPeriodUs = observed.periodUs;
    frame.bdaqCallbackLagUs = observed.callbackLagUs;
    frame.bdaqModelStatus = observed.status;
    JoinedRow &row = m_joinRows[frame.frameId];
    if (row.receivedUs == 0)
        row.receivedUs = RadarHostClock::nowUs();
    row.frame = frame;
    m_matcher.addFrame(frame);
    resolvePending();
}

void RadarPositionWorker::resolvePending()
{
    const qint64 nowUs = RadarHostClock::nowUs();
    m_matcher.resolve(nowUs, [this](const RadarFrameMatcher::Match &match) {
        save(match);
    });
    finishJoined();
    tryCompletePipe();
}

qint64 RadarPositionWorker::effectiveRadarLossUs(qint64 nowUs) const
{
    if (m_radarLossUs >= 0)
        return m_radarLossUs;
    // Device can stop UDP entirely rather than emit an inactive packet.
    // Confirm that its last valid motion has expired before estimating loss.
    return m_lastActiveRadarUs >= 0 &&
           nowUs - m_lastActiveRadarUs >= RadarFrameMatcher::FrameTimeoutUs
        ? m_lastActiveRadarUs + RadarFrameMatcher::MaxBracketUs : -1;
}

void RadarPositionWorker::tryCompletePipe()
{
    if (m_pipeCompleted || m_radarQueueDrops || !m_started ||
        m_recentRealAxialPoints.empty())
        return;
    const qint64 lossUs = effectiveRadarLossUs(RadarHostClock::nowUs());
    const auto &lastReal = m_recentRealAxialPoints.back();
    if (!AxialTailFill::endConfirmed(m_recentThicknessFrames, lossUs, lastReal))
        return;

    const double distanceMm = m_headTracker.radarToLaserDistanceMm();
    m_tailFillPoints = AxialTailFill::build(
        m_recentThicknessFrames, lossUs, lastReal, distanceMm);
    if (m_tailFillPoints.empty()) {
        m_tailFillDecision = QStringLiteral(
            "雷达失效后仍有厚度，最近10帧中达到5个NaN且至少3帧连续，已触发整管重绘；首个NaN前有效测厚点为0，不生成补偿");
    } else {
        m_tailFillDecision = QStringLiteral(
            "雷达失效后%1帧有效厚度（不限制SNR及壁厚波动）均分至管末%2mm；最近10帧内5个NaN且至少3帧连续，从首个NaN截止并立即重绘")
            .arg(m_tailFillPoints.size()).arg(distanceMm, 0, 'f', 1);
    }
    // Freeze BEFORE emitting signals: DAQ may continue indefinitely and later
    // noise or the next pipe must not alter the already finalized profile.
    m_pipeCompleted = true;
    // A single queued completion notification lets the GUI fetch all tail
    // points atomically; Stop cannot interleave between thousands of per-point
    // queued events and accidentally duplicate the tail.
    emit pipeCompleted(static_cast<int>(m_tailFillPoints.size()),
                       distanceMm, m_tailFillDecision);
}

void RadarPositionWorker::save(const RadarFrameMatcher::Match &m)
{
    auto numberOrBlank = [](double d, int precision) {
        return std::isfinite(d) ? QString::number(d, 'f', precision) : QString();
    };
    RadarFrameMatcher::Match bdaqMatch;
    bdaqMatch.quality = QStringLiteral("BDAQ_NOT_VALIDATED");
    if (m.frame.bdaqEstimatedHostUs >= 0) {
        auto bdaqFrame = m.frame;
        bdaqFrame.daqCallbackUs = m.frame.bdaqEstimatedHostUs;
        bdaqMatch = m_matcher.match(bdaqFrame, RadarHostClock::nowUs());
    }
    const double deltaMm = std::isfinite(bdaqMatch.relativeMm) && std::isfinite(m.relativeMm)
        ? bdaqMatch.relativeMm - m.relativeMm
        : std::numeric_limits<double>::quiet_NaN();
    auto row = m_joinRows.find(m.frame.frameId);
    if (row != m_joinRows.end()) {
        row->second.callbackMatch = m;
        row->second.bdaqMatch = bdaqMatch;
        row->second.callbackHead = m_headTracker.at(m);
        row->second.bdaqHead = m_headTracker.at(bdaqMatch);
        row->second.hasPosition = true;
        if (m.frame.frameId == m_laserHeadFrameId)
            tryResolveLaserHead();
    }
    const qint64 resolvedUs = RadarHostClock::nowUs();
    if (m_csvFile) {
    m_csvStream << m.frame.frameId << ',' << m.frame.daqCallbackUs << ','
                << QString::number(m.frame.bdaqTimestamp, 'g', 17) << ','
                << m.beforeUs << ',' << m.afterUs << ',' << m.bracketSpanUs << ','
                << m.beforeLengthRaw << ',' << m.afterLengthRaw << ','
                << int(m.beforeGood) << ',' << int(m.afterGood) << ','
                << numberOrBlank(m.rawInterpolated, 6) << ','
                << numberOrBlank(m.relativeMm, 6) << ','
                << m.quality << ",DAQ_CALLBACK_APPROX,RELATIVE_RADAR_FIRST_PACKET,"
                << resolvedUs << ',' << (resolvedUs - m.frame.daqCallbackUs) << ','
                << m.frame.bdaqEstimatedHostUs << ',' << m.frame.bdaqPeriodUs << ','
                << m.frame.bdaqCallbackLagUs << ',' << m.frame.bdaqModelStatus << ','
                << numberOrBlank(bdaqMatch.relativeMm, 6) << ',' << bdaqMatch.quality << ','
                << numberOrBlank(deltaMm, 6) << ','
                << m.frame.daqCallbackUnixUs << ','
                << m.frame.callbackClockPairSpreadUs << '\n';
    }
    ++m_completed;
    if (m.quality == QStringLiteral("INTERPOLATED"))
        ++m_valid;
    if (m_completed % 100 == 0) {
        if (m_csvFile)
            m_csvStream.flush();
        emit statusMessage(QStringLiteral(
            "实时位置关联：frames=%1 good=%2 waiting=%3 join=%4 timeout=%5 capacity=%6")
            .arg(m_completed).arg(m_valid).arg(m_matcher.pendingCount())
            .arg(m_joinRows.size()).arg(m_joinTimedOut).arg(m_joinForced));
    }
}

void RadarPositionWorker::addThickness(quint64 frameId, double thicknessMm,
                                       double snrDb, bool valid)
{
    if (!m_started)
        return;
    auto completed = m_writtenRows.find(frameId);
    if (completed != m_writtenRows.end()) {
        completed->second.thicknessMm = thicknessMm;
        completed->second.snrDb = snrDb;
        completed->second.valid = valid && std::isfinite(thicknessMm);
        completed->second.hasThickness = true;
        completed->second.backfilled = false;
        writeJoined(completed->second, QStringLiteral("LATE_THICKNESS"));
        return;
    }
    if (frameId <= m_lastFinalizedFrameId) {
        emit statusMessage(QStringLiteral(
            "迟到厚度超出联结修订窗口：Frame ID=%1，不能重新创建该帧位置")
            .arg(frameId));
        return;
    }
    auto &row = m_joinRows[frameId];
    if (row.receivedUs == 0)
        row.receivedUs = RadarHostClock::nowUs();
    if (row.frame.frameId == 0)
        row.frame.frameId = frameId; // keep real identity if Frame metadata was dropped
    row.thicknessMm = thicknessMm;
    row.snrDb = snrDb;
    row.valid = valid && std::isfinite(thicknessMm);
    row.hasThickness = true;
    // Observe the confirming 10-frame NaN vote AS IT ARRIVES, without waiting for
    // the separate radar/Frame join's two-frame revision horizon or timeout.
    // writeJoined will later replace this provisional record with final
    // position and backfill identity for the SAME Frame ID.
    rememberTailThickness(row, false);
    m_latestThicknessId = (std::max)(m_latestThicknessId, frameId);
    tryResolveLaserHead();
    finishJoined();
    tryCompletePipe();
}

void RadarPositionWorker::setLaserHeadFrame(quint64 frameId)
{
    if (!m_started || frameId == 0 || m_laserHeadFrameId != 0)
        return; // one front edge per DAQ session; don't reset during reacquisition
    m_laserHeadFrameId = frameId;
    tryResolveLaserHead();
}

void RadarPositionWorker::tryResolveLaserHead()
{
    if (!m_laserHeadFrameId || m_geometryHeadOriginReady)
        return;
    // The head proxy is anchored to FIRST effective radar output after
    // >=300ms of quiet, NOT to the first thickness frame. Do not require the
    // first thickness frame's BDAQ timestamp: it may be inside the 20-frame
    // warmup, while later properly mapped frames are still usable.
    if (!m_headTracker.hasReachedLaserGate()) {
        if (!m_laserArrivalWaitReported) {
            m_laserArrivalWaitReported = true;
            emit headStatusChanged(QStringLiteral(
                "三帧厚度已确认，但雷达尚未累计前进%1mm（或位置不可靠）；管头到达未确认")
                .arg(m_headTracker.radarToLaserDistanceMm(), 0, 'f', 1));
        }
        return;
    }
    // Only after BOTH independent gates (three-frame echo evidence and
    // configured radar displacement D) may the axial curve be published. The
    // first stable thickness frame never resets the radar position origin.
    m_geometryHeadOriginReady = true;
    m_laserArrivalWaitReported = false;
    emit headStatusChanged(QStringLiteral(
        "三帧测厚已确认：首次雷达运动后累计前进%1mm才放行轴向点（未标定）")
        .arg(m_headTracker.radarToLaserDistanceMm(), 0, 'f', 1));
}

void RadarPositionWorker::addBackfill(quint64 frameId, double thicknessMm, double snrDb)
{
    if (!m_started || !std::isfinite(thicknessMm))
        return;
    auto it = m_joinRows.find(frameId);
    if (it != m_joinRows.end()) {
        it->second.thicknessMm = thicknessMm;
        it->second.snrDb = snrDb;
        it->second.valid = true;
        it->second.hasThickness = true;
        it->second.backfilled = true;
        return;
    }
    // Backfills more than one frame behind can arrive after the FRAME line
    // was finalized. Append a revision with the SAME Frame ID and position.
    auto written = m_writtenRows.find(frameId);
    if (written != m_writtenRows.end()) {
        written->second.thicknessMm = thicknessMm;
        written->second.snrDb = snrDb;
        written->second.valid = true;
        written->second.backfilled = true;
        writeJoined(written->second, QStringLiteral("BACKFILL"));
    } else {
        emit statusMessage(QStringLiteral("Late backfill cannot be joined to retained Frame ID %1")
                           .arg(frameId));
    }
}

void RadarPositionWorker::rememberTailThickness(const JoinedRow &row,
                                                 bool hasRadarPosition)
{
    const auto &f = row.frame;
    if (!f.frameId || f.daqCallbackUs <= 0 || !row.hasThickness)
        return;
    // Prefer BDAQ-estimated acquisition time to callback time, so a delayed
    // callback cannot turn a pre-loss echo into a post-loss tail measurement.
    const qint64 frameTimeUs = f.bdaqEstimatedHostUs >= 0
        ? f.bdaqEstimatedHostUs : f.daqCallbackUs;
    const AxialTailFill::TailSample candidate{
        f.frameId, frameTimeUs, row.thicknessMm, row.snrDb,
        row.valid, row.backfilled, hasRadarPosition};
    const auto it = std::lower_bound(
        m_recentThicknessFrames.begin(), m_recentThicknessFrames.end(),
        f.frameId, [](const auto &p, quint64 id) { return p.frameId < id; });
    if (it != m_recentThicknessFrames.end() && it->frameId == f.frameId)
        *it = candidate;
    else
        m_recentThicknessFrames.insert(it, candidate);
    while (m_recentThicknessFrames.size() > AxialTailFill::MaxTailFrames)
        m_recentThicknessFrames.pop_front();
}

void RadarPositionWorker::writeJoined(const JoinedRow &row, const QString &revision)
{
    auto fmt = [](double value) {
        return std::isfinite(value) ? QString::number(value, 'f', 6) : QString();
    };
    const auto &f = row.frame;
    const auto &callback = row.callbackMatch;
    const auto &bdaq = row.bdaqMatch;
    const double delta = std::isfinite(callback.relativeMm) && std::isfinite(bdaq.relativeMm)
        ? bdaq.relativeMm - callback.relativeMm
        : std::numeric_limits<double>::quiet_NaN();
    double thicknessOriginPosition = std::numeric_limits<double>::quiet_NaN();
    QString thicknessOriginQuality = QStringLiteral("THICKNESS_HEAD_PENDING");
    if (m_laserHeadFrameId && f.frameId >= m_laserHeadFrameId &&
        m_geometryHeadOriginReady &&
        f.bdaqModelStatus == QStringLiteral("EPOCH_BRIDGED_UNCALIBRATED") &&
        bdaq.quality == QStringLiteral("INTERPOLATED") &&
        row.bdaqHead.quality == QStringLiteral("RADAR_MOTION_DISTANCE_UNCALIBRATED") &&
        row.bdaqHead.valid()) {
        // Independent conservative physical gate: first motion + configured D.
        // Do not assume first radar motion is the LB2 edge.
        thicknessOriginPosition = row.bdaqHead.laserHeadMm;
        thicknessOriginQuality =
            QStringLiteral("RADAR_FIRST_MOTION_MINUS_DISTANCE_UNCALIBRATED");
    } else if (m_laserHeadFrameId && f.frameId >= m_laserHeadFrameId) {
        thicknessOriginQuality = QStringLiteral("GEOMETRIC_HEAD_NOT_VALID");
    }
    if (m_joinFile) {
    m_joinStream << f.frameId << ',' << revision << ','
                 << f.daqCallbackUs << ',' << QString::number(f.bdaqTimestamp, 'g', 17) << ','
                 << f.bdaqEstimatedHostUs << ',' << f.bdaqPeriodUs << ','
                 << f.bdaqCallbackLagUs << ',' << f.bdaqModelStatus << ','
                 << fmt(callback.relativeMm) << ','
                 << (row.hasPosition ? callback.quality : QStringLiteral("NO_MATCH")) << ','
                 << fmt(bdaq.relativeMm) << ','
                 << (row.hasPosition ? bdaq.quality : QStringLiteral("NO_MATCH")) << ','
                 << fmt(delta) << ',' << fmt(row.thicknessMm) << ','
                 << fmt(row.snrDb) << ',' << (row.valid ? 1 : 0) << ','
                 << (row.backfilled ? 1 : 0)
                 << ",RELATIVE_RADAR_FIRST_PACKET_UNCALIBRATED,"
                 << f.daqCallbackUnixUs << ',' << f.callbackClockPairSpreadUs << ','
                 << fmt(row.callbackHead.laserHeadMm) << ','
                 << fmt(row.bdaqHead.laserHeadMm) << ',' << row.bdaqHead.quality
                 << QStringLiteral(",LB1_0_RADAR_%1_LB2_%2_LASER_%3_MM_HEAD_PROXY,")
                        .arg(RadarLaserGeometry::Lb1ToRadarMm, 0, 'f', 0)
                        .arg(RadarLaserGeometry::Lb1ToRadarMm +
                             RadarLaserGeometry::RadarToLb2Mm, 0, 'f', 0)
                        .arg(RadarLaserGeometry::Lb1ToRadarMm +
                             m_headTracker.radarToLaserDistanceMm(), 0, 'f', 1)
                 << fmt(thicknessOriginPosition) << ',' << m_laserHeadFrameId
                 << ',' << thicknessOriginQuality
                 << ",RADAR_FIRST_EFFECTIVE_MOTION_UNVERIFIED," << m_headTracker.radarToLaserDistanceMm()
                 << ',' << m_headTracker.lb2ToLaserMm() << ','
                 << (row.joinFailure.isEmpty() ? QStringLiteral("JOIN_OK") : row.joinFailure)
                 << '\n';
    }
    // Commit the resolved radar-position identity and any T2 revision to
    // the same tail record. Never duplicate an ultrasonic Frame ID.
    rememberTailThickness(row, std::isfinite(thicknessOriginPosition));
    // Exactly the live-plot acceptance condition; no fake missing positions.
    // Buffer the newest revision per Frame ID within the existing 256-frame
    // horizon, so the exported CSV has one final row per plotted Frame ID.
    if (row.valid && std::isfinite(row.thicknessMm) &&
        std::isfinite(thicknessOriginPosition)) {
        // Bounded source history for end-of-pipe interpolation, independent
        // of the optional CSV checkbox. Preserve revisions by real Frame ID.
        auto realIt = std::find_if(m_recentRealAxialPoints.begin(),
                                  m_recentRealAxialPoints.end(),
                                  [id = f.frameId](const auto &p) {
                                      return p.frameId == id;
                                  });
        AxialTailFill::RealPoint real{
            f.frameId, thicknessOriginPosition, row.thicknessMm, row.snrDb};
        if (realIt != m_recentRealAxialPoints.end())
            *realIt = real;
        else if (m_recentRealAxialPoints.empty() ||
                 m_recentRealAxialPoints.back().frameId < f.frameId) {
            m_recentRealAxialPoints.push_back(real);
            while (m_recentRealAxialPoints.size() > 64)
                m_recentRealAxialPoints.pop_front();
        }
        if (m_axialFile) {
            AxialCsvRow csv;
            csv.positionMm = thicknessOriginPosition;
            csv.thicknessMm = row.thicknessMm;
            csv.snrDb = row.snrDb;
            csv.bdaqStartTime = f.bdaqTimestamp;
            csv.revision = revision;
            csv.backfilled = row.backfilled;
            csv.bdaqStatus = f.bdaqModelStatus;
            csv.positionQuality = thicknessOriginQuality;
            m_axialPendingRows.insert_or_assign(f.frameId, std::move(csv));
            // Output is deferred until revision retention ends, so each
            // Frame ID appears ONCE with its latest validated thickness.
        }
        if (!m_pipeCompleted)
            emit axialPointReady(f.frameId, thicknessOriginPosition,
                                 row.thicknessMm, thicknessOriginQuality);
    }
}

void RadarPositionWorker::flushAxialRowsThrough(quint64 lastFrameId)
{
    if (!m_axialFile)
        return;
    while (!m_axialPendingRows.empty() &&
           m_axialPendingRows.begin()->first <= lastFrameId) {
        auto it = m_axialPendingRows.begin();
        const quint64 id = it->first;
        const auto &r = it->second;
        const auto fmt = [](double x) {
            return std::isfinite(x) ? QString::number(x, 'f', 6) : QString();
        };
        m_axialStream << id << ',' << fmt(r.positionMm) << ','
                      << fmt(r.thicknessMm) << ',' << fmt(r.snrDb) << ','
                      << r.revision << ',' << (r.backfilled ? 1 : 0) << ','
                      << QString::number(r.bdaqStartTime, 'g', 17) << ','
                      << r.bdaqStatus << ',' << r.positionQuality
                      << ",RADAR_FIRST_EFFECTIVE_MOTION_UNVERIFIED,"
                      << m_headTracker.radarToLaserDistanceMm() << ','
                      << m_headTracker.lb2ToLaserMm()
                      << ",POINT,,,,,,,,\n";
        ++m_axialWritten;
        if (m_axialWritten % 100 == 0)
            m_axialStream.flush();
        m_axialPendingRows.erase(it);
    }
}

void RadarPositionWorker::writeFinalRadarUdpSummary()
{
    if (!m_axialFile)
        return;
    // This SUMMARY row has exactly the same number of columns as ordinary
    // POINT rows, with empty x/y/thickness fields; Excel/chart readers can
    // filter record_type==POINT without fabricating a tail measurement.
    QStringList fields(21, QString());
    fields[12] = QStringLiteral("RADAR_FINAL_UDP");
    if (!m_haveRadarUdp) {
        fields[20] = QStringLiteral("NO_UDP_RECEIVED");
    } else {
        fields[13] = QString::number(m_finalRadarLengthRaw);
        fields[14] = QString::number(double(m_finalRadarLengthRaw) *
                                      PipeHeadAxialTracker::LengthMmPerCount, 'f', 6);
        fields[16] = QString::number(m_firstRadarLengthRaw);
        fields[17] = QString::number(m_finalRadarHostReceiveUs);
        fields[18] = QString::number(m_finalRadarGoodRaw);
        fields[19] = QString::number(m_finalRadarSpeedRaw);
        if (m_finalRadarLengthRaw >= m_firstRadarLengthRaw) {
            fields[15] = QString::number(
                double(quint64(m_finalRadarLengthRaw) - m_firstRadarLengthRaw) *
                PipeHeadAxialTracker::LengthMmPerCount, 'f', 6);
        }
        const qint64 ageUs = RadarHostClock::nowUs() - m_finalRadarHostReceiveUs;
        // A packet may be received after motion stops and still carry the
        // frozen cumulative length. Age and GoodReading are not tail sensors.
        if (m_radarQueueDrops)
            fields[20] = QStringLiteral("QUEUE_OVERFLOW_UNRELIABLE");
        else if (ageUs < 0 || ageUs > 250000)
            fields[20] = QStringLiteral("LAST_UDP_STALE");
        else if (m_finalRadarLengthRaw < m_firstRadarLengthRaw)
            fields[20] = QStringLiteral("LENGTH_COUNTER_REVERSED");
        else if (m_finalRadarGoodRaw == 0)
            fields[20] = QStringLiteral("GOOD_READING_ZERO");
        else
            fields[20] = QStringLiteral("UDP_RECEIVED_UNCALIBRATED");
    }
    m_axialStream << fields.join(',') << '\n';
    m_axialStream.flush();
}

void RadarPositionWorker::finishJoined(bool finalizeAll)
{
    while (!m_joinRows.empty()) {
        auto it = m_joinRows.begin();
        // One thickness-frame delay permits T2 identity confirmation to backfill
        // frame n before n is emitted. Arbitrary older backfills use revisions.
        const bool complete = it->second.hasThickness && it->second.hasPosition;
        const bool twoFramesBeyond = m_latestThicknessId >= it->first &&
            m_latestThicknessId - it->first >= 2;
        const qint64 nowUs = RadarHostClock::nowUs();
        const bool expired = it->second.receivedUs > 0 &&
            nowUs - it->second.receivedUs >= JoinTimeoutUs;
        const bool forced = m_joinRows.size() > MaxJoinRows;
        if (!finalizeAll && !forced && !expired && !(complete && twoFramesBeyond))
            break;

        JoinedRow row = it->second;
        if (!complete) {
            row.joinFailure = forced ? QStringLiteral("JOIN_CAPACITY_LIMIT")
                : (expired ? QStringLiteral("JOIN_TIMEOUT_300MS")
                           : QStringLiteral("JOIN_STOP_INCOMPLETE"));
            if (!row.hasPosition) {
                row.callbackMatch.quality = row.joinFailure + QStringLiteral("_NO_POSITION");
                row.bdaqMatch.quality = row.joinFailure + QStringLiteral("_NO_POSITION");
            }
            if (!row.hasThickness) {
                row.thicknessMm = std::numeric_limits<double>::quiet_NaN();
                row.valid = false;
            }
            if (forced) ++m_joinForced;
            else ++m_joinTimedOut;
            const quint64 failures = m_joinForced + m_joinTimedOut;
            if (failures == 1 || failures % 100 == 0)
                emit statusMessage(QStringLiteral(
                    "Frame ID关联异常：超时=%1，容量结算=%2，保留厚度且位置缺失标记")
                    .arg(m_joinTimedOut).arg(m_joinForced));
        }
        writeJoined(row, QStringLiteral("FRAME"));
        m_writtenRows.insert_or_assign(it->first, row);
        while (m_writtenRows.size() > 256) {
            flushAxialRowsThrough(m_writtenRows.begin()->first);
            m_writtenRows.erase(m_writtenRows.begin());
        }
        m_lastFinalizedFrameId = (std::max)(m_lastFinalizedFrameId, it->first);
        m_joinRows.erase(it);
    }
    if (m_joinFile && (finalizeAll || (m_completed > 0 && m_completed % 100 == 0)))
        m_joinStream.flush();
    if (m_axialFile && finalizeAll) {
        flushAxialRowsThrough(std::numeric_limits<quint64>::max());
        m_axialStream.flush();
    }
}

void RadarPositionWorker::stop()
{
    if (!m_started) {
        if (m_receiver) {
            delete m_receiver;
            m_receiver = nullptr;
        }
        if (m_csvFile) {
            m_csvStream.setDevice(nullptr);
            m_csvFile->close();
            delete m_csvFile;
            m_csvFile = nullptr;
        }
        if (m_axialFile) {
            m_axialStream.setDevice(nullptr);
            m_axialFile->close();
            delete m_axialFile;
            m_axialFile = nullptr;
        }
        if (m_joinFile) {
            m_joinStream.setDevice(nullptr);
            m_joinFile->close();
            delete m_joinFile;
            m_joinFile = nullptr;
        }
        return;
    }
    if (m_expiryTimer) {
        m_expiryTimer->stop();
        delete m_expiryTimer;
        m_expiryTimer = nullptr;
    }
    if (m_receiver) {
        m_receiver->stop();
        delete m_receiver;
        m_receiver = nullptr;
    }
    // Force all outstanding frames to get an explicit no-bracket status.
    m_matcher.resolve(RadarHostClock::nowUs() + RadarFrameMatcher::FrameTimeoutUs + 1,
                      [this](const RadarFrameMatcher::Match &m) { save(m); });
    finishJoined(true);
    // A late batch of already-acquired frames may complete the sliding
    // 10-frame NaN vote during shutdown, but Stop itself is NOT evidence of a
    // pipe tail. Never generate a speculative tail from partial acquisition.
    tryCompletePipe();
    const double distanceMm = m_headTracker.radarToLaserDistanceMm();
    if (!m_pipeCompleted)
        m_tailFillDecision = QStringLiteral(
            "尚未确认管尾：须雷达停止有效输出、仍有有效厚度，最近10帧内至少5个NaN且至少3帧连续；停止采集不触发整管补偿重绘");
    if (m_joinFile) {
        m_joinStream.flush();
        m_joinStream.setDevice(nullptr);
        m_joinFile->close();
        delete m_joinFile;
        m_joinFile = nullptr;
    }
    if (m_csvFile) {
        m_csvStream.flush();
        m_csvStream.setDevice(nullptr);
        m_csvFile->close();
        delete m_csvFile;
        m_csvFile = nullptr;
    }
    if (m_axialFile) {
        // Thickness and Frame ID are ACTUALLY measured after radar loss.
        // Only axial x is estimated, so this remains a separate orange series.
        for (const auto &p : m_tailFillPoints) {
            QStringList cols(21, QString());
            cols[0] = QString::number(p.frameId);
            cols[1] = QString::number(p.xMm, 'f', 6);
            cols[2] = QString::number(p.thicknessMm, 'f', 6);
            cols[3] = QString::number(p.snrDb, 'f', 6);
            cols[4] = QStringLiteral("POST_RADAR_LOSS");
            cols[5] = p.backfilled ? QStringLiteral("1") : QStringLiteral("0");
            cols[8] = QStringLiteral("TAIL_POSITION_ESTIMATED_THICKNESS_MEASURED");
            cols[9] = QStringLiteral("POST_RADAR_LOSS_EVEN_SPACING");
            cols[10] = QString::number(distanceMm, 'f', 1);
            cols[12] = QStringLiteral("TAIL_ESTIMATED");
            m_axialStream << cols.join(',') << '\n';
        }
        writeFinalRadarUdpSummary();
        m_axialStream.setDevice(nullptr);
        m_axialFile->close();
        delete m_axialFile;
        m_axialFile = nullptr;
    }
    emit statusMessage(QStringLiteral("轴向实时关联完成：%1帧，有效雷达匹配%2帧；轴向数据CSV %3，已写入%4条")
                       .arg(m_completed).arg(m_valid)
                       .arg(m_saveCsv ? m_csvPath : QStringLiteral("关闭"))
                       .arg(m_axialWritten));
    m_started = false;
}

RadarPositionSidecar::RadarPositionSidecar(QObject *parent)
    : QObject(parent)
{
}

RadarPositionSidecar::~RadarPositionSidecar()
{
    stopSession();
}

void RadarPositionSidecar::setSharedRadarSource(RadarUdpSharedService *source)
{
    if (m_active || m_sharedRadarSource == source)
        return;
    if (m_sharedRadarSource)
        disconnect(m_sharedRadarSource, nullptr, this, nullptr);
    m_sharedRadarSource = source;
    if (source) {
        // The radar service emits in its worker thread. Enqueue under a
        // short mutex rather than posting one unbounded Qt event per packet.
        connect(source, &RadarUdpSharedService::sampleReceived, this,
                [this](const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs) {
            MailEvent event;
            event.kind = MailEvent::Kind::Radar;
            event.radar = packet;
            event.radarUs = hostSteadyUs;
            enqueueEvent(std::move(event));
        }, Qt::DirectConnection);
    }
}

bool RadarPositionSidecar::startSession(bool saveCsv, double radarToLaserDistanceMm)
{
    if (m_active)
        return true;
    if (!std::isfinite(radarToLaserDistanceMm) ||
        radarToLaserDistanceMm <= PipeHeadAxialTracker::RadarToLb2Mm ||
        radarToLaserDistanceMm > RadarLaserGeometry::MaximumRadarToLaserMm)
        return false;
    m_sessionRadarToLaserMm = radarToLaserDistanceMm;
    m_outputPath.clear();
    if (saveCsv) {
        QDir root(QDir::currentPath());
        if (!root.mkpath(QStringLiteral("AxialThicknessData"))) {
            emit statusMessage(QStringLiteral("无法创建轴向壁厚CSV文件夹"));
            return false;
        }
        const QString name = QStringLiteral("AxialThickness_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
        m_outputPath = root.filePath(QStringLiteral("AxialThicknessData/") + name);
    }

    const quint64 generation = ++m_sessionGeneration;
    m_completionDelivered = false;
    m_worker = new RadarPositionWorker(m_outputPath, m_sharedRadarSource != nullptr,
                                        saveCsv, RadarPositionWorker::CsvMode::AxialThickness);
    m_worker->setRadarToLaserDistanceMm(m_sessionRadarToLaserMm);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    // Old worker notifications can remain in the GUI event queue after stop.
    // Generation-gate them so prior pipe Frame IDs cannot reappear following
    // a display reset or the next acquisition's Frame ID counter restart.
    connect(m_worker, &RadarPositionWorker::statusMessage, this,
            [this, generation](const QString &message) {
                if (generation == m_sessionGeneration)
                    emit statusMessage(message);
            }, Qt::QueuedConnection);
    connect(m_worker, &RadarPositionWorker::axialPointReady, this,
            [this, generation](quint64 frameId, double positionMm, double thicknessMm,
                               const QString &quality) {
                if (generation == m_sessionGeneration)
                    emit axialPointReady(frameId, positionMm, thicknessMm, quality);
            }, Qt::QueuedConnection);
    connect(m_worker, &RadarPositionWorker::headStatusChanged, this,
            [this, generation](const QString &status) {
                if (generation == m_sessionGeneration)
                    emit headStatusChanged(status);
            }, Qt::QueuedConnection);
    connect(m_worker, &RadarPositionWorker::pipeCompleted, this,
            [this, generation, worker = m_worker](int count, double length,
                                                   const QString &decision) {
                if (generation != m_sessionGeneration || m_completionDelivered)
                    return;
                std::vector<AxialTailFill::EstimatedPoint> completedTail;
                QMetaObject::invokeMethod(worker, [worker, &completedTail]() {
                    completedTail = worker->tailFillPoints();
                }, Qt::BlockingQueuedConnection);
                m_completionDelivered = true;
                for (const auto &p : completedTail)
                    emit axialTailPointReady(p.xMm, p.thicknessMm);
                emit tailFillFinished(count, length, decision);
            }, Qt::QueuedConnection);
    connect(&m_thread, &QThread::started, m_worker, &RadarPositionWorker::start);
    {
        std::lock_guard<std::mutex> guard(m_mailMutex);
        m_mailbox.clear();
        m_radarQueued = m_metadataQueued = 0;
        m_peakRadarQueued = m_peakMetadataQueued = 0;
        m_totalRadarDropped = m_totalMetadataDropped = 0;
        m_radarDroppedPending = m_metadataDroppedPending = 0;
        m_drainPosted = false;
        m_mailOpen = true;
        m_radarStopCutoffUs.store(0, std::memory_order_release);
    }
    m_thread.start();
    m_active = true;
    return true;
}

void RadarPositionSidecar::submitFrame(quint64 frameId, qint64 callbackSteadyUs,
                                        double bdaqTimestamp, qint64 callbackUnixUs,
                                        qint64 clockPairSpreadUs)
{
    if (!m_active || !m_worker)
        return;
    RadarFrameMatcher::Frame frame;
    frame.frameId = frameId;
    frame.daqCallbackUs = callbackSteadyUs;
    frame.bdaqTimestamp = bdaqTimestamp;
    frame.daqCallbackUnixUs = callbackUnixUs;
    frame.callbackClockPairSpreadUs = clockPairSpreadUs;
    MailEvent event;
    event.kind = MailEvent::Kind::Frame;
    event.frame = frame;
    enqueueEvent(std::move(event));
}

void RadarPositionSidecar::submitThickness(quint64 frameId, double thicknessMm,
                                            double snrDb, bool valid)
{
    if (!m_active || !m_worker)
        return;
    MailEvent event;
    event.kind = MailEvent::Kind::Thickness;
    event.frameId = frameId;
    event.thickness = thicknessMm;
    event.snr = snrDb;
    event.valid = valid;
    enqueueEvent(std::move(event));
}

void RadarPositionSidecar::confirmLaserHeadFrame(quint64 firstStableFrameId)
{
    if (!m_active || !m_worker || !firstStableFrameId)
        return;
    MailEvent event;
    event.kind = MailEvent::Kind::Head;
    event.frameId = firstStableFrameId;
    enqueueEvent(std::move(event));
}

void RadarPositionSidecar::submitBackfill(quint64 targetFrameId, double thicknessMm,
                                          double snrDb)
{
    if (!m_active || !m_worker)
        return;
    MailEvent event;
    event.kind = MailEvent::Kind::Backfill;
    event.frameId = targetFrameId;
    event.thickness = thicknessMm;
    event.snr = snrDb;
    enqueueEvent(std::move(event));
}

RadarPositionSidecar::MailboxStats RadarPositionSidecar::mailboxStats() const
{
    std::lock_guard<std::mutex> guard(m_mailMutex);
    return {m_radarQueued, m_metadataQueued, m_peakRadarQueued,
            m_peakMetadataQueued, m_totalRadarDropped, m_totalMetadataDropped};
}

void RadarPositionSidecar::enqueueEvent(MailEvent event)
{
    std::lock_guard<std::mutex> guard(m_mailMutex);
    if (!m_mailOpen || !m_worker)
        return;
    if (event.kind == MailEvent::Kind::Radar) {
        const qint64 cutoff = m_radarStopCutoffUs.load(std::memory_order_acquire);
        if (cutoff > 0 && event.radarUs > cutoff)
            return; // after actual DAQ Stop(), not part of this pipe's length
    }

    if (event.kind == MailEvent::Kind::Radar) {
        if (m_radarQueued == MaxRadarQueued) {
            // Never block the UDP receiver. Drop oldest UNPROCESSED radar
            // sample, invalidate continuous axial position, count the loss.
            const auto it = std::find_if(m_mailbox.begin(), m_mailbox.end(),
                [](const MailEvent &item) {
                    return item.kind == MailEvent::Kind::Radar;
                });
            if (it != m_mailbox.end()) {
                m_mailbox.erase(it);
                --m_radarQueued;
                ++m_radarDroppedPending;
                ++m_totalRadarDropped;
            }
        }
        ++m_radarQueued;
        m_peakRadarQueued = (std::max)(m_peakRadarQueued, m_radarQueued);
    } else {
        const bool critical = event.kind == MailEvent::Kind::Head ||
                              event.kind == MailEvent::Kind::Backfill;
        // Reserve four positions for T2 two-frame backfills and laser-head
        // confirmation. They must not be starved by ordinary Frame events.
        if (m_metadataQueued >= (critical ? MaxMetadataQueued
                                          : MaxMetadataQueued - 4)) {
            if (critical && m_metadataQueued == MaxMetadataQueued) {
                const auto it = std::find_if(m_mailbox.begin(), m_mailbox.end(),
                    [](const MailEvent &item) {
                    return item.kind == MailEvent::Kind::Frame ||
                           item.kind == MailEvent::Kind::Thickness;
                });
                if (it != m_mailbox.end()) {
                    m_mailbox.erase(it);
                    --m_metadataQueued;
                    ++m_metadataDroppedPending;
                    ++m_totalMetadataDropped;
                }
            }
            if (m_metadataQueued >= (critical ? MaxMetadataQueued
                                              : MaxMetadataQueued - 4)) {
                // Sidecar metadata ONLY: core DAQ thickness is unaffected.
                ++m_metadataDroppedPending;
                ++m_totalMetadataDropped;
                return;
            }
        }
        ++m_metadataQueued;
        m_peakMetadataQueued = (std::max)(m_peakMetadataQueued, m_metadataQueued);
    }
    m_mailbox.push_back(std::move(event));
    if (!m_drainPosted) {
        m_drainPosted = true;
        RadarPositionWorker *worker = m_worker;
        QMetaObject::invokeMethod(worker, [this, worker]() {
            drainMailbox(worker);
        }, Qt::QueuedConnection);
    }
}

void RadarPositionSidecar::drainMailbox(RadarPositionWorker *worker)
{
    // Runs only on the location worker's Qt thread. At most one queued drain
    // wakeup exists. A live batch yields back to the Qt event loop; stop
    // switches to full drain for deterministic CSV/Frame ID finalization.
    for (;;) {
        std::deque<MailEvent> batch;
        quint64 radarLost = 0, metadataLost = 0;
        bool closed = false;
        {
            std::lock_guard<std::mutex> guard(m_mailMutex);
            closed = !m_mailOpen;
            const size_t take = (std::min)(DrainBatch, m_mailbox.size());
            for (size_t i = 0; i < take; ++i) {
                MailEvent &event = m_mailbox.front();
                if (event.kind == MailEvent::Kind::Radar)
                    --m_radarQueued;
                else
                    --m_metadataQueued;
                batch.push_back(std::move(event));
                m_mailbox.pop_front();
            }
            radarLost = m_radarDroppedPending;
            metadataLost = m_metadataDroppedPending;
            m_radarDroppedPending = m_metadataDroppedPending = 0;
        }
        if (radarLost)
            worker->noteRadarQueueOverflow(radarLost);
        if (metadataLost)
            worker->noteMetadataQueueOverflow(metadataLost);
        for (const auto &event : batch) {
            switch (event.kind) {
            case MailEvent::Kind::Radar:
                worker->addRadarSample(event.radar, event.radarUs);
                break;
            case MailEvent::Kind::Frame:
                worker->addFrame(event.frame);
                break;
            case MailEvent::Kind::Thickness:
                worker->addThickness(event.frameId, event.thickness,
                                     event.snr, event.valid);
                break;
            case MailEvent::Kind::Backfill:
                worker->addBackfill(event.frameId, event.thickness, event.snr);
                break;
            case MailEvent::Kind::Head:
                worker->setLaserHeadFrame(event.frameId);
                break;
            }
        }
        {
            std::lock_guard<std::mutex> guard(m_mailMutex);
            closed = !m_mailOpen;
            if (m_mailbox.empty()) {
                m_drainPosted = false;
                return;
            }
            if (!closed) {
                QMetaObject::invokeMethod(worker, [this, worker]() {
                    drainMailbox(worker);
                }, Qt::QueuedConnection);
                return;
            }
        }
        // Closing: consume everything on this thread instead of posting
        // additional events behind the stop command.
    }
}

void RadarPositionSidecar::markMeasurementStopped()
{
    if (!m_active)
        return;
    const qint64 now = RadarHostClock::nowUs();
    qint64 expected = 0;
    m_radarStopCutoffUs.compare_exchange_strong(
        expected, now, std::memory_order_acq_rel);
}

void RadarPositionSidecar::stopSession()
{
    if (!m_active)
        return;
    markMeasurementStopped();
    // Invalidate queued notifications immediately; last-run tail must never
    // be delivered as the next pipe's new axial position measurements.
    ++m_sessionGeneration;
    {
        // Stop accepting producer events BEFORE draining or destroying worker.
        std::lock_guard<std::mutex> guard(m_mailMutex);
        m_mailOpen = false;
    }
    std::deque<AxialTailFill::RealPoint> finalReal;
    std::vector<AxialTailFill::EstimatedPoint> finalTail;
    QString finalTailDecision;
    bool confirmedComplete = false;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker,
            [this, worker = m_worker, &finalReal, &finalTail, &finalTailDecision,
             &confirmedComplete]() {
                drainMailbox(worker); // drain in FIFO order
                worker->stop();      // finalize measured points, then CSV tail
                finalReal = worker->recentRealAxialPoints();
                finalTail = worker->tailFillPoints();
                finalTailDecision = worker->tailFillDecision();
                confirmedComplete = worker->hasCompletedPipe();
            }, Qt::BlockingQueuedConnection);
    }
    // If the pipe was completed during acquisition, its final curve and
    // statistics are frozen.  Stop only closes the worker and CSV, without
    // replaying a second completion or admitting later noise.
    if (!m_completionDelivered && confirmedComplete) {
        for (const auto &p : finalReal)
            emit axialPointReady(p.frameId, p.xMm, p.thicknessMm,
                                 QStringLiteral("RADAR_FIRST_MOTION_MINUS_DISTANCE_UNCALIBRATED"));
        for (const auto &p : finalTail)
            emit axialTailPointReady(p.xMm, p.thicknessMm);
        m_completionDelivered = true;
        emit tailFillFinished(static_cast<int>(finalTail.size()),
                              m_sessionRadarToLaserMm, finalTailDecision);
    }
    m_thread.quit();
    m_thread.wait();
    m_worker = nullptr;
    m_active = false;
}
