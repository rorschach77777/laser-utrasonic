#pragma once

#include "T2FilteredPeakRefinement.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <deque>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

class SecondEchoParabolicTracker
{
public:
    enum class Stage { GlobalSearch, Warmup, Locked };
    enum class FilteredPeakMode { Integer, Parabolic };

    struct Config {
        int startupConfirmations = 2;
        int startupWindowFrames = 3;
        double startupConsistencySamples = 12.0;
        int predictionHalfWidth = 16;
        int maxMissesBeforeGlobal = 3;
        // LOST后短时间保留冻结T2身份用于重捕获；若连续长时间仍无法恢复，
        // 说明已进入新的采集段，必须放弃旧位置/模板并重新冷启动。
        // 100 Hz下30帧约0.3 s，明显长于普通3帧失锁门，同时不会把旧身份永久保留。
        int frozenIdentityColdRestartMisses = 30;
        bool independentLostAgeEnabled = false; // A/B: 未发布年龄，不受WARMUP候选命中影响
        int afterColdRestartConfirmations = 3; // 冷启动放弃冻结模板后，需3帧同一候选族确认
        double strongPeriodResidualSamples = 4.0;
        double acceptablePeriodResidualSamples = 10.0;
        double minCandidateSnrDb = 4.5;
        // 当前帧已有强DeltaT/T2-T3物理身份佐证时，允许把T2候选SNR下限降到
        // 单帧候选生成下限附近；身份由独立period保证，不靠低SNR峰本身建锁。
        double strongPeriodWeakCandidateSnrDb = 3.0;
        double lockedWeakCandidateSnrDb = 2.5;
        // 独立、仅LOCK可用的低SNR局部候选。不会进入GLOBAL/WARMUP，
        // 弱发布绝不污染模板/运动历史，且仍须通过严格NCC和物理身份门。
        bool weakLockRecoveryEnabled = false;
        double weakLockMinSnrDb = 1.5;
        double weakLockMinCorrelation = 0.92;
        double weakLockMaxResidualSamples = 4.0;
        int weakLockMinHistory = 4;
        // 只供离线调查连续失锁使用；默认只对首个MISS开放弱发布。
        int weakLockMaxPreviousMisses = 0;
        double lockMinTemplateCorrelation = 0.82;
        double reacquireMinTemplateCorrelation = 0.78;
        double templateUpdateMinCorrelation = 0.88;
        double uncorroboratedCertificationCorrelation = 0.90;
        // 无DeltaT时仅在“非常接近预测中心”情况下允许模板相关轻微低于普通LOCK门，
        // 避免真实T2因单帧波形形变造成一个点的空洞；不能用于大幅重定位。
        double uncorroboratedNearPredictionResidualSamples = 2.0;
        double uncorroboratedNearPredictionMinCorrelation = 0.78;
        double uncorroboratedNearPredictionMinSnrDb = 6.0;
        // 单模板瞬时形变时的两条受限旁证通道：
        // 1) 唯一近预测候选 + 中等以上模板相关；
        // 2) 极近预测 + Formal T1+参考周期也落在同一位置，即使模板形态明显变化也可延续。
        double uncorroboratedAmbiguityGapSamples = 8.0;
        double uncorroboratedUniqueResidualSamples = 8.0;
        double uncorroboratedUniqueNoReferenceResidualSamples = 4.0;
        double uncorroboratedUniqueMinCorrelation = 0.75;
        double uncorroboratedUniqueMinSnrDb = 8.0;
        double uncorroboratedUniqueReferenceResidualSamples = 16.0;
        double uncorroboratedDualAnchorResidualSamples = 2.0;
        double uncorroboratedDualAnchorReferenceResidualSamples = 4.0;
        double uncorroboratedDualAnchorMinCorrelation = 0.30;
        double uncorroboratedDualAnchorMinSnrDb = 6.0;
        double referencePeriodResidualSamples = 48.0;
        double templateEmaAlpha = 0.20;
        double uncorroboratedRecenterThresholdSamples = 6.0;
        // 已稳定LOCK、模板高度一致且位移很小时，可把小幅recenter当作连续T2运动直接发布；
        // 更大的无佐证跳变仍保留两帧确认，防止跳到相邻packet。
        double uncorroboratedDirectRecenterMaxSamples = 8.0;
        double uncorroboratedDirectRecenterMinCorrelation = 0.95;
        double uncorroboratedDirectRecenterMinSnrDb = 6.0;
        int uncorroboratedDirectRecenterMinHistory = 4;
        // Thick-wall non-high-precision PARABOLIC LOCK only: extend the
        // previous 8-sample last-accepted gate up to 10 samples ONLY with
        // extra-strong shape agreement. Do not
        // self-certify ordinary 8..10-sample morphology changes: prematurely
        // learning one (Frame_0384) can make the next real peak go missing.
        // The <=12 predicted-residual and >=8 dB guards remain unchanged.
        double thickDirectRecenterLastAcceptedGateSamples = 10.0;
        double thickDirectRecenterExtendedMinCorrelation = 0.94;
        // A large TOF step can be physical when consecutive axial sample
        // positions are far apart. This additional high-confidence channel
        // only PUBLISHES an isolated measurement; it never changes predictor,
        // formally accepted history, templates, or the pending recenter vote.
        // Missing axial motion information is NOT interpreted as >30 mm.
        double highConfidenceJumpAxialSpacingMm = 30.0;
        double highConfidenceJumpMinSnrDb = 15.0;
        double highConfidenceJumpMinCorrelation = 0.87;
        double highConfidenceJumpMaxPredResidual = 12.0;
        double highConfidenceJumpMaxLastResidual = 12.0;
        double highConfidenceJumpMaxCrestEnvelopeGap = 6.0;
        double highConfidenceJumpMinCompetitorGap = 8.0;
        double highConfidenceJumpKnownSpacingMinSnrDb = 13.0;
        double highConfidenceJumpKnownSpacingMaxResidual = 16.0;
        double uncorroboratedRecenterConsistencySamples = 4.0;
        int uncorroboratedRecenterConfirmFrames = 2;
        // Historical single-peak thresholds retained for Config compatibility;
        // preemptive single-peak substitution is no longer executed. The
        // physical/global reacquisition still uses thickSinglePeakMinSnrDb.
        double thickSinglePeakMaxResidualSamples = 10.0;
        double thickSinglePeakMinSnrDb = 7.0;
        // Shape-switching packets in moving 20 mm pipe are frequently 5-7 dB.
        // Admit them only when history is stable and the position is tighter.
        double thickSinglePeakSoftMinSnrDb = 4.5;
        double thickSinglePeakSoftMaxResidualSamples = 7.0;
        int thickSinglePeakSoftMinHistory = 4;
        // The median-velocity predictor can overshoot during pipe motion.
        // A single strong peak near the last published TOF remains the same
        // physical packet even when it is outside the instantaneous predictor.
        double thickLastAnchorMaxResidualSamples = 5.0;
        double thickLastAnchorMaxPredictionResidualSamples = 24.0;
        double thickLastAnchorMinSnrDb = 4.0;
        double thickLastAnchorExtendedResidualSamples = 9.5;
        double thickSinglePeakMinimumCompetitorGapSamples = 6.0;
        // Final-only publication shadow, called AFTER the full primary result.
        // No forced candidate competition, template update, motion-history
        // learning, premature startup vote or LOCK reacquisition is allowed.
        // Shared thick/thin fallback uses frame-local NET packet SNR,
        // +/-32 prediction and residual-adaptive identity evidence.
        bool thickFinalSinglePeakFallbackEnabled = true;
        // Opt-in for thin-wall T2-only; other modes are unaffected.
        bool thinFinalSinglePeakFallbackEnabled = false;
        // The final fallback's net packet-SNR and adaptive identity checks
        // below are shared by thick and thin T2-only. Second-echo ordinal
        // evidence continues to be handled by the primary tracker.
        double thickAmbiguousMinSnrDb = 1.3;
        double thickAmbiguousPredictionGateSamples = 32.0;
        double thickAmbiguousCompetitorGateSamples = 32.0;
        // Uncertified one-peak publications in LOCK must also agree with the
        // last *accepted* TOF. Frame_0327 has a real 10.14dB peak 11.48
        // samples away from both prediction and last accepted identity; the
        // old 10-sample history gate was the actual blocker, not pred +/-16.
        // This is publication-only and does NOT move the accepted identity.
        double thickShadowLastAcceptedGateSamples = 16.0;
        // Weak genuine packets cannot be decided from energy SNR alone.
        // Allow tightly tracked high-shape-identity packets at lower SNR,
        // but quarantine long jumps whose waveform identity is weak.
        double thickWeakIdentityCorr = 0.94;
        double thickWeakIdentityMaxPredictionResidual = 4.0;
        double thickWeakIdentityMinSnrDb = -6.0;
        double thickLongJumpResidualSamples = 8.0;
        // A strong, well-measured waveform is allowed a short trend-model
        // overshoot if it stays close to the last formally accepted TOF.
        double thickLongJumpStrongExcessSnrDb = 8.0;
        double thickLongJumpStrongLastResidualSamples = 8.0;
        double thickLongJumpMinIdentityCorr = 0.85;
        // Formal T2-only tracking: after a miss, do not instantly re-learn
        // a low-SNR, marginal-template result with no ordinal evidence.
        double thickPostGapLowIdentitySnrDb = 6.0;
        double thickPostGapLowIdentityCorr = 0.87;
        double thickStartupSinglePeakMinSnrDb = 11.0;
        double thickStartupLastTofGateSamples = 8.0;
        double thickReacquireMaxDriftSamples = 48.0;
    };

    struct Candidate {
        double tofSamples = nan();
        double snrDb = nan();
        double score = 0.0;
        int rank = -1;
        std::vector<double> envelopeTemplate;
        int envelopePeakIndex = -1; // actual Hilbert envelope maximum, NOT t50
    };

    struct Frame {
        std::vector<Candidate> candidates;
        std::vector<Candidate> weakLockCandidates; // 单帧3dB以下独立候选，不参与GLOBAL
        // Raw smoothed T2 envelope and per-local-maximum SNR from the waveform.
        // These samples are independent of formal/weak candidate selection,
        // SNR prefilter, peak separation and the Top-8 budget. The terminal
        // single-peak fallback scans them within its own prediction window.
        int directEnvelopeFirstIndex = -1;
        int directSupportLeft = -1;
        int directSupportRight = -1;
        int directT50HalfWidth = 12;
        // Only PARABOLIC mode broadens the positive-crest search around
        // the matched envelope peak. Keep Integer unchanged.
        // A/B: 12 = old baseline; default 16; tested up to 20.
        int parabolicPositivePeakSearchHalfWidth = 16;
        std::vector<double> directEnvelope;
        std::vector<double> directLocalMaxSnrDb; // Legacy/global SNR diagnostic only.
        // Per-frame filtered waveform in the same ROI. Fallback SNR is
        // recalculated in its prediction window using these values alone.
        std::vector<double> directFilteredSignal;
        int directEnergyHalfWidth = 8;
        // When true, candidates and prediction/history are all in FILTERED
        // positive integer-crest coordinates, including direct fallback.
        // Do not mix t50-candidate gates with integer position history.
        bool integerPeakTrackingEnabled = false;
        FilteredPeakMode filteredPeakMode = FilteredPeakMode::Integer;
        double referenceTofSamples = nan();
        // Optional measured axial distance from previous 100-Hz sample (mm).
        // It must come from synchronized speed/position data, not TOF itself.
        double axialStepMm = nan();
        bool thickWallRangeOnly = false; // explicitly select first physical echo

        bool periodEvidenceAvailable = false;
        double formalT1TofSamples = nan();
        double periodSamples = nan();
        // 当高质量DeltaT不可用时，允许用“Formal T1 + 产品参考厚度对应的参考周期”
        // 作为较弱的ordinal佐证。它只用于身份，不进入T2厚度公式。
        double referencePeriodSamples = nan();

        bool t2T3EvidenceAvailable = false;
        double t3TofSamples = nan();
    };

    struct Decision {
        bool candidateFound = false;
        bool available = false;
        bool certified = false;
        // Once the ordinary second-frame vote establishes cold-start LOCK,
        // permit publishing the preceding frame's OWN measured TOF.
        bool hasStartupBackfill = false;
        int startupBackfillFramesAgo = 0;
        double startupBackfillTofSamples = nan();
        double startupBackfillSnrDb = nan();
        double startupBackfillPeakVoltage = nan();
        bool deltaTStrongSupport = false;
        bool deltaTAcceptableSupport = false;
        bool t2T3Support = false;
        bool referenceT1Support = false;

        Stage stage = Stage::GlobalSearch;
        double tofSamples = nan();
        double selectedSnrDb = nan();
        double selectedScore = nan();
        int selectedRank = -1;
        int selectedEnvelopePeakIndex = -1; // matched integer peak for output only
        double predictedTofSamples = nan();
        double periodResidualSamples = nan();
        double t2T3ResidualSamples = nan();
        double templateCorrelation = nan();
        int rawCandidateCount = 0;
        double nearestPredictionResidualSamples = nan();
        double secondNearestPredictionResidualSamples = nan();
        int predictionNeighborhoodCandidateCount = 0;
        double nearestPredictionCorrelation = nan();
        double nearestPredictionSnrDb = nan();
        const char *thickFallbackAudit = "NOT_THICK";
        // Diagnostic only: raw waveform peak position, per-packet robust-mean
        // SNR, extracted packet edges, and balanced background counts.
        std::string thickFallbackPacketSnrTrace;
        double thickFallbackSelectedTof = nan();
        double thickFallbackSelectedSnr = nan();
        double thickFallbackPredictionResidual = nan();
        double thickFallbackLastResidual = nan();
        double nearestPeriodAnchorResidualSamples = nan();
        double nearestReferenceAnchorResidualSamples = nan();
        int startupVotes = 0;
        int consecutiveMisses = 0;
        int weakCandidatesConsidered = 0;
        double weakNearestCorrelation = nan();
        double weakNearestSnrDb = nan();
        int weakRawCount = 0;
        int weakNear4Count = 0;
        int weakNear8Count = 0;
        double weakBestNear4Correlation = nan();
        double weakBestNear4SnrDb = nan();
        double weakNearestResidualSamples = nan();
        int lockHistoryCount = 0;
        int lockMissesBefore = 0;
        int unpublishedAgeFrames = 0;
        bool independentColdRestarted = false;
        // 仅诊断：GLOBAL/WARMUP候选剔除是旧冻结模板、参考位置还是当前ordinal证据。
        const char *globalRejectPolicy = "NONE";
        bool globalReferenceGuardRejected = false;
        double globalReferenceGuardRejectedTof = nan();
        double globalFrozenTofSamples = nan();
        int globalStrongCandidateCount = 0;
        int globalGateCandidateCount = 0;
        int globalShapePassCandidateCount = 0;
        double globalBestGateCorrelation = nan();
        double globalNearestFrozenDistance = nan();
        const char *reason = "NO_T2_CANDIDATE";
    };

    // Locator for the matched packet's true integer carrier crest.
    // Hilbert envelope maximum alone is not the positive filtered maximum.
    // Used only for final output in the old mode; when integer tracking is
    // explicitly selected it also defines candidate/history/prediction
    // coordinates. Template shapes and noise measurements remain unchanged.
    static int matchedFilteredPositivePeakIndex(
        const Frame &f, int envelopePeakIndex,
        bool outputOnlyParabolicWindow = false)
    {
        if (envelopePeakIndex < 0 || f.directEnvelopeFirstIndex < 0
            || f.directFilteredSignal.empty())
            return -1;
        const int first = f.directEnvelopeFirstIndex;
        const int last = first + static_cast<int>(f.directFilteredSignal.size()) - 1;
        // Parabolic A/B: widen only the matched carrier-crest search.
        // Identity candidate ROI and predictor/NCC gates are unchanged.
        // Integer remains at the original <=12-sample search.
        const int half = (outputOnlyParabolicWindow
            || (f.integerPeakTrackingEnabled
                && f.filteredPeakMode == FilteredPeakMode::Parabolic))
            ? std::clamp(f.parabolicPositivePeakSearchHalfWidth, 6, 24)
            : std::clamp(f.directT50HalfWidth, 6, 12);
        const int left = std::max({first, f.directSupportLeft,
                                   envelopePeakIndex - half});
        const int right = std::min({last, f.directSupportRight,
                                    envelopePeakIndex + half});
        if (left > right)
            return -1;
        int selected = -1;
        double highest = -std::numeric_limits<double>::infinity();
        for (int i = left; i <= right; ++i) {
            const double v = f.directFilteredSignal[static_cast<size_t>(i - first)];
            if (std::isfinite(v) && v > highest) {
                highest = v;
                selected = i;
            }
        }
        return selected;
    }

    // The same measured positive carrier crest defines candidate TOF,
    // LOCK history/prediction, and final fallback in every non-t50 mode.
    // Missing interpolation is an INVALID observation, never a silent
    // integer/t50 fallback that would mix predictor coordinate systems.
    static double matchedFilteredPeakTofSamples(
        const Frame& f, int envelopePeakIndex)
    {
        const int peak = matchedFilteredPositivePeakIndex(f, envelopePeakIndex);
        if (peak < 0) return nan();
        if (f.filteredPeakMode == FilteredPeakMode::Integer)
            return static_cast<double>(peak);
        // No Sinc12/Sinc24 work: candidate identity and prediction both
        // use the same three-point parabolic crest position.
        return T2FilteredPeakRefinement::analyzeParabolic(
            f.directFilteredSignal, f.directEnvelopeFirstIndex, peak,
            f.directSupportLeft, f.directSupportRight).parabolicIndex;
    }

    SecondEchoParabolicTracker() = default;
    explicit SecondEchoParabolicTracker(const Config &cfg) : m_cfg(cfg) {}

    // Main program uses the same thin-wall fallback switch as its existing
    // t50 tracker. No reset or history change when the mode is selected.
    void setThinFinalSinglePeakFallbackEnabled(bool enabled)
    {
        m_cfg.thinFinalSinglePeakFallbackEnabled = enabled;
    }

    void reset()
    {
        m_stage = Stage::GlobalSearch;
        m_history.clear();
        m_pendingCenters.clear();
        m_frameClock = 0;
        m_pendingStartupClock = 0;
        m_pendingStartupTof = nan();
        m_pendingStartupSnr = nan();
        m_pendingStartupVoltage = nan();
        m_frozenTofSamples = nan();
        m_fallbackPredictionTof = nan();
        m_template.clear();
        m_misses = 0;
        m_recoveryMisses = 0;
        m_unpublishedAge = 0;
        m_afterColdRestart = false;
        m_recenterPendingTof = nan();
        m_recenterPendingCount = 0;
        m_framesSincePublication = 0;
        m_lastPublishedTof = nan();
        m_thickWallRangeOnly = false;
        m_recentTemplate.clear();
        m_alternateTemplate.clear();
    }

    // Final publication-only fallback: the ordinary algorithm always runs
    // first and owns *all* identity, history, recenter and template changes.
    // The fallback may fill only an otherwise unavailable thickness result.
    Decision process(const Frame &f)
    {
        const Stage stageBefore = m_stage;
        const double anchorBefore = stageBefore == Stage::Locked
            ? predictedTof() : m_frozenTofSamples;
        const double lastAccepted = !m_history.empty()
            ? m_history.back() : nan();
        Decision out = processPrimary(f);
        // A genuinely abrupt thickness change is possible if successive
        // positions are far apart. Distinguish strong, phase-aligned measured
        // crests from false carrier identities. An unsupported single-frame
        // jump may be displayed with uncertified quality, but its normal
        // recenter vote and frozen trusted prediction remain untouched.
        if (!out.available && stageBefore == Stage::Locked
            && std::string_view(out.reason) == "T2_LOCK_RECENTER_PENDING"
            && f.thickWallRangeOnly && f.integerPeakTrackingEnabled
            && f.filteredPeakMode == FilteredPeakMode::Parabolic
            && std::isfinite(lastAccepted) && std::isfinite(anchorBefore)
            && out.lockHistoryCount >= 4 && std::isfinite(out.tofSamples)) {
            const bool spaced = std::isfinite(f.axialStepMm)
                && f.axialStepMm >= m_cfg.highConfidenceJumpAxialSpacingMm;
            const double predLimit = spaced
                ? m_cfg.highConfidenceJumpKnownSpacingMaxResidual
                : m_cfg.highConfidenceJumpMaxPredResidual;
            const double lastLimit = spaced
                ? m_cfg.highConfidenceJumpKnownSpacingMaxResidual
                : m_cfg.highConfidenceJumpMaxLastResidual;
            const double predResidual = std::abs(out.tofSamples - anchorBefore);
            const double lastResidual = std::abs(out.tofSamples - lastAccepted);
            const bool unique = !std::isfinite(out.secondNearestPredictionResidualSamples)
                || (out.secondNearestPredictionResidualSamples
                    - out.nearestPredictionResidualSamples)
                    >= m_cfg.highConfidenceJumpMinCompetitorGap;
            for (const auto &c : f.candidates) {
                if (c.rank != out.selectedRank
                    || std::abs(c.tofSamples - out.tofSamples) > 1e-5)
                    continue;
                const double crestGap = c.envelopePeakIndex >= 0
                    ? std::abs(out.tofSamples - c.envelopePeakIndex)
                    : std::numeric_limits<double>::infinity();
                const bool highConfidence = std::isfinite(out.selectedSnrDb)
                    && out.selectedSnrDb >= (spaced
                        ? m_cfg.highConfidenceJumpKnownSpacingMinSnrDb
                        : m_cfg.highConfidenceJumpMinSnrDb)
                    && std::isfinite(out.templateCorrelation)
                    && out.templateCorrelation >= m_cfg.highConfidenceJumpMinCorrelation
                    && predResidual <= predLimit && lastResidual <= lastLimit
                    && crestGap <= m_cfg.highConfidenceJumpMaxCrestEnvelopeGap
                    && unique;
                if (highConfidence) {
                    out.available = true;
                    out.certified = false;
                    out.selectedEnvelopePeakIndex = c.envelopePeakIndex;
                    out.thickFallbackAudit = spaced
                        ? "HIGH_CONF_SPACED_JUMP_NO_PREDICTION_UPDATE"
                        : "HIGH_CONF_JUMP_NO_PREDICTION_UPDATE";
                    out.reason = "T2_LOCK_HIGH_CONF_JUMP_SHADOW";
                }
                break;
            }
        }
        if (out.available && std::isfinite(out.tofSamples)) {
            // The template tracker has already identified the packet using
            // t50. Capture ONLY that matched packet's integer envelope max.
            // Neither the state nor learned history is modified by the
            // caller's choice of output locator.
            for (const auto &c : f.candidates) {
                if (c.envelopePeakIndex >= 0
                    && std::abs(c.tofSamples - out.tofSamples) < 1e-5
                    && c.rank == out.selectedRank) {
                    out.selectedEnvelopePeakIndex = c.envelopePeakIndex;
                    break;
                }
            }
            if (out.selectedEnvelopePeakIndex < 0) {
                for (const auto &c : f.weakLockCandidates) {
                    if (c.envelopePeakIndex >= 0
                        && std::abs(c.tofSamples - out.tofSamples) < 1e-5) {
                        out.selectedEnvelopePeakIndex = c.envelopePeakIndex;
                        break;
                    }
                }
            }
        }
        const bool singlePeakFallbackEnabled = f.thickWallRangeOnly
            ? m_cfg.thickFinalSinglePeakFallbackEnabled
            : m_cfg.thinFinalSinglePeakFallbackEnabled;
        if (!singlePeakFallbackEnabled || out.available)
            return out;
        if (!std::isfinite(anchorBefore)) {
            out.thickFallbackAudit = "FINAL_NO_TRUSTED_PREDICTION";
            return out;
        }
        if (f.directEnvelope.size() < 3
            || f.directEnvelope.size() != f.directFilteredSignal.size()
            || f.directEnvelopeFirstIndex < 0) {
            out.thickFallbackAudit = "FINAL_NO_DIRECT_ENVELOPE";
            return out;
        }

        // Cold restart explicitly revokes an old identity: do not resurrect it
        // using a stale frozen TOF. Neither cold start nor untrusted warmup
        // without a frozen anchor permits a guessed fallback position.
        if (std::string_view(out.reason) == "T2_LONG_GAP_COLD_RESTART") {
            out.thickFallbackAudit = "FINAL_COLD_RESTART";
            return out;
        }
        // The formal tracker isolated a low-identity observation after a
        // miss. Do not rescue that SAME observation by an untrusted fallback.
        if (std::string_view(out.reason) == "T2_LOCK_POST_GAP_LOW_IDENTITY_PENDING") {
            out.thickFallbackAudit = "FINAL_PRIMARY_POST_GAP_ISOLATED";
            return out;
        }

        Candidate directMeasuredPeak;
        if (!findDirectMeasuredSinglePeak(f, anchorBefore, directMeasuredPeak,
                                          out.thickFallbackPacketSnrTrace)) {
            out.thickFallbackAudit = "FINAL_NO_LOCAL_WINDOW_SNR1P3_PEAK";
            return out;
        }
        const Candidate *candidate = &directMeasuredPeak;
        out.thickFallbackSelectedTof = candidate->tofSamples;
        out.thickFallbackSelectedSnr = candidate->snrDb;
        out.thickFallbackPredictionResidual = std::abs(
            candidate->tofSamples - anchorBefore);
        if (std::isfinite(lastAccepted))
            out.thickFallbackLastResidual = std::abs(
                candidate->tofSamples - lastAccepted);

        // The LOCK velocity prediction can temporarily overshoot. Require
        // corroboration from the last ACCEPTED primary TOF, not from a
        // previously emitted uncertified fallback.
        if (stageBefore == Stage::Locked) {
            if (!std::isfinite(lastAccepted)) {
                out.thickFallbackAudit = "FINAL_MISSING_LAST_ACCEPTED";
                return out;
            }
            if (std::abs(candidate->tofSamples - lastAccepted)
                    > m_cfg.thickShadowLastAcceptedGateSamples) {
                out.thickFallbackAudit = "FINAL_LAST_TOF_GT16";

                return out;
            }
        }

        // A series of provisional peaks can straddle opposite sides of the
        // last FORMALLY accepted TOF. Both pass the 16-sample historical gate
        // yet differ by >16 from each other, creating a false one-frame jump.
        // Keep the expanded +/-32 search, but require continuity between
        // consecutive successful fallback positions as well.
        if (std::isfinite(m_fallbackPredictionTof)
            && std::abs(candidate->tofSamples - m_fallbackPredictionTof)
                > m_cfg.thickShadowLastAcceptedGateSamples) {
            out.thickFallbackAudit = "FINAL_PREVIOUS_FALLBACK_TOF_GT16";
            return out;
        }

        // Both thin- and thick-wall T2-only use the SAME packet-local net
        // SNR and residual-adaptive shape-identity rules. No extra thin-only
        // blanket NCC floor: stable weak echoes can have changed morphology.
        const double fallbackCorr = candidateTemplateCorrelation(*candidate);
        // A weak integer-crest candidate can appear close to the new
        // predictor simply because an unrelated carrier crest is far from
        // its envelope center. Restrict ONLY these suspect candidates, not
        // every legitimate morphology change: either correlation is very
        // poor, or both shape and crest-to-envelope phase identity disagree.
        // Keep high-precision, strong-signal and coherent weak fallback
        // unchanged.
        const double crestEnvelopeSeparation =
            candidate->envelopePeakIndex >= 0
                ? std::abs(candidate->tofSamples
                    - static_cast<double>(candidate->envelopePeakIndex))
                : std::numeric_limits<double>::infinity();
        // Audit the *measured* packet chosen by the direct fallback, not just
        // the nearest formal/Top-K NCC. This diagnostic never affects gates.
        if (f.integerPeakTrackingEnabled) {
            out.thickFallbackPacketSnrTrace +=
                "|FINAL_IDENTITY:NCC=" + std::to_string(fallbackCorr)
                + ":CREST_ENVELOPE_GAP=" + std::to_string(crestEnvelopeSeparation)
                + ":ENVELOPE_PEAK=" + std::to_string(candidate->envelopePeakIndex)
                + ":CREST_TOF=" + std::to_string(candidate->tofSamples);
        }
        // Frame_0284-type narrowly continuous weak packet: the independently
        // measured fallback crest is <4 samples from prediction, <2 samples
        // from the last formally accepted TOF and <4 from its envelope peak.
        // A template score of 0.549 (just below the old 0.55 hard floor) may
        // be published as an UNLEARNED fallback, never primary-certified.
        // Do not generalize to long jumps, separated carrier cycles, weaker
        // packets, thin walls, integer/Sinc or the high-precision t50 path.
        const bool continuousMarginalWeakParabola = f.thickWallRangeOnly
            && f.integerPeakTrackingEnabled
            && f.filteredPeakMode == FilteredPeakMode::Parabolic
            && std::isfinite(lastAccepted)
            && std::abs(candidate->tofSamples - anchorBefore) <= 4.0
            && std::abs(candidate->tofSamples - lastAccepted) <= 2.0
            && std::isfinite(candidate->snrDb) && candidate->snrDb >= 3.0
            && std::isfinite(fallbackCorr) && fallbackCorr >= 0.54
            && fallbackCorr < 0.55
            && crestEnvelopeSeparation <= 4.0;
        if (f.integerPeakTrackingEnabled
            && candidate->snrDb < m_cfg.thickPostGapLowIdentitySnrDb
            && (!std::isfinite(fallbackCorr) || fallbackCorr < 0.75)
            && ((!std::isfinite(fallbackCorr) || fallbackCorr < 0.55)
                || crestEnvelopeSeparation > 7.0)
            && !continuousMarginalWeakParabola) {
            out.thickFallbackAudit = "FINAL_INTEGER_WEAK_PACKET_IDENTITY_PENDING";
            return out;
        }
        // As a peak drifts 8+ samples from the trusted prediction, local
        // energy SNR is insufficient evidence of physical packet identity.
        // Shape consistency becomes necessary ONLY for such long jumps;
        // ordinary small-drift and very weak but strongly matched echoes
        // remain publishable. No fixed NCC gate is imposed on all peaks.
        const bool strongNearLast = std::isfinite(candidate->snrDb)
            && candidate->snrDb >= m_cfg.thickLongJumpStrongExcessSnrDb
            && std::isfinite(lastAccepted)
            && std::abs(candidate->tofSamples - lastAccepted)
                <= m_cfg.thickLongJumpStrongLastResidualSamples;
        if (std::abs(candidate->tofSamples - anchorBefore)
                > m_cfg.thickLongJumpResidualSamples
            && (!std::isfinite(fallbackCorr)
                || fallbackCorr < m_cfg.thickLongJumpMinIdentityCorr)
            && !strongNearLast) {
            out.thickFallbackAudit = "FINAL_LONG_JUMP_WEAK_IDENTITY_PENDING";
            return out;
        }

        // Do not let a low-NCC, low-SNR provisional crest jump >8 samples
        // from the last accepted physical identity and drag the predictor to
        // an unrelated carrier packet. Such a single-frame jump is untrusted
        // until a later current-frame observation independently confirms it.
        // It remains a missing frame, not an alleged thickness measurement.
        if (f.thickWallRangeOnly && f.integerPeakTrackingEnabled
            && f.filteredPeakMode == FilteredPeakMode::Parabolic
            && std::isfinite(fallbackCorr) && fallbackCorr < 0.75
            && std::isfinite(candidate->snrDb)
            && candidate->snrDb >= 5.0 && candidate->snrDb < 8.0
            && std::isfinite(lastAccepted)
            && std::abs(candidate->tofSamples - lastAccepted) > 8.0
            && std::abs(candidate->tofSamples - anchorBefore) > 6.0) {
            out.thickFallbackAudit = "FINAL_UNVERIFIED_JUMP_NO_PREDICTION_UPDATE";
            return out;
        }

        // A successfully measured fallback becomes the NEXT predicted peak.
        // Keep its position separate from m_history (formally accepted TOFs);
        // NEVER learn from its envelope or add startup votes. Primary
        // selection and recenter confirmation have already run.
        // Only this marginal 0.54..0.55 NCC exception is output-only: do not
        // replace the predictor, last-accepted identity, template, or the
        // miss/recenter state left by processPrimary(). Frame_0285's remote
        // +13-sample crest must therefore not inherit a weak Frame_0284
        // prediction, and Frame_0286 may still reacquire from trusted history.
        if (!continuousMarginalWeakParabola) {
            m_fallbackPredictionTof = candidate->tofSamples;
            if (m_stage != Stage::Locked)
                m_frozenTofSamples = candidate->tofSamples;
            else
                m_misses = 0; // The ordinary fallback is predictor-qualified.
            m_framesSincePublication = 0;
            m_unpublishedAge = 0;
        }
        out.stage = m_stage;
        out.consecutiveMisses = m_stage == Stage::Locked
            ? m_misses : m_recoveryMisses;
        out.candidateFound = true;
        out.available = true;
        out.certified = false;
        out.tofSamples = candidate->tofSamples;
        out.selectedSnrDb = candidate->snrDb;
        out.selectedScore = candidate->score;
        out.selectedRank = candidate->rank;
        out.selectedEnvelopePeakIndex = candidate->envelopePeakIndex;
        out.templateCorrelation = fallbackCorr;
        out.thickFallbackSelectedTof = candidate->tofSamples;
        out.thickFallbackSelectedSnr = candidate->snrDb;
        out.thickFallbackPredictionResidual = std::abs(
            candidate->tofSamples - anchorBefore);
        if (std::isfinite(lastAccepted))
            out.thickFallbackLastResidual = std::abs(
                candidate->tofSamples - lastAccepted);
        out.thickFallbackAudit = continuousMarginalWeakParabola
            ? "POST_PRIMARY_MARGINAL_WEAK_PUBLISHED_NO_PREDICTION_UPDATE"
            : (stageBefore == Stage::Locked
                ? "POST_PRIMARY_LOCK_PRED_UPDATED_NO_TEMPLATE"
                : "POST_PRIMARY_LOST_PRED_UPDATED_NO_TEMPLATE");
        out.reason = stageBefore == Stage::Locked
            ? "T2_LOCK_FINAL_SINGLE_PEAK_SHADOW"
            : "T2_LOST_FINAL_SINGLE_PEAK_SHADOW";
        return out;
    }

private:
    Decision processPrimary(const Frame &f)
    {
        Decision out;
        ++m_frameClock;
        if (m_framesSincePublication < std::numeric_limits<int>::max())
            ++m_framesSincePublication;
        m_thickWallRangeOnly = f.thickWallRangeOnly;
        // pending只是候选假设，不等于已恢复有效测量。
        if (m_unpublishedAge < std::numeric_limits<int>::max())
            ++m_unpublishedAge;
        if (m_cfg.independentLostAgeEnabled && m_stage != Stage::Locked
            && std::isfinite(m_frozenTofSamples)
            && m_unpublishedAge >= std::max(1, m_cfg.frozenIdentityColdRestartMisses)) {
            m_stage = Stage::GlobalSearch;
            m_history.clear();
            m_pendingCenters.clear();
            m_frozenTofSamples = nan();
            m_fallbackPredictionTof = nan();
            m_template.clear();
            m_misses = 0;
            m_recoveryMisses = 0;
            m_recenterPendingTof = nan();
            m_recenterPendingCount = 0;
            m_afterColdRestart = true;
            out.independentColdRestarted = true;
        }
        out.unpublishedAgeFrames = m_unpublishedAge;
        out.stage = m_stage;
        // LOST no longer has a velocity history. Retain only the frozen,
        // previously learned identity as its prediction reference; never use
        // a newly detected ambiguous peak to train that reference.
        out.predictedTofSamples = f.thickWallRangeOnly && m_stage != Stage::Locked
            && std::isfinite(m_frozenTofSamples)
            ? m_frozenTofSamples : predictedTof();
        out.rawCandidateCount = static_cast<int>(f.candidates.size());
        out.lockHistoryCount = static_cast<int>(m_history.size());
        out.lockMissesBefore = m_misses;
        out.weakRawCount = static_cast<int>(f.weakLockCandidates.size());
        if (std::isfinite(out.predictedTofSamples)) {
            for (const auto &wc : f.weakLockCandidates) {
                if (!std::isfinite(wc.tofSamples)) continue;
                const double distance = std::abs(wc.tofSamples - out.predictedTofSamples);
                if (!std::isfinite(out.weakNearestResidualSamples)
                    || distance < out.weakNearestResidualSamples)
                    out.weakNearestResidualSamples = distance;
                if (distance <= 8.0) ++out.weakNear8Count;
                if (distance <= 4.0) {
                    ++out.weakNear4Count;
                    const double corr = candidateTemplateCorrelation(wc);
                    if (std::isfinite(corr)
                        && (!std::isfinite(out.weakBestNear4Correlation)
                            || corr > out.weakBestNear4Correlation)) {
                        out.weakBestNear4Correlation = corr;
                        out.weakBestNear4SnrDb = wc.snrDb;
                    }
                }
            }
        }
        for (const auto &candidate : f.candidates) {
            if (!std::isfinite(candidate.tofSamples))
                continue;
            if (std::isfinite(out.predictedTofSamples)) {
                const double r = std::abs(candidate.tofSamples - out.predictedTofSamples);
                if (r <= std::max(1, m_cfg.predictionHalfWidth))
                    ++out.predictionNeighborhoodCandidateCount;
                if (!std::isfinite(out.nearestPredictionResidualSamples)
                    || r < out.nearestPredictionResidualSamples) {
                    out.secondNearestPredictionResidualSamples = out.nearestPredictionResidualSamples;
                    out.nearestPredictionResidualSamples = r;
                    out.nearestPredictionCorrelation = candidateTemplateCorrelation(candidate);
                    out.nearestPredictionSnrDb = candidate.snrDb;
                } else if (!std::isfinite(out.secondNearestPredictionResidualSamples)
                           || r < out.secondNearestPredictionResidualSamples) {
                    out.secondNearestPredictionResidualSamples = r;
                }
            }
            if (std::isfinite(f.formalT1TofSamples)
                && std::isfinite(f.referencePeriodSamples)) {
                const double r = std::abs(candidate.tofSamples
                    - (f.formalT1TofSamples + f.referencePeriodSamples));
                if (!std::isfinite(out.nearestReferenceAnchorResidualSamples)
                    || r < out.nearestReferenceAnchorResidualSamples)
                    out.nearestReferenceAnchorResidualSamples = r;
            }
            if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                && std::isfinite(f.periodSamples)) {
                const double r = std::abs(candidate.tofSamples
                    - (f.formalT1TofSamples + f.periodSamples));
                if (!std::isfinite(out.nearestPeriodAnchorResidualSamples)
                    || r < out.nearestPeriodAnchorResidualSamples)
                    out.nearestPeriodAnchorResidualSamples = r;
            }
        }

        // LOCK阶段只有“当前真实周期”证据可以打断时序预测重新做全局候选竞争。
        // Formal T1 + reference period 只是弱身份先验，只用于GLOBAL/WARMUP/重捕获，
        // 不能让已经LOCK的T2每帧在大窗口内重新跳峰。
        const bool currentOrdinalEvidence = !f.thickWallRangeOnly && (
            (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
             && std::isfinite(f.periodSamples))
            || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                && std::isfinite(f.periodSamples)));

        const Candidate *selected = nullptr;
        bool directSelfCertifiedRecenter = false;
        if (m_stage == Stage::Locked && currentOrdinalEvidence) {
            // LOCK也服从当前物理ordinal证据：有DeltaT/T2-T3证据时，优先选与序列一致的
            // T2候选，防止预测链在弱信号段沿着错误近邻packet自洽漂移。
            selected = chooseGlobalCandidate(f);
        } else if (m_stage == Stage::Locked && std::isfinite(out.predictedTofSamples)) {
            static constexpr int expand[] = {1, 2, 4};
            const int factor = expand[std::clamp(m_misses, 0, 2)];
            const double gate = factor * std::max(1, m_cfg.predictionHalfWidth);
            selected = chooseLockedCandidate(f, out.predictedTofSamples, gate);
        } else {
            selected = chooseGlobalCandidate(f);
        }

        if (f.thickWallRangeOnly)
            out.thickFallbackAudit = "DEFERRED_TO_FINAL_PUBLICATION";

        // 没有任何序列佐证时，连续跟踪只能在参考T2附近的身份区内延续。
        // 超出该区说明旧预测可能已沿着近邻packet漂移；此时必须LOST等待period/重新认证，
        // 不能把“大搜索窗允许发现”误解成“任何大窗峰都属于同一T2”。
        constexpr double uncorroboratedIdentityGateSamples = 128.0;
        if (selected && !currentOrdinalEvidence && std::isfinite(f.referenceTofSamples)
            && std::abs(selected->tofSamples - f.referenceTofSamples)
                > uncorroboratedIdentityGateSamples) {
            // The startup nominal/reference anchor is not a physical upper
            // limit. After prolonged stable LOCK, a genuine thick-wall echo
            // may cross its +/-128 gate. Allow ONLY a near-prediction,
            // high-NCC, strong packet near the last formally accepted TOF;
            // never extend GLOBAL/WARMUP or accept an untrusted far island.
            const double shapeCorr = candidateTemplateCorrelation(*selected);
            const bool continuousLockedPacket = f.thickWallRangeOnly
                && f.integerPeakTrackingEnabled
                && f.filteredPeakMode == FilteredPeakMode::Parabolic
                && m_stage == Stage::Locked
                && m_history.size() >= 4
                && std::isfinite(out.predictedTofSamples)
                && std::abs(selected->tofSamples - out.predictedTofSamples) <= 8.0
                && !m_history.empty()
                && std::abs(selected->tofSamples - m_history.back()) <= 10.0
                && std::isfinite(shapeCorr) && shapeCorr >= 0.94
                && std::isfinite(selected->snrDb) && selected->snrDb >= 8.0;
            if (!continuousLockedPacket) {
                out.globalReferenceGuardRejected = true;
                out.globalReferenceGuardRejectedTof = selected->tofSamples;
                selected = nullptr;
            }
        }

        if (!selected && (m_stage == Stage::GlobalSearch || m_stage == Stage::Warmup)) {
            // 与chooseGlobalCandidate()门限相同的只读审计，不放行任何额外候选。
            const bool hasOrdinalEvidence =
                (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                 && std::isfinite(f.periodSamples))
                || (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.referencePeriodSamples))
                || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                    && std::isfinite(f.periodSamples));
            out.globalFrozenTofSamples = m_frozenTofSamples;
            out.globalRejectPolicy = hasOrdinalEvidence ? "ORDINAL_EVIDENCE"
                : (std::isfinite(m_frozenTofSamples) ? "FROZEN_IDENTITY"
                   : "REFERENCE_STARTUP");
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                    || c.snrDb < m_cfg.minCandidateSnrDb)
                    continue;
                ++out.globalStrongCandidateCount;
                if (hasOrdinalEvidence) {
                    const bool supported =
                        (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                            && std::isfinite(f.periodSamples)
                            && std::abs(c.tofSamples - f.formalT1TofSamples - f.periodSamples)
                                <= m_cfg.acceptablePeriodResidualSamples)
                        || (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                            && std::isfinite(f.referencePeriodSamples)
                            && std::abs(c.tofSamples - f.formalT1TofSamples - f.referencePeriodSamples)
                                <= m_cfg.referencePeriodResidualSamples)
                        || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                            && std::isfinite(f.periodSamples)
                            && std::abs(f.t3TofSamples - c.tofSamples - f.periodSamples)
                                <= m_cfg.acceptablePeriodResidualSamples);
                    if (supported)
                        ++out.globalGateCandidateCount;
                } else if (std::isfinite(m_frozenTofSamples)) {
                    const double delta = std::abs(c.tofSamples - m_frozenTofSamples);
                    if (!std::isfinite(out.globalNearestFrozenDistance)
                        || delta < out.globalNearestFrozenDistance)
                        out.globalNearestFrozenDistance = delta;
                    if (delta > 128.0)
                        continue;
                    ++out.globalGateCandidateCount;
                    const double corr = candidateTemplateCorrelation(c);
                    if (std::isfinite(corr)
                        && (!std::isfinite(out.globalBestGateCorrelation)
                            || corr > out.globalBestGateCorrelation))
                        out.globalBestGateCorrelation = corr;
                    if (m_template.empty() ||
                        (std::isfinite(corr) && corr >= m_cfg.reacquireMinTemplateCorrelation))
                        ++out.globalShapePassCandidateCount;
                } else if (std::isfinite(f.referenceTofSamples)
                    && std::abs(c.tofSamples - f.referenceTofSamples) <= 128.0) {
                    ++out.globalGateCandidateCount;
                    ++out.globalShapePassCandidateCount;
                } else if (!std::isfinite(f.referenceTofSamples)) {
                    ++out.globalGateCandidateCount;
                    ++out.globalShapePassCandidateCount;
                }
            }
        }

        if (!selected) {
            // 仅稳定LOCK阶段：单帧全局3dB门下的弱候选也能利用已学到的T2
            // 包络身份。只允许预测附近、高NCC且与当前period不冲突的候选。
            // 输出采用publication-only：内部仍按miss推进，绝不把弱候选
            // 塞入运动历史或更新模板，防止多帧噪声自我强化、错误锁定。
            const Candidate *weak = nullptr;
            double bestDistance = std::numeric_limits<double>::infinity();
            if (m_cfg.weakLockRecoveryEnabled && m_stage == Stage::Locked
                && m_misses <= m_cfg.weakLockMaxPreviousMisses
                && static_cast<int>(m_history.size()) >= m_cfg.weakLockMinHistory
                && std::isfinite(out.predictedTofSamples) && !m_template.empty()) {
                for (const auto &c : f.weakLockCandidates) {
                    ++out.weakCandidatesConsidered;
                    if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                        || c.snrDb < m_cfg.weakLockMinSnrDb)
                        continue;
                    const double delta = std::abs(c.tofSamples - out.predictedTofSamples);
                    if (delta > m_cfg.weakLockMaxResidualSamples)
                        continue;
                    const double corr = candidateTemplateCorrelation(c);
                    if (!std::isfinite(corr))
                        continue;
                    if (!std::isfinite(out.weakNearestCorrelation)
                        || delta < bestDistance) {
                        out.weakNearestCorrelation = corr;
                        out.weakNearestSnrDb = c.snrDb;
                    }
                    if (corr < m_cfg.weakLockMinCorrelation)
                        continue;
                    if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                        && std::isfinite(f.periodSamples)
                        && std::abs(c.tofSamples - f.formalT1TofSamples - f.periodSamples)
                            > m_cfg.acceptablePeriodResidualSamples)
                        continue;
                    if (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                        && std::isfinite(f.periodSamples)
                        && std::abs(f.t3TofSamples - c.tofSamples - f.periodSamples)
                            > m_cfg.acceptablePeriodResidualSamples)
                        continue;
                    if (!weak || delta < bestDistance) {
                        weak = &c;
                        bestDistance = delta;
                    }
                }
            }
            if (weak) {
                out.candidateFound = true;
                out.tofSamples = weak->tofSamples;
                out.selectedSnrDb = weak->snrDb;
                out.selectedScore = weak->score;
                out.selectedRank = weak->rank;
                out.templateCorrelation = candidateTemplateCorrelation(*weak);
                applyEvidence(f, out);
                // 必须有高模板NCC，且弱候选不会认证为高置信身份。
                // 真实弱回波仅作为隔离输出（非标定模板学习源）。
                const bool cold = observeMiss();
                out.stage = m_stage;
                out.consecutiveMisses = m_stage == Stage::Locked ? m_misses : m_recoveryMisses;
                out.available = !cold;
                out.certified = false;
                out.reason = cold ? "T2_LONG_GAP_COLD_RESTART" : "T2_LOCK_WEAK_SHADOW_PUBLISHED";
                return out;
            }
            const Stage stageBeforeMiss = m_stage;
            const char *missReason = "T2_GLOBAL_CANDIDATE_MISSING";
            if (stageBeforeMiss == Stage::Locked) {
                if (f.candidates.empty()) {
                    missReason = "T2_LOCK_NO_RAW_CANDIDATE";
                } else if (currentOrdinalEvidence) {
                    if (f.periodEvidenceAvailable
                        && std::isfinite(out.nearestPeriodAnchorResidualSamples)
                        && out.nearestPeriodAnchorResidualSamples
                            > m_cfg.acceptablePeriodResidualSamples) {
                        missReason = "T2_LOCK_PERIOD_ANCHOR_CONFLICT";
                    } else {
                        missReason = "T2_LOCK_ORDINAL_EVIDENCE_NO_MATCH";
                    }
                } else if (std::isfinite(out.predictedTofSamples)) {
                    static constexpr int expand[] = {1, 2, 4};
                    const int factor = expand[std::clamp(m_misses, 0, 2)];
                    const double gate = factor * std::max(1, m_cfg.predictionHalfWidth);
                    if (!std::isfinite(out.nearestPredictionResidualSamples)
                        || out.nearestPredictionResidualSamples > gate) {
                        missReason = "T2_LOCK_NO_PREDICTION_GATE_MATCH";
                    } else {
                        missReason = "T2_LOCK_TEMPLATE_REJECTED";
                    }
                } else {
                    missReason = "T2_LOCK_NO_PREDICTION";
                }
            }
            const bool coldRestarted = observeMiss();
            out.stage = m_stage;
            out.consecutiveMisses = m_stage == Stage::Locked ? m_misses : m_recoveryMisses;
            out.reason = coldRestarted ? "T2_LONG_GAP_COLD_RESTART" : missReason;
            return out;
        }

        // 无当前period时，若候选只能依靠新增的低模板相关旁证通道通过，
        // 将它作为纯发布旁路：输出当前测量，但内部状态严格沿用“本帧miss”的旧路径。
        // 必须在recenterPending逻辑之前返回，避免旁路帧清空/改变任何恢复状态。
        const double preRecenterCorrelation = candidateTemplateCorrelation(*selected);
        const bool shadowPublicationOnly = m_stage == Stage::Locked
            && !currentOrdinalEvidence
            && !m_template.empty()
            && std::isfinite(preRecenterCorrelation)
            && preRecenterCorrelation < m_cfg.lockMinTemplateCorrelation;
        if (shadowPublicationOnly) {
            out.candidateFound = true;
            out.tofSamples = selected->tofSamples;
            out.selectedSnrDb = selected->snrDb;
            out.selectedScore = selected->score;
            out.selectedRank = selected->rank;
            out.templateCorrelation = preRecenterCorrelation;
            applyEvidence(f, out);
            const bool certifiedBeforeMiss = identityCertified(out);
            const bool coldRestarted = observeMiss();
            out.available = true;
            out.certified = certifiedBeforeMiss;
            out.stage = m_stage;
            out.consecutiveMisses = m_stage == Stage::Locked ? m_misses : m_recoveryMisses;
            if (coldRestarted)
                out.certified = false;
            out.reason = "T2_LOCK_SOFT_PUBLICATION_ONLY";
            return out;
        }

        // LOCK中没有当前period佐证时，大幅recenter先做两帧确认，避免单帧跳到同窗
        // 的另一packet后下一帧又返回。真实厚度台阶若持续存在，第二帧即可确认通过。
        if (m_stage == Stage::Locked && !currentOrdinalEvidence
            && std::isfinite(out.predictedTofSamples)
            && std::abs(selected->tofSamples - out.predictedTofSamples)
                > m_cfg.uncorroboratedRecenterThresholdSamples) {
            const double recenterResidual = std::abs(
                selected->tofSamples - out.predictedTofSamples);
            const double recenterCorrelation = candidateTemplateCorrelation(*selected);

            directSelfCertifiedRecenter =
                (f.thickWallRangeOnly
                    && m_history.size() >= 4
                    && recenterResidual <= 12.0
                    && (std::abs(selected->tofSamples - m_history.back()) <= 8.0
                        || (f.integerPeakTrackingEnabled
                            && f.filteredPeakMode == FilteredPeakMode::Parabolic
                            && std::abs(selected->tofSamples - m_history.back())
                                <= m_cfg.thickDirectRecenterLastAcceptedGateSamples
                            && std::isfinite(recenterCorrelation)
                            && recenterCorrelation
                                >= m_cfg.thickDirectRecenterExtendedMinCorrelation))
                    && std::isfinite(recenterCorrelation)
                    && recenterCorrelation >= 0.82
                    && selected->snrDb >= 8.0)
                || static_cast<int>(m_history.size()) >= m_cfg.uncorroboratedDirectRecenterMinHistory
                && recenterResidual <= m_cfg.uncorroboratedDirectRecenterMaxSamples
                && std::isfinite(recenterCorrelation)
                && recenterCorrelation >= m_cfg.uncorroboratedDirectRecenterMinCorrelation
                && std::isfinite(selected->snrDb)
                && selected->snrDb >= m_cfg.uncorroboratedDirectRecenterMinSnrDb;
            if (directSelfCertifiedRecenter) {
                m_recenterPendingTof = nan();
                m_recenterPendingCount = 0;
            } else if (!std::isfinite(m_recenterPendingTof)
                || std::abs(selected->tofSamples - m_recenterPendingTof)
                    > m_cfg.uncorroboratedRecenterConsistencySamples) {
                m_recenterPendingTof = selected->tofSamples;
                m_recenterPendingCount = 1;
                out.candidateFound = true;
                out.tofSamples = selected->tofSamples;
                out.selectedSnrDb = selected->snrDb;
                out.selectedScore = selected->score;
                out.selectedRank = selected->rank;
                out.templateCorrelation = candidateTemplateCorrelation(*selected);
                out.available = false;
                out.certified = false;
                out.reason = "T2_LOCK_RECENTER_PENDING";
                return out;
            }
            if (!directSelfCertifiedRecenter) {
                ++m_recenterPendingCount;
                if (m_recenterPendingCount < m_cfg.uncorroboratedRecenterConfirmFrames) {
                    out.candidateFound = true;
                    out.tofSamples = selected->tofSamples;
                    out.available = false;
                    out.certified = false;
                    out.reason = "T2_LOCK_RECENTER_PENDING";
                    return out;
                }
                m_recenterPendingTof = nan();
                m_recenterPendingCount = 0;
            }
        } else {
            m_recenterPendingTof = nan();
            m_recenterPendingCount = 0;
        }

        out.candidateFound = true;
        out.tofSamples = selected->tofSamples;
        out.selectedSnrDb = selected->snrDb;
        out.selectedScore = selected->score;
        out.selectedRank = selected->rank;
        out.templateCorrelation = candidateTemplateCorrelation(*selected);

        applyEvidence(f, out);

        if (m_stage == Stage::GlobalSearch || m_stage == Stage::Warmup) {
            // 能找到可用候选说明真实信号已重新出现，长空窗计数从这里重新开始；
            // 是否正式建锁仍由下面的period或2-of-3启动确认决定。
            m_recoveryMisses = 0;
            // 当前帧若已有强DeltaT/T2-T3 ordinal证据，候选身份已经被独立序列证据
            // 直接认证，不必再等待2-of-3。注意：证据只决定“这是T2”，厚度仍只用T2 TOF。
            const bool directIdentity = out.deltaTStrongSupport || out.t2T3Support;
            bool confirmed = false;
            if (directIdentity) {
                m_stage = Stage::Locked;
                m_history.clear();
                pushHistory(out.tofSamples);
                m_pendingCenters.clear();
                m_frozenTofSamples = nan();
                m_misses = 0;
                m_recoveryMisses = 0;
                m_afterColdRestart = false;
                confirmed = true;
            } else {
                // Thick wall: a high-SNR isolated peak near the last *published*
                // identity may resume without another pending frame. Cold first
                // startup still requires ordinary two-frame consistency.
                const bool closeToLast = f.thickWallRangeOnly
                    && std::isfinite(m_lastPublishedTof)
                    && std::abs(out.tofSamples - m_lastPublishedTof)
                        <= m_cfg.thickStartupLastTofGateSamples;
                const bool strong = selected->snrDb >= m_cfg.thickStartupSinglePeakMinSnrDb;
                bool unique = true;
                for (const auto &c : f.candidates) {
                    if (&c != selected && std::isfinite(c.tofSamples)
                        && std::abs(c.tofSamples - selected->tofSamples) < 10.0)
                        unique = false;
                }
                if (closeToLast && strong && unique) {
                    m_stage = Stage::Locked;
                    m_history.clear();
                    pushHistory(out.tofSamples);
                    m_pendingCenters.clear();
                    m_frozenTofSamples = nan();
                    m_misses = 0;
                    m_recoveryMisses = 0;
                    m_afterColdRestart = false;
                    confirmed = true;
                } else {
                    // Only a matching immediately preceding cold-start frame
                    // can be backfilled: no gap, mismatch, or later reacquire.
                    const bool matchedPreviousColdFrame =
                        m_stage == Stage::Warmup
                        && m_pendingCenters.size() == 1
                        && m_pendingStartupClock != 0
                        && m_pendingStartupClock + 1 == m_frameClock
                        && !m_afterColdRestart
                        && !std::isfinite(m_lastPublishedTof)
                        && std::isfinite(m_pendingStartupTof)
                        && std::abs(out.tofSamples - m_pendingStartupTof)
                            <= m_cfg.startupConsistencySamples;
                    confirmed = observeStartup(out.tofSamples);
                    if (confirmed && matchedPreviousColdFrame) {
                        out.hasStartupBackfill = true;
                        out.startupBackfillFramesAgo = 1;
                        out.startupBackfillTofSamples = m_pendingStartupTof;
                        out.startupBackfillSnrDb = m_pendingStartupSnr;
                        out.startupBackfillPeakVoltage = m_pendingStartupVoltage;
                    }
                    if (!confirmed && m_stage == Stage::Warmup
                        && m_pendingCenters.size() == 1) {
                        // First vote or replacement after inconsistent peaks.
                        m_pendingStartupClock = m_frameClock;
                        m_pendingStartupTof = out.tofSamples;
                        m_pendingStartupSnr = out.selectedSnrDb;
                        m_pendingStartupVoltage = nan();
                        const int local = static_cast<int>(std::llround(out.tofSamples))
                            - f.directEnvelopeFirstIndex;
                        if (local >= 0
                            && local < static_cast<int>(f.directFilteredSignal.size()))
                            m_pendingStartupVoltage =
                                f.directFilteredSignal[static_cast<size_t>(local)];
                    }
                }
            }
            out.stage = m_stage;
            out.startupVotes = directIdentity ? 1 : static_cast<int>(m_pendingCenters.size());
            out.available = confirmed;
            if (confirmed) {
                m_unpublishedAge = 0;
                m_framesSincePublication = 0;
                m_lastPublishedTof = out.tofSamples;
                updateTemplate(selected->envelopeTemplate, true);
            }
            out.certified = confirmed && identityCertified(out);
            out.reason = confirmed
                ? (directIdentity ? "T2_STARTUP_DIRECT_PERIOD_CERTIFIED"
                                  : (out.certified ? "T2_STARTUP_CERTIFIED" : "T2_STARTUP_TRACKED"))
                : "T2_STARTUP_PENDING";
            return out;
        }

        // Shared thick/thin T2 post-gap identity quarantine: a marginal
        // low-SNR formal candidate immediately after a miss is insufficient
        // without ordinal evidence. Retain trusted history/template and
        // allow a later causal observation to reconfirm. Stable LOCK weak
        // packets and independently period-certified T2 are unaffected.
        if (m_stage == Stage::Locked
            && m_misses > 0 && !currentOrdinalEvidence
            && std::isfinite(out.selectedSnrDb)
            && out.selectedSnrDb < m_cfg.thickPostGapLowIdentitySnrDb
            && (!std::isfinite(out.templateCorrelation)
                || out.templateCorrelation < m_cfg.thickPostGapLowIdentityCorr)) {
            out.available = false;
            out.certified = false;
            out.reason = "T2_LOCK_POST_GAP_LOW_IDENTITY_PENDING";
            out.consecutiveMisses = m_misses;
            return out;
        }

        // LOCK: temporal identity is already established. DeltaT remains supporting evidence only.
        m_misses = 0;
        pushHistory(out.tofSamples);
        if (out.deltaTAcceptableSupport || out.t2T3Support
            || (std::isfinite(out.templateCorrelation)
                && out.templateCorrelation >= m_cfg.templateUpdateMinCorrelation)) {
            if (m_thickWallRangeOnly && selected->snrDb >= 8.0
                && out.templateCorrelation >= m_cfg.templateUpdateMinCorrelation) {
                const double originalCorr = correlationAtShift(
                    m_template, selected->envelopeTemplate, 0);
                if (std::isfinite(originalCorr) && originalCorr >= 0.88
                    && originalCorr < 0.95)
                    m_alternateTemplate = selected->envelopeTemplate;
                m_recentTemplate = selected->envelopeTemplate;
            }
            updateTemplate(selected->envelopeTemplate, false);
        }
        out.available = true;
        m_unpublishedAge = 0;
        m_framesSincePublication = 0;
        m_lastPublishedTof = out.tofSamples;
        out.certified = identityCertified(out);
        out.stage = m_stage;
        out.consecutiveMisses = m_misses;
        out.reason = directSelfCertifiedRecenter
            ? "T2_LOCK_SELF_CERTIFIED_RECENTER"
            : (out.certified ? "T2_LOCK_CERTIFIED" : "T2_LOCK_TRACKED");
        return out;
    }

public:
    Stage stage() const { return m_stage; }

    static const char *stageName(Stage stage)
    {
        switch (stage) {
        case Stage::GlobalSearch: return "GLOBAL_SEARCH";
        case Stage::Warmup: return "WARMUP";
        case Stage::Locked: return "LOCK";
        }
        return "GLOBAL_SEARCH";
    }

private:
    static double nan() { return std::numeric_limits<double>::quiet_NaN(); }

    const Candidate *chooseGlobalCandidate(const Frame &f) const
    {
        const Candidate *bestSupported = nullptr;
        double bestSupportedScore = -std::numeric_limits<double>::infinity();

        // GLOBAL/WARMUP优先使用当前序列证据确定ordinal。只要本帧已有可靠DeltaT
        // 或T2->T3 period证据，就不允许一个与period矛盾的更强峰先建立错误T2锁。
        const bool hasOrdinalEvidence = !f.thickWallRangeOnly && (
            (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
             && std::isfinite(f.periodSamples))
            || (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                && std::isfinite(f.referencePeriodSamples))
            || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                && std::isfinite(f.periodSamples)));
        if (hasOrdinalEvidence) {
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb))
                    continue;

                bool supported = false;
                bool strongPhysicalSupport = false;
                double residualPenalty = 0.0;
                if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.periodSamples)) {
                    const double r = std::abs(
                        c.tofSamples - (f.formalT1TofSamples + f.periodSamples));
                    if (r <= m_cfg.acceptablePeriodResidualSamples) {
                        supported = true;
                        strongPhysicalSupport = r <= m_cfg.strongPeriodResidualSamples;
                        residualPenalty += 0.05 * r;
                    }
                }
                if (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.referencePeriodSamples)) {
                    const double r = std::abs(
                        c.tofSamples - (f.formalT1TofSamples + f.referencePeriodSamples));
                    if (r <= m_cfg.referencePeriodResidualSamples) {
                        supported = true;
                        residualPenalty += 0.015 * r;
                    }
                }
                if (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                    && std::isfinite(f.periodSamples)) {
                    const double r = std::abs(
                        f.t3TofSamples - c.tofSamples - f.periodSamples);
                    if (r <= m_cfg.acceptablePeriodResidualSamples) {
                        supported = true;
                        strongPhysicalSupport = strongPhysicalSupport
                            || r <= m_cfg.strongPeriodResidualSamples;
                        residualPenalty += 0.05 * r;
                    }
                }
                if (!supported)
                    continue;

                const double requiredSnr = strongPhysicalSupport
                    ? std::min(m_cfg.minCandidateSnrDb, m_cfg.strongPeriodWeakCandidateSnrDb)
                    : m_cfg.minCandidateSnrDb;
                if (c.snrDb < requiredSnr)
                    continue;

                const double composite = c.score - residualPenalty;
                if (!bestSupported || composite > bestSupportedScore) {
                    bestSupported = &c;
                    bestSupportedScore = composite;
                }
            }
            // 有ordinal证据却找不到一致候选时，宁可等待，不允许远处强峰建锁。
            return bestSupported;
        }

        if (f.thickWallRangeOnly && std::isfinite(m_lastPublishedTof)) {
            const Candidate *best = nullptr;
            double bestResidual = std::numeric_limits<double>::infinity();
            const double driftLimit = m_framesSincePublication
                <= m_cfg.frozenIdentityColdRestartMisses
                ? m_cfg.thickReacquireMaxDriftSamples
                : 2.0 * m_cfg.thickReacquireMaxDriftSamples;
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                    || c.snrDb < m_cfg.thickSinglePeakMinSnrDb)
                    continue;
                const double distance = std::abs(c.tofSamples - m_lastPublishedTof);
                if (distance > driftLimit)
                    continue;
                if (!best || distance < bestResidual) {
                    best = &c;
                    bestResidual = distance;
                }
            }
            return best;
        }

        if (std::isfinite(m_frozenTofSamples)) {
            const Candidate *bestFrozen = nullptr;
            double bestFrozenResidual = std::numeric_limits<double>::infinity();
            double bestFrozenScore = -std::numeric_limits<double>::infinity();
            constexpr double frozenGateSamples = 128.0;
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                    || c.snrDb < m_cfg.minCandidateSnrDb)
                    continue;
                const double r = std::abs(c.tofSamples - m_frozenTofSamples);
                if (r > frozenGateSamples)
                    continue;
                const double corr = candidateTemplateCorrelation(c);
                if (!m_template.empty()
                    && (!std::isfinite(corr)
                        || corr < m_cfg.reacquireMinTemplateCorrelation))
                    continue;
                if (!bestFrozen || r < bestFrozenResidual - 1e-9
                    || (std::abs(r - bestFrozenResidual) <= 1e-9
                        && c.score > bestFrozenScore)) {
                    bestFrozen = &c;
                    bestFrozenResidual = r;
                    bestFrozenScore = c.score;
                }
            }
            return bestFrozen;
        }

        const Candidate *best = nullptr;
        double bestComposite = -std::numeric_limits<double>::infinity();
        constexpr double startupReferenceGateSamples = 128.0;
        for (const auto &c : f.candidates) {
            if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                || c.snrDb < m_cfg.minCandidateSnrDb)
                continue;

            double composite = c.score;
            if (std::isfinite(f.referenceTofSamples)) {
                const double residual = std::abs(c.tofSamples - f.referenceTofSamples);
                // 大窗口负责“发现”，建锁种子仍必须靠近参考厚度的物理T2中心；
                // 真正更远的新厚度只能由period证据或已建立的连续T2身份带过去。
                if (residual > startupReferenceGateSamples)
                    continue;
                composite -= 0.0040 * residual;
            }

            if (!best || composite > bestComposite) {
                best = &c;
                bestComposite = composite;
            }
        }
        return best;
    }

    // Mean-subtracted filtered-signal power, matching regular localEnergy().
    // No global or historical noise floor is referenced.
    static double directAcEnergy(const Frame &f, int center)
    {
        const int half = std::max(1, f.directEnergyHalfWidth);
        const int first = f.directEnvelopeFirstIndex;
        const int k = center - first;
        if (k - half < 0
            || k + half >= static_cast<int>(f.directFilteredSignal.size()))
            return nan();
        double mean = 0.0;
        for (int j = k - half; j <= k + half; ++j)
            mean += f.directFilteredSignal[static_cast<size_t>(j)];
        const int count = 2 * half + 1;
        mean /= count;
        double energy = 0.0;
        for (int j = k - half; j <= k + half; ++j) {
            const double v = f.directFilteredSignal[static_cast<size_t>(j)] - mean;
            energy += v * v;
        }
        return energy / count;
    }

    struct DirectPacketSnr {
        double snrDb = nan();
        double packetEnergy = nan();
        double backgroundEnergy = nan();
        int left = -1;
        int right = -1;
        int backgroundLeftCount = 0;
        int backgroundRightCount = 0;
        const char *status = "NO_PACKET";
    };

    // Keep peak search AND packet segmentation within predicted +/-32 only.
    // Estimate background over a SEPARATE wider +/-64-sample region, bounded
    // by the physical T2 support and currently available filtered waveform.
    // Missing bilateral background still makes SNR untrustworthy.
    DirectPacketSnr directWindowSnrDb(const Frame &f, int peak,
                                       int begin, int end) const
    {
        DirectPacketSnr result;
        const int first = f.directEnvelopeFirstIndex;
        const int k = peak - first;
        const double amplitude = f.directEnvelope[static_cast<size_t>(k)];
        // Reuse this tracker's scratch across candidates and 100-Hz frames.
        // nth_element may reorder the scratch but never the source envelope.
        auto &levels = m_snrLevelsScratch;
        levels.clear();
        levels.reserve(static_cast<size_t>(end - begin + 1));
        for (int i = begin; i <= end; ++i)
            levels.push_back(f.directEnvelope[static_cast<size_t>(i - first)]);
        const size_t mid = levels.size() / 2;
        std::nth_element(levels.begin(), levels.begin() + mid, levels.end());
        const double baseline = levels[mid];
        if (!std::isfinite(amplitude) || amplitude <= baseline * 1.05) {
            result.status = "NO_PROMINENT_PACKET";
            return result;
        }
        const double edge = baseline + 0.30 * (amplitude - baseline);
        bool leftFound = false, rightFound = false;
        // Two consecutive values below the envelope threshold prevent a
        // single noisy trough splitting an otherwise continuous packet.
        for (int i = peak - 1; i > begin; --i) {
            if (f.directEnvelope[static_cast<size_t>(i-first)] <= edge
                && f.directEnvelope[static_cast<size_t>(i-1-first)] <= edge) {
                result.left = i + 1;
                leftFound = true;
                break;
            }
        }
        for (int i = peak + 1; i < end; ++i) {
            if (f.directEnvelope[static_cast<size_t>(i-first)] <= edge
                && f.directEnvelope[static_cast<size_t>(i+1-first)] <= edge) {
                result.right = i - 1;
                rightFound = true;
                break;
            }
        }
        if (!leftFound || !rightFound) {
            result.status = "PACKET_BOUNDARY_MISSING";
            return result;
        }
        const int packetWidth = result.right - result.left + 1;
        // At 160 MHz the 20-30 MHz carrier period spans roughly 5-8
        // samples. Shorter envelope islands are not a resolved wavepacket.
        if (packetWidth < 8 || packetWidth > 48) {
            result.status = "PACKET_WIDTH_UNRELIABLE";
            return result;
        }
        // Packet AC energy over the WHOLE packet, not one peak-centered
        // 7-sample window. Remove its own DC component as localEnergy does.
        double mean = 0.0;
        for (int i = result.left; i <= result.right; ++i)
            mean += f.directFilteredSignal[static_cast<size_t>(i-first)];
        mean /= packetWidth;
        double packetPower = 0.0;
        for (int i = result.left; i <= result.right; ++i) {
            const double v = f.directFilteredSignal[static_cast<size_t>(i-first)] - mean;
            packetPower += v * v;
        }
        result.packetEnergy = packetPower / packetWidth;
        if (result.packetEnergy <= 1e-24) {
            result.status = "PACKET_ZERO_ENERGY";
            return result;
        }
        // Extend background by 32 samples on EACH side without extending
        // peak search, packet boundaries, or the accepted TOF gate. Every
        // 7-sample energy window must stay within the physical T2 ROI and
        // outside the entire packet plus guard. No temporal noise is used.
        constexpr int backgroundExtraSamples = 32;
        const int half = std::max(1, f.directEnergyHalfWidth);
        const int guard = std::max(3, half);
        const int stride = std::max(2, half);
        const int availableLeft = std::max(
            f.directEnvelopeFirstIndex + half, f.directSupportLeft + half);
        const int availableRight = std::min(
            f.directEnvelopeFirstIndex
                + static_cast<int>(f.directFilteredSignal.size()) - half - 1,
            f.directSupportRight - half);
        const int backgroundBegin = std::max(
            availableLeft, begin - backgroundExtraSamples);
        const int backgroundEnd = std::min(
            availableRight, end + backgroundExtraSamples);
        auto &bg = m_snrBackgroundScratch;
        bg.clear();
        if (backgroundBegin <= backgroundEnd)
            bg.reserve(static_cast<size_t>((backgroundEnd-backgroundBegin) / stride + 1));
        for (int i = backgroundBegin; i <= backgroundEnd; i += stride) {
            const bool isLeft = i + half < result.left - guard;
            const bool isRight = i - half > result.right + guard;
            if (!isLeft && !isRight)
                continue;
            const double e = directAcEnergy(f, i);
            if (!std::isfinite(e) || e <= 1e-24)
                continue;
            bg.push_back(e);
            if (isLeft)
                ++result.backgroundLeftCount;
            else
                ++result.backgroundRightCount;
        }
        if (result.backgroundLeftCount < 2 || result.backgroundRightCount < 2
            || bg.size() < 5) {
            result.status = "UNBALANCED_BACKGROUND";
            return result;
        }
        // Winsorized robust MEAN of the remaining local AC energies.
        // Unlike a pure median, averaging preserves the background power
        // scale; capping only the upper/lower 10% reduces isolated spikes.
        std::sort(bg.begin(), bg.end());
        const size_t trim = bg.size() / 10;
        const double low = bg[trim], high = bg[bg.size()-1-trim];
        double sum = 0.0;
        for (double e : bg)
            sum += std::clamp(e, low, high);
        result.backgroundEnergy = sum / static_cast<double>(bg.size());
        // E_packet is observed signal PLUS noise power. Subtract the local
        // independently estimated noise contribution before dividing:
        // true excess-SNR = (E_packet-E_bg)/E_bg, not (S+N)/N.
        // Noise-only packets have zero/negative excess and cannot pass.
        const double excess = result.packetEnergy - result.backgroundEnergy;
        if (excess <= 1e-24) {
            result.status = "NO_POSITIVE_PACKET_EXCESS";

            return result;
        }
        result.snrDb = 10.0 * std::log10(excess / result.backgroundEnergy);
        result.status = "OK";
        return result;
    }

    // Search the raw T2 envelope directly, not f.candidates or
    // f.weakLockCandidates. SNR is measured using this frame's filtered
    // samples around each peak and noise from the same prediction window.
    // Envelope-energy t50 remains the TOF estimator; NCC is diagnostic only.
    bool findDirectMeasuredSinglePeak(const Frame &f, double predicted,
                                      Candidate &outPeak,
                                      std::string &snrTrace) const
    {
        if (!std::isfinite(predicted)
            || f.directEnvelope.size() < 3
            || f.directEnvelope.size() != f.directFilteredSignal.size()
            || f.directEnvelopeFirstIndex < 0)
            return false;
        const int first = f.directEnvelopeFirstIndex;
        const int last = first + static_cast<int>(f.directEnvelope.size()) - 1;
        const int begin = std::max(first + 1,
            static_cast<int>(std::ceil(predicted - m_cfg.thickAmbiguousPredictionGateSamples)));
        const int end = std::min(last - 1,
            static_cast<int>(std::floor(predicted + m_cfg.thickAmbiguousPredictionGateSamples)));
        bool found = false;
        double closest = std::numeric_limits<double>::infinity();
        double highestSnr = -std::numeric_limits<double>::infinity();
        for (int i = begin; i <= end; ++i) {
            const int k = i - first;
            const double e = f.directEnvelope[static_cast<size_t>(k)];
            if (!(e > 1e-24
                && e >= f.directEnvelope[static_cast<size_t>(k - 1)]
                && e >= f.directEnvelope[static_cast<size_t>(k + 1)]))
                continue;
            const DirectPacketSnr packet = directWindowSnrDb(f, i, begin, end);
            if (!snrTrace.empty())
                snrTrace.push_back('|');
            snrTrace += std::to_string(i) + ":" + packet.status
                + ":" + (std::isfinite(packet.snrDb)
                    ? std::to_string(packet.snrDb) : "NA")
                + ":" + std::to_string(packet.left)
                + "-" + std::to_string(packet.right)
                + ":" + std::to_string(packet.backgroundLeftCount)
                + "/" + std::to_string(packet.backgroundRightCount);
            const double snr = packet.snrDb;
            if (!std::isfinite(snr)
                || snr <= m_cfg.thickWeakIdentityMinSnrDb)
                continue;
            const int left = std::max(f.directSupportLeft,
                                     i - f.directT50HalfWidth);
            const int right = std::min(f.directSupportRight,
                                      i + f.directT50HalfWidth);
            if (left < first || right > last || right < left)
                continue;
            double total = 0.0;
            for (int j = left; j <= right; ++j) {
                const double amp = f.directEnvelope[static_cast<size_t>(j - first)];
                total += amp * amp;
            }
            if (total <= 1e-24)
                continue;
            const double half = total * 0.5;
            double sum = 0.0;
            double tof = static_cast<double>(i);
            for (int j = left; j <= right; ++j) {
                const double amp = f.directEnvelope[static_cast<size_t>(j - first)];
                const double energy = amp * amp;
                if (sum + energy >= half) {
                    tof = static_cast<double>(j) - 0.5
                        + (energy > 1e-24
                            ? std::clamp((half - sum) / energy, 0.0, 1.0) : 0.5);
                    break;
                }
                sum += energy;
            }
            // All LOCK, warmup, history, velocity and fallback gates must
            // use the SAME coordinate system in integer peak tracking mode.
            // The t50 estimate above remains unchanged when precision is ON.
            if (f.integerPeakTrackingEnabled) {
                const double refinedPeak = matchedFilteredPeakTofSamples(f, i);
                if (!std::isfinite(refinedPeak))
                    continue;
                tof = refinedPeak;
            }
            const double dist = std::abs(tof - predicted);
            if (dist > m_cfg.thickAmbiguousPredictionGateSamples
                || (std::isfinite(f.referenceTofSamples)
                    && std::abs(tof - f.referenceTofSamples) > 128.0))
                continue;
            // Evidence-adaptive admission: NET packet energy SNR>1.3,
            // or genuinely weak energy evidence with strong temporal shape
            // identity and very tight motion continuity. Does not require
            // high SNR for a visibly coherent weak echo.
            Candidate probe;
            probe.envelopeTemplate.reserve(41);
            for (int j = i - 20; j <= i + 20; ++j)
                probe.envelopeTemplate.push_back(j >= first && j <= last
                    ? f.directEnvelope[static_cast<size_t>(j - first)] : 0.0);
            const double corr = candidateTemplateCorrelation(probe);
            const bool ordinaryEnergy = snr > m_cfg.thickAmbiguousMinSnrDb;
            const bool weakIdentity = dist <= m_cfg.thickWeakIdentityMaxPredictionResidual
                && std::isfinite(corr)
                && corr >= m_cfg.thickWeakIdentityCorr;
            if (!ordinaryEnergy && !weakIdentity)
                continue;
            if (!found || dist < closest - 1e-9
                || (std::abs(dist - closest) <= 1e-9 && snr > highestSnr)) {
                found = true;
                closest = dist;
                highestSnr = snr;
                outPeak.tofSamples = tof;
                outPeak.snrDb = snr;
                outPeak.score = snr / 10.0;
                outPeak.rank = -1; // direct waveform peak, not a Top-8 rank
                outPeak.envelopePeakIndex = i;
                outPeak.envelopeTemplate = std::move(probe.envelopeTemplate);
            }
        }
        return found;
    }

    const Candidate *chooseLockedCandidate(
        const Frame &f,
        double predicted,
        double gate) const
    {
        double nearestResidual = std::numeric_limits<double>::infinity();
        double secondResidual = std::numeric_limits<double>::infinity();
        for (const auto &c : f.candidates) {
            if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                || c.snrDb < m_cfg.lockedWeakCandidateSnrDb)
                continue;
            const double r = std::abs(c.tofSamples - predicted);
            if (r < nearestResidual) {
                secondResidual = nearestResidual;
                nearestResidual = r;
            } else if (r < secondResidual) {
                secondResidual = r;
            }
        }

        const Candidate *best = nullptr;
        double bestResidual = std::numeric_limits<double>::infinity();
        double bestScore = -std::numeric_limits<double>::infinity();
        for (const auto &c : f.candidates) {
            if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                || c.snrDb < m_cfg.lockedWeakCandidateSnrDb)
                continue;
            const double r = std::abs(c.tofSamples - predicted);
            if (r > gate)
                continue;
            const double corr = candidateTemplateCorrelation(c);
            if (!m_template.empty()) {
                const bool normalTemplate = std::isfinite(corr)
                    && corr >= m_cfg.lockMinTemplateCorrelation;
                const bool nearPredictionOverride = std::isfinite(corr)
                    && r <= m_cfg.uncorroboratedNearPredictionResidualSamples
                    && corr >= m_cfg.uncorroboratedNearPredictionMinCorrelation
                    && c.snrDb >= m_cfg.uncorroboratedNearPredictionMinSnrDb;
                const bool nearestCandidate = std::abs(r - nearestResidual) <= 1e-9;
                const bool unambiguous = nearestCandidate
                    && (!std::isfinite(secondResidual)
                        || secondResidual - nearestResidual
                            >= m_cfg.uncorroboratedAmbiguityGapSamples);
                double referenceResidual = std::numeric_limits<double>::infinity();
                const bool referenceAnchorAvailable = std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.referencePeriodSamples);
                if (referenceAnchorAvailable) {
                    referenceResidual = std::abs(c.tofSamples
                        - (f.formalT1TofSamples + f.referencePeriodSamples));
                }
                const bool uniqueShapeOverride = unambiguous
                    && r <= m_cfg.uncorroboratedUniqueResidualSamples
                    && (r <= m_cfg.uncorroboratedUniqueNoReferenceResidualSamples
                        || (referenceAnchorAvailable
                            && referenceResidual
                                <= m_cfg.uncorroboratedUniqueReferenceResidualSamples))
                    && std::isfinite(corr)
                    && corr >= m_cfg.uncorroboratedUniqueMinCorrelation
                    && c.snrDb >= m_cfg.uncorroboratedUniqueMinSnrDb;
                const bool dualAnchorOverride = unambiguous
                    && r <= m_cfg.uncorroboratedDualAnchorResidualSamples
                    && referenceResidual <= m_cfg.uncorroboratedDualAnchorReferenceResidualSamples
                    && std::isfinite(corr)
                    && corr >= m_cfg.uncorroboratedDualAnchorMinCorrelation
                    && c.snrDb >= m_cfg.uncorroboratedDualAnchorMinSnrDb;
                if (!normalTemplate && !nearPredictionOverride
                    && !uniqueShapeOverride && !dualAnchorOverride)
                    continue;
            }
            if (!best || r < bestResidual - 1e-9
                || (std::abs(r - bestResidual) <= 1e-9 && c.score > bestScore)) {
                best = &c;
                bestResidual = r;
                bestScore = c.score;
            }
        }
        return best;
    }

    void applyEvidence(const Frame &f, Decision &out) const
    {
        if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
            && std::isfinite(f.periodSamples)) {
            out.periodResidualSamples = out.tofSamples
                - (f.formalT1TofSamples + f.periodSamples);
            const double a = std::abs(out.periodResidualSamples);
            out.deltaTStrongSupport = a <= m_cfg.strongPeriodResidualSamples;
            out.deltaTAcceptableSupport = a <= m_cfg.acceptablePeriodResidualSamples;
        }

        if (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
            && std::isfinite(f.referencePeriodSamples)) {
            const double residual = out.tofSamples
                - (f.formalT1TofSamples + f.referencePeriodSamples);
            out.referenceT1Support = std::abs(residual)
                <= m_cfg.referencePeriodResidualSamples;
        }

        if (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
            && std::isfinite(f.periodSamples)) {
            out.t2T3ResidualSamples = f.t3TofSamples
                - out.tofSamples - f.periodSamples;
            out.t2T3Support = std::abs(out.t2T3ResidualSamples)
                <= m_cfg.acceptablePeriodResidualSamples;
        }
    }

    bool identityCertified(const Decision &d) const
    {
        // DeltaT/period只是增强身份证据；没有它时，稳定LOCK仍允许T2-only输出。
        return d.deltaTAcceptableSupport || d.t2T3Support || d.referenceT1Support
            || (m_stage == Stage::Locked && m_history.size() >= 4
                && std::isfinite(d.templateCorrelation)
                && d.templateCorrelation >= m_cfg.uncorroboratedCertificationCorrelation);
    }

    bool observeStartup(double tof)
    {
        if (!std::isfinite(tof))
            return false;
        if (m_stage == Stage::GlobalSearch) {
            m_stage = Stage::Warmup;
            m_pendingCenters.clear();
            m_pendingCenters.push_back(tof);
            return false;
        }

        const double center = median(m_pendingCenters);
        if (std::abs(tof - center) > m_cfg.startupConsistencySamples) {
            m_pendingCenters.clear();
            m_pendingCenters.push_back(tof);
            return false;
        }

        m_pendingCenters.push_back(tof);
        while (static_cast<int>(m_pendingCenters.size()) > m_cfg.startupWindowFrames)
            m_pendingCenters.pop_front();

        const int confirmations = m_afterColdRestart
            ? std::max(m_cfg.startupConfirmations, m_cfg.afterColdRestartConfirmations)
            : m_cfg.startupConfirmations;
        if (static_cast<int>(m_pendingCenters.size()) < confirmations)
            return false;

        m_stage = Stage::Locked;
        m_history.clear();
        for (double v : m_pendingCenters)
            pushHistory(v);
        m_pendingCenters.clear();
        m_frozenTofSamples = nan();
        m_misses = 0;
        m_recoveryMisses = 0;
        m_afterColdRestart = false;
        return true;
    }

    bool observeMiss()
    {
        if (m_stage == Stage::Locked) {
            ++m_misses;
            if (m_misses >= m_cfg.maxMissesBeforeGlobal) {
                m_frozenTofSamples = predictedTof();
                if (!std::isfinite(m_frozenTofSamples) && !m_history.empty())
                    m_frozenTofSamples = m_history.back();
                m_stage = Stage::GlobalSearch;
                m_history.clear();
                m_pendingCenters.clear();
                m_misses = 0;
                m_recoveryMisses = 0;
            }
            return false;
        }

        if (!std::isfinite(m_frozenTofSamples))
            return false;

        ++m_recoveryMisses;
        if (m_recoveryMisses < std::max(1, m_cfg.frozenIdentityColdRestartMisses)
            && (!m_thickWallRangeOnly || m_framesSincePublication
                < std::max(1, m_cfg.frozenIdentityColdRestartMisses)))
            return false;

        // 长空窗说明旧T2身份已经失去时序意义。彻底清除旧位置和模板，
        // 重新进入与首次采集完全一致的GlobalSearch/2-of-3冷启动。
        m_stage = Stage::GlobalSearch;
        m_history.clear();
        m_pendingCenters.clear();
        m_frozenTofSamples = nan();
        m_fallbackPredictionTof = nan();
        m_template.clear();
        m_misses = 0;
        m_recoveryMisses = 0;
        m_recenterPendingTof = nan();
        m_recenterPendingCount = 0;
        return true;
    }

    static double correlationAtShift(const std::vector<double> &a,
                                     const std::vector<double> &b, int shift)
    {
        if (a.empty() || b.size() != a.size() || std::abs(shift) >= static_cast<int>(a.size()))
            return nan();
        const size_t count = a.size() - static_cast<size_t>(std::abs(shift));
        // Existing thin-wall tracker/tests also use short diagnostic templates.
        // Only thick-wall multi-shift alignment assumes a longer packet.
        if (count < 2)
            return nan();
        const size_t aStart = shift < 0 ? static_cast<size_t>(-shift) : 0;
        const size_t bStart = shift > 0 ? static_cast<size_t>(shift) : 0;
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < count; ++i) {
            meanA += a[aStart + i];
            meanB += b[bStart + i];
        }
        meanA /= static_cast<double>(count);
        meanB /= static_cast<double>(count);
        double num = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < count; ++i) {
            const double x = a[aStart + i] - meanA;
            const double y = b[bStart + i] - meanB;
            num += x * y;
            aa += x * x;
            bb += y * y;
        }
        return aa > 1e-24 && bb > 1e-24 ? num / std::sqrt(aa * bb) : nan();
    }

    double candidateTemplateCorrelation(const Candidate &candidate) const
    {
        const double primary = correlationAtShift(m_template, candidate.envelopeTemplate, 0);
        if (!m_thickWallRangeOnly)
            return primary;
        double best = primary;
        for (const auto *shape : {&m_template, &m_recentTemplate, &m_alternateTemplate}) {
            if (shape->size() < 20)
                continue;
            for (int shift = -2; shift <= 2; ++shift) {
                const double corr = correlationAtShift(*shape, candidate.envelopeTemplate, shift);
                if (std::isfinite(corr) && (!std::isfinite(best) || corr > best))
                    best = corr;
            }
        }
        return best;
    }

    void updateTemplate(const std::vector<double> &shape, bool force)
    {
        if (shape.empty())
            return;
        if (force || m_template.size() != shape.size()) {
            m_template = shape;
            if (m_thickWallRangeOnly) {
                m_recentTemplate = shape;
                m_alternateTemplate.clear();
            }
            return;
        }
        const double alpha = std::clamp(m_cfg.templateEmaAlpha, 0.01, 1.0);
        for (size_t i = 0; i < shape.size(); ++i)
            m_template[i] = (1.0 - alpha) * m_template[i] + alpha * shape[i];
    }

    void pushHistory(double tof)
    {
        if (!std::isfinite(tof))
            return;
        // A formally accepted primary position supersedes provisional
        // fallback prediction; its template update follows the normal rules.
        m_fallbackPredictionTof = nan();
        m_history.push_back(tof);
        while (m_history.size() > 7)
            m_history.pop_front();
    }

    double predictedTof() const
    {
        if (std::isfinite(m_fallbackPredictionTof))
            return m_fallbackPredictionTof;
        if (m_history.empty())
            return nan();
        if (m_history.size() < 3)
            return m_history.back();

        std::vector<double> velocity;
        for (size_t i = 1; i < m_history.size(); ++i)
            velocity.push_back(m_history[i] - m_history[i - 1]);
        const double v = median(velocity);
        return m_history.back() + v;
    }

    static double median(const std::deque<double> &values)
    {
        return median(std::vector<double>(values.begin(), values.end()));
    }

    static double median(std::vector<double> values)
    {
        if (values.empty())
            return nan();
        std::sort(values.begin(), values.end());
        const size_t m = values.size() / 2;
        return values.size() % 2
            ? values[m] : 0.5 * (values[m - 1] + values[m]);
    }

    Config m_cfg;
    Stage m_stage = Stage::GlobalSearch;
    std::deque<double> m_history;
    std::deque<double> m_pendingCenters;
    std::uint64_t m_frameClock = 0;
    std::uint64_t m_pendingStartupClock = 0;
    double m_pendingStartupTof = nan();
    double m_pendingStartupSnr = nan();
    double m_pendingStartupVoltage = nan();
    double m_frozenTofSamples = nan();
    // Published fallback TOF for next-frame prediction only. It is not an
    // accepted template or a certified observation in the TOF history.
    double m_fallbackPredictionTof = nan();
    std::vector<double> m_template;
    int m_misses = 0;
    int m_recoveryMisses = 0;
    int m_unpublishedAge = 0;
    bool m_afterColdRestart = false;
    double m_recenterPendingTof = nan();
    int m_recenterPendingCount = 0;
    int m_framesSincePublication = 0;
    double m_lastPublishedTof = nan();
    bool m_thickWallRangeOnly = false;
    std::vector<double> m_recentTemplate;
    std::vector<double> m_alternateTemplate;
    // Allocation-free after warmup for the fallback packet-net-SNR scans.
    // Mutable scratch only: never consulted for history, identity or output.
    mutable std::vector<double> m_snrLevelsScratch;
    mutable std::vector<double> m_snrBackgroundScratch;
};
