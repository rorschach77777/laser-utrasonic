#pragma once

#include <QVariantMap>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace CandidateTof {

inline double finiteNumber(const QVariantMap &r, const QString &key)
{
    bool ok = false;
    const double value = r.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : std::numeric_limits<double>::quiet_NaN();
}

enum class PolarityIdentity { Unknown, Positive, Negative };

inline PolarityIdentity oppositePolarity(PolarityIdentity identity)
{
    if (identity == PolarityIdentity::Positive)
        return PolarityIdentity::Negative;
    if (identity == PolarityIdentity::Negative)
        return PolarityIdentity::Positive;
    return PolarityIdentity::Unknown;
}

struct PolarityBranch
{
    bool solved = false;
    double r2 = std::numeric_limits<double>::quiet_NaN();
    double center = std::numeric_limits<double>::quiet_NaN();
};

struct PolarityDecision
{
    PolarityIdentity identity = PolarityIdentity::Unknown;
    PolarityIdentity selected = PolarityIdentity::Unknown;
    double selectedCenter = std::numeric_limits<double>::quiet_NaN();
    QString stage;
};

class DualPolarityIdentitySelector
{
public:
    PolarityDecision observe(const QVariantMap &r)
    {
        PolarityDecision out;
        out.stage = classifyStage(r);
        const PolarityBranch positive = branch(r, true);
        const PolarityBranch negative = branch(r, false);

        if (m_identity == PolarityIdentity::Unknown) {
            const PolarityIdentity candidate = chooseAcquisitionCandidate(positive, negative);
            const PolarityBranch candidateBranch = getBranch(candidate, positive, negative);
            updateTentative(candidate, candidateBranch);

            const bool commitStage = out.stage == QStringLiteral("WARMUP")
                || out.stage == QStringLiteral("LOCK");
            if (commitStage && m_tentative != PolarityIdentity::Unknown
                && m_tentativeCount >= kAcquireConfirmFrames) {
                const PolarityBranch established = getBranch(m_tentative, positive, negative);
                if (established.solved && established.r2 >= kMinimumAcquireR2) {
                    m_identity = m_tentative;
                    m_identityCenter = established.center;
                    m_previousIdentityCenter = established.center;
                    clearChallenger();
                }
            }
        }

        if (m_identity != PolarityIdentity::Unknown) {
            const PolarityBranch current = getBranch(m_identity, positive, negative);
            const PolarityIdentity opposite = oppositePolarity(m_identity);
            const PolarityBranch challenger = getBranch(opposite, positive, negative);

            const bool currentHealthy = current.solved
                && std::isfinite(current.center) && std::isfinite(current.r2)
                && current.r2 >= kMinimumHoldR2;
            const bool challengerQualityOk = challenger.solved
                && std::isfinite(challenger.center) && std::isfinite(challenger.r2)
                && challenger.r2 >= kMinimumSwitchR2;
            const bool currentClearlyLost = !currentHealthy;

            if (currentClearlyLost && challengerQualityOk)
                updateChallenger(opposite, challenger.center);
            else
                clearChallenger();

            if (m_challenger != PolarityIdentity::Unknown
                && m_challengerCount >= kSwitchConfirmFrames) {
                m_identity = m_challenger;
                m_identityCenter = m_challengerCenter;
                m_previousIdentityCenter = m_challengerCenter;
                clearChallenger();
            }

            const PolarityBranch selected = getBranch(m_identity, positive, negative);
            if (selected.solved && std::isfinite(selected.center)) {
                out.selected = m_identity;
                out.selectedCenter = selected.center;
                if (!std::isfinite(m_identityCenter))
                    m_previousIdentityCenter = selected.center;
                else
                    m_previousIdentityCenter = m_identityCenter;
                m_identityCenter = selected.center;
            }
        }

        out.identity = m_identity;
        return out;
    }

private:
    static constexpr double kAcquireContinuitySamples = 4.0;
    static constexpr double kAcquireContinuityAdvantage = 1.5;
    static constexpr int kAcquireConfirmFrames = 3;
    static constexpr double kMinimumAcquireR2 = 0.95;
    static constexpr double kMinimumHoldR2 = 0.85;
    static constexpr double kMinimumSwitchR2 = 0.98;
    static constexpr int kSwitchConfirmFrames = 4;

    PolarityIdentity m_identity = PolarityIdentity::Unknown;
    double m_identityCenter = std::numeric_limits<double>::quiet_NaN();
    double m_previousIdentityCenter = std::numeric_limits<double>::quiet_NaN();
    PolarityIdentity m_tentative = PolarityIdentity::Unknown;
    int m_tentativeCount = 0;
    double m_tentativeCenter = std::numeric_limits<double>::quiet_NaN();
    PolarityIdentity m_challenger = PolarityIdentity::Unknown;
    int m_challengerCount = 0;
    double m_challengerCenter = std::numeric_limits<double>::quiet_NaN();

    static QString classifyStage(const QVariantMap &r)
    {
        const QString state = r.value(QStringLiteral("trackingState")).toString().toUpper();
        if (state.contains(QStringLiteral("WARMUP")))
            return QStringLiteral("WARMUP");
        if (state.contains(QStringLiteral("LOCK"))
            || state.contains(QStringLiteral("RELOCK"))
            || state.contains(QStringLiteral("BACKUP"))
            || state.contains(QStringLiteral("JUMP"))
            || r.value(QStringLiteral("predictionApplied")).toBool())
            return QStringLiteral("LOCK");
        return QStringLiteral("SEARCH");
    }

    static PolarityBranch branch(const QVariantMap &r, bool positive)
    {
        PolarityBranch b;
        const QString prefix = positive ? QStringLiteral("positive") : QStringLiteral("negative");
        b.solved = r.value(prefix + QStringLiteral("FitSolved")).toBool();
        b.r2 = finiteNumber(r, prefix + QStringLiteral("FitR2"));
        b.center = finiteNumber(r, prefix + QStringLiteral("FitCenterIndex"));
        b.solved = b.solved && std::isfinite(b.r2) && std::isfinite(b.center);
        return b;
    }

    static PolarityBranch getBranch(PolarityIdentity identity,
                                    const PolarityBranch &positive,
                                    const PolarityBranch &negative)
    {
        if (identity == PolarityIdentity::Positive)
            return positive;
        if (identity == PolarityIdentity::Negative)
            return negative;
        return {};
    }

    PolarityIdentity chooseAcquisitionCandidate(const PolarityBranch &positive,
                                                const PolarityBranch &negative) const
    {
        if (positive.solved && !negative.solved)
            return PolarityIdentity::Positive;
        if (!positive.solved && negative.solved)
            return PolarityIdentity::Negative;
        if (!positive.solved && !negative.solved)
            return PolarityIdentity::Unknown;

        if (std::isfinite(m_tentativeCenter)) {
            const double positiveResidual = std::abs(positive.center - m_tentativeCenter);
            const double negativeResidual = std::abs(negative.center - m_tentativeCenter);
            if (positiveResidual <= kAcquireContinuitySamples
                && positiveResidual + kAcquireContinuityAdvantage < negativeResidual)
                return PolarityIdentity::Positive;
            if (negativeResidual <= kAcquireContinuitySamples
                && negativeResidual + kAcquireContinuityAdvantage < positiveResidual)
                return PolarityIdentity::Negative;
        }
        return positive.r2 >= negative.r2 ? PolarityIdentity::Positive : PolarityIdentity::Negative;
    }

    void updateTentative(PolarityIdentity candidate, const PolarityBranch &b)
    {
        if (candidate == PolarityIdentity::Unknown || !b.solved) {
            m_tentative = PolarityIdentity::Unknown;
            m_tentativeCount = 0;
            m_tentativeCenter = std::numeric_limits<double>::quiet_NaN();
            return;
        }
        const bool consecutive = candidate == m_tentative
            && std::isfinite(m_tentativeCenter)
            && std::abs(b.center - m_tentativeCenter) <= kAcquireContinuitySamples;
        if (consecutive) {
            ++m_tentativeCount;
            m_tentativeCenter = b.center;
        } else {
            m_tentative = candidate;
            m_tentativeCount = 1;
            m_tentativeCenter = b.center;
        }
    }

    void updateChallenger(PolarityIdentity challenger, double center)
    {
        const bool consecutive = challenger == m_challenger
            && std::isfinite(m_challengerCenter)
            && std::abs(center - m_challengerCenter) <= kAcquireContinuitySamples;
        if (consecutive) {
            ++m_challengerCount;
            m_challengerCenter = center;
        } else {
            m_challenger = challenger;
            m_challengerCount = 1;
            m_challengerCenter = center;
        }
    }

    void clearChallenger()
    {
        m_challenger = PolarityIdentity::Unknown;
        m_challengerCount = 0;
        m_challengerCenter = std::numeric_limits<double>::quiet_NaN();
    }
};

struct SingleAnchorDecision
{
    double center = std::numeric_limits<double>::quiet_NaN();
    QString source = QStringLiteral("UNAVAILABLE");
};

class SingleAnchorFallback
{
public:
    SingleAnchorDecision resolve(const QVariantMap &r, const PolarityDecision &selector)
    {
        SingleAnchorDecision out;
        if (selector.identity == PolarityIdentity::Unknown) {
            out.source = QStringLiteral("IDENTITY_UNKNOWN");
            return out;
        }
        if (std::isfinite(selector.selectedCenter)) {
            out.center = selector.selectedCenter;
            out.source = QStringLiteral("IDENTITY_FIT");
            update(selector.selectedCenter);
            return out;
        }
        const QString key = selector.identity == PolarityIdentity::Positive
            ? QStringLiteral("positivePolarityDirectPeakIndex")
            : QStringLiteral("negativePolarityDirectPeakIndex");
        const double direct = finiteNumber(r, key);
        if (!std::isfinite(direct)) {
            out.source = QStringLiteral("IDENTITY_DIRECT_UNAVAILABLE");
            return out;
        }
        const double predicted = prediction();
        if (std::isfinite(predicted) && std::abs(direct - predicted) > kGate) {
            out.source = QStringLiteral("DIRECT_REJECTED_CONTINUITY");
            return out;
        }
        out.center = direct;
        out.source = QStringLiteral("IDENTITY_DIRECT_3PT");
        update(direct);
        return out;
    }

private:
    static constexpr double kGate = 6.0;
    static constexpr double kVelocity = 3.0;
    double m_previous = std::numeric_limits<double>::quiet_NaN();
    double m_last = std::numeric_limits<double>::quiet_NaN();

    double prediction() const
    {
        if (!std::isfinite(m_last)) return std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(m_previous)) return m_last;
        return m_last + std::clamp(m_last - m_previous, -kVelocity, kVelocity);
    }
    void update(double center)
    {
        m_previous = m_last;
        m_last = center;
    }
};

struct DualAnchorDecision
{
    double center = std::numeric_limits<double>::quiet_NaN();
    QString source = QStringLiteral("UNAVAILABLE");
    double directCandidate = std::numeric_limits<double>::quiet_NaN();
    double fitPredictedCenter = std::numeric_limits<double>::quiet_NaN();
    double directPredictedCenter = std::numeric_limits<double>::quiet_NaN();
    bool acceptedByFitAnchor = false;
    bool acceptedByDirectAnchor = false;
};

class DualAnchorFallback
{
    struct Track {
        double previous = std::numeric_limits<double>::quiet_NaN();
        double last = std::numeric_limits<double>::quiet_NaN();
        double predicted() const
        {
            if (!std::isfinite(last)) return std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(previous)) return last;
            return last + std::clamp(last - previous, -3.0, 3.0);
        }
        void update(double v) { if (std::isfinite(v)) { previous = last; last = v; } }
        void reset() { previous = last = std::numeric_limits<double>::quiet_NaN(); }
        void resetTo(double v) { reset(); update(v); }
    };

public:
    DualAnchorDecision resolve(const QVariantMap &r,
                               const PolarityDecision &selector,
                               const SingleAnchorDecision &single)
    {
        DualAnchorDecision out;
        resetIfIdentityChanged(selector.identity);
        if (selector.identity == PolarityIdentity::Unknown) {
            out.source = QStringLiteral("IDENTITY_UNKNOWN");
            return out;
        }
        const QString key = selector.identity == PolarityIdentity::Positive
            ? QStringLiteral("positivePolarityDirectPeakIndex")
            : QStringLiteral("negativePolarityDirectPeakIndex");
        const double direct = finiteNumber(r, key);
        const double raw = finiteNumber(r, QStringLiteral("directParabolicPeakIndex"));
        out.directCandidate = direct;
        out.fitPredictedCenter = m_fit.predicted();
        out.directPredictedCenter = m_direct.predicted();

        if (std::isfinite(selector.selectedCenter)) {
            out.center = selector.selectedCenter;
            out.source = QStringLiteral("IDENTITY_FIT");
            m_fit.update(out.center);
            updateDirectIfContinuous(direct, out.directPredictedCenter);
            return out;
        }
        if (!std::isfinite(direct)) {
            out.source = QStringLiteral("IDENTITY_DIRECT_UNAVAILABLE");
            return out;
        }
        if (std::isfinite(out.fitPredictedCenter))
            out.acceptedByFitAnchor = std::abs(direct - out.fitPredictedCenter) <= 6.0;
        if (std::isfinite(out.directPredictedCenter))
            out.acceptedByDirectAnchor = std::abs(direct - out.directPredictedCenter) <= 4.0;

        if (std::isfinite(single.center)) {
            out.center = direct;
            out.source = QStringLiteral("DUAL_DIRECT_SINGLE_COMPAT");
            m_direct.resetTo(direct);
            return out;
        }
        const bool noAnchor = !std::isfinite(out.fitPredictedCenter)
            && !std::isfinite(out.directPredictedCenter);
        const bool rawCorroborated = std::isfinite(raw) && std::abs(direct - raw) <= 1.5;
        if (out.acceptedByFitAnchor || out.acceptedByDirectAnchor || noAnchor || rawCorroborated) {
            out.center = direct;
            if (out.acceptedByFitAnchor && out.acceptedByDirectAnchor)
                out.source = QStringLiteral("DUAL_DIRECT_BOTH_ANCHORS");
            else if (out.acceptedByDirectAnchor)
                out.source = QStringLiteral("DUAL_DIRECT_DIRECT_ANCHOR");
            else if (out.acceptedByFitAnchor)
                out.source = QStringLiteral("DUAL_DIRECT_FIT_ANCHOR");
            else if (rawCorroborated)
                out.source = QStringLiteral("DUAL_DIRECT_RAW_CORROBORATED");
            else
                out.source = QStringLiteral("DUAL_DIRECT_BOOTSTRAP");
            m_direct.resetTo(direct);
            return out;
        }
        out.source = QStringLiteral("DUAL_DIRECT_REJECTED");
        return out;
    }

private:
    PolarityIdentity m_identity = PolarityIdentity::Unknown;
    Track m_fit;
    Track m_direct;

    void resetIfIdentityChanged(PolarityIdentity identity)
    {
        if (identity == m_identity) return;
        m_identity = identity;
        m_fit.reset();
        m_direct.reset();
    }
    void updateDirectIfContinuous(double direct, double predicted)
    {
        if (!std::isfinite(direct)) return;
        if (!std::isfinite(predicted) || std::abs(direct - predicted) <= 4.0)
            m_direct.update(direct);
    }
};

struct CycleDecision
{
    double center = std::numeric_limits<double>::quiet_NaN();
    QString source = QStringLiteral("UNAVAILABLE");
    double rawDirect = std::numeric_limits<double>::quiet_NaN();
    bool rejectedCycleSlip = false;
    bool acceptedRapidMove = false;
};

class SamePolarityCycleGate
{
    struct Track {
        double previous = std::numeric_limits<double>::quiet_NaN();
        double last = std::numeric_limits<double>::quiet_NaN();
        double predicted() const
        {
            if (!std::isfinite(last)) return std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(previous)) return last;
            return last + std::clamp(last - previous, -3.0, 3.0);
        }
        void update(double v) { if (std::isfinite(v)) { previous = last; last = v; } }
        void reset() { previous = last = std::numeric_limits<double>::quiet_NaN(); }
        void resetTo(double v) { reset(); update(v); }
    };

public:
    CycleDecision resolve(const QVariantMap &r,
                          const PolarityDecision &selector,
                          const DualAnchorDecision &dual)
    {
        CycleDecision out;
        resetIfIdentityChanged(selector.identity);
        if (selector.identity == PolarityIdentity::Unknown) {
            out.source = QStringLiteral("CYCLE_IDENTITY_UNKNOWN");
            return out;
        }

        const QString key = selector.identity == PolarityIdentity::Positive
            ? QStringLiteral("positivePolarityDirectPeakIndex")
            : QStringLiteral("negativePolarityDirectPeakIndex");
        const double identityDirect = finiteNumber(r, key);
        out.rawDirect = finiteNumber(r, QStringLiteral("directParabolicPeakIndex"));
        const double candidate = dual.center;
        if (!std::isfinite(candidate)) {
            out.source = dual.source;
            return out;
        }

        const double predicted = m_cycle.predicted();
        const bool near = !std::isfinite(predicted) || std::abs(candidate - predicted) <= 6.0;
        if (near) {
            out.center = candidate;
            out.source = QStringLiteral("CYCLE_ACCEPT_CONTINUOUS_") + dual.source;
            m_cycle.update(candidate);
            return out;
        }

        const bool rawAvailable = std::isfinite(out.rawDirect);
        const bool candidateRawAgree = rawAvailable && std::abs(candidate - out.rawDirect) <= 3.0;
        const bool identityDirectRawAgree = std::isfinite(identityDirect) && rawAvailable
            && std::abs(identityDirect - out.rawDirect) <= 1.5;
        const bool identityDirectNear = std::isfinite(identityDirect) && std::isfinite(predicted)
            && std::abs(identityDirect - predicted) <= 6.0;

        if (dual.source == QStringLiteral("IDENTITY_FIT")) {
            if (rawAvailable && std::abs(candidate - out.rawDirect) > 5.0
                && identityDirectRawAgree && identityDirectNear) {
                out.center = identityDirect;
                out.source = QStringLiteral("CYCLE_RECOVER_IDENTITY_DIRECT_FROM_FIT_SLIP");
                m_cycle.update(out.center);
                return out;
            }
            if (candidateRawAgree) {
                if (isRecoveryTrackingState(r)) {
                    out.rejectedCycleSlip = true;
                    out.source = QStringLiteral("CYCLE_REJECT_RAPID_FIT_RAW_RECOVERY_PENDING");
                    return out;
                }
                out.center = candidate;
                out.source = QStringLiteral("CYCLE_ACCEPT_RAPID_FIT_RAW");
                out.acceptedRapidMove = true;
                m_cycle.resetTo(candidate);
                return out;
            }
            out.rejectedCycleSlip = true;
            out.source = QStringLiteral("CYCLE_REJECT_FIT_SLIP");
            return out;
        }

        if (dual.acceptedByFitAnchor) {
            out.center = candidate;
            out.source = QStringLiteral("CYCLE_ACCEPT_DIRECT_FIT_ANCHOR");
            m_cycle.resetTo(candidate);
            return out;
        }
        if (candidateRawAgree) {
            if (isRecoveryTrackingState(r)) {
                out.rejectedCycleSlip = true;
                out.source = QStringLiteral("CYCLE_REJECT_RAPID_DIRECT_RAW_RECOVERY_PENDING");
                return out;
            }
            out.center = candidate;
            out.source = QStringLiteral("CYCLE_ACCEPT_RAPID_DIRECT_RAW");
            out.acceptedRapidMove = true;
            m_cycle.resetTo(candidate);
            return out;
        }
        out.rejectedCycleSlip = true;
        out.source = QStringLiteral("CYCLE_REJECT_DIRECT_AMBIGUOUS");
        return out;
    }

private:
    PolarityIdentity m_identity = PolarityIdentity::Unknown;
    Track m_cycle;

    void resetIfIdentityChanged(PolarityIdentity identity)
    {
        if (identity == m_identity) return;
        m_identity = identity;
        m_cycle.reset();
    }

    static bool isRecoveryTrackingState(const QVariantMap &r)
    {
        const QString tracking = r.value(QStringLiteral("trackingState")).toString().toUpper();
        return tracking == QStringLiteral("JUMP_PENDING")
            || tracking.startsWith(QStringLiteral("BACKFILLED_"))
            || tracking.contains(QStringLiteral("_RECOVERED"));
    }
};

struct ShiftDecision
{
    double center = std::numeric_limits<double>::quiet_NaN();
    QString source = QStringLiteral("UNAVAILABLE");
    bool backfillPrevious = false;
    bool backfillUsedRaw = false;
    QString backfillFile;
    double backfillCenter = std::numeric_limits<double>::quiet_NaN();
};

class PendingDeferredConfirmer
{
    struct Track {
        double previous = std::numeric_limits<double>::quiet_NaN();
        double last = std::numeric_limits<double>::quiet_NaN();
        double predicted() const
        {
            if (!std::isfinite(last)) return std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(previous)) return last;
            return last + std::clamp(last - previous, -3.0, 3.0);
        }
        void update(double v) { if (std::isfinite(v)) { previous = last; last = v; } }
        void reset() { previous = last = std::numeric_limits<double>::quiet_NaN(); }
        void resetTo(double v) { reset(); update(v); }
    };

public:
    ShiftDecision resolve(const QVariantMap &r,
                          const PolarityDecision &selector,
                          const DualAnchorDecision &dual,
                          const CycleDecision &cycle)
    {
        ShiftDecision out;
        resetIfIdentityChanged(selector.identity);
        if (selector.identity == PolarityIdentity::Unknown) {
            out.source = QStringLiteral("CYCLE_SHIFT_IDENTITY_UNKNOWN");
            return out;
        }

        const QString file = r.value(QStringLiteral("fileName")).toString();
        const double candidate = dual.center;
        const double predicted = m_confirmed.predicted();

        if (m_pending) {
            const Reliability reliability = confirmationReliability(r, dual, cycle);
            if (!reliability.reliable) {
                ++m_deferredCount;
                out.source = QStringLiteral("CYCLE_SHIFT_DEFERRED_") + reliability.reason;
                if (std::isfinite(cycle.center))
                    out.center = cycle.center;
                if (m_deferredCount <= 2)
                    return out;
                out.source = QStringLiteral("CYCLE_SHIFT_DEFERRED_TIMEOUT_UNRESOLVED");
                clearPending();
                return out;
            }

            const bool sameNewCycle = std::isfinite(candidate)
                && std::abs(candidate - m_pendingCenter) <= 4.0;
            if (sameNewCycle) {
                out.center = std::isfinite(cycle.center) ? cycle.center : candidate;
                out.source = QStringLiteral("CYCLE_SHIFT_CONFIRMED");
                out.backfillPrevious = true;
                out.backfillFile = m_pendingFile;
                out.backfillCenter = m_pendingCenter;
                if (std::isfinite(m_pendingRaw)
                    && std::abs(m_pendingRaw - out.center) <= 3.0) {
                    out.backfillCenter = m_pendingRaw;
                    out.backfillUsedRaw = true;
                }
                clearPending();
                m_confirmed.resetTo(out.center);
                m_confirmedShiftActive = true;
                return out;
            }
            clearPending();
        }

        if (std::isfinite(cycle.center)) {
            out.center = cycle.center;
            out.source = QStringLiteral("CYCLE_SHIFT_PASS_") + cycle.source;
            if (cycle.acceptedRapidMove)
                m_confirmed.resetTo(out.center);
            else
                updateConfirmed(out.center);
            m_confirmedShiftActive = false;
            return out;
        }
        if (!cycle.rejectedCycleSlip || !std::isfinite(candidate)) {
            out.source = cycle.source;
            return out;
        }

        if (m_confirmedShiftActive && std::isfinite(predicted)
            && std::abs(candidate - predicted) <= 6.0) {
            out.center = candidate;
            out.source = QStringLiteral("CYCLE_SHIFT_CONFIRMED_TRACK_HOLD");
            updateConfirmed(out.center);
            return out;
        }

        m_pending = true;
        m_pendingCenter = candidate;
        m_pendingRaw = cycle.rawDirect;
        m_pendingFile = file;
        m_deferredCount = 0;
        out.source = QStringLiteral("CYCLE_SHIFT_PENDING");
        return out;
    }

private:
    struct Reliability { bool reliable = true; QString reason = QStringLiteral("RELIABLE"); };
    PolarityIdentity m_identity = PolarityIdentity::Unknown;
    Track m_confirmed;
    bool m_pending = false;
    double m_pendingCenter = std::numeric_limits<double>::quiet_NaN();
    double m_pendingRaw = std::numeric_limits<double>::quiet_NaN();
    QString m_pendingFile;
    int m_deferredCount = 0;
    bool m_confirmedShiftActive = false;

    Reliability confirmationReliability(const QVariantMap &r,
                                        const DualAnchorDecision &dual,
                                        const CycleDecision &cycle) const
    {
        Reliability out;
        if (!std::isfinite(dual.center)) {
            out.reliable = false;
            out.reason = QStringLiteral("NO_DUAL_CENTER");
            return out;
        }
        const QString tracking = r.value(QStringLiteral("trackingState")).toString().toUpper();
        if (tracking.contains(QStringLiteral("JUMP_PENDING"))) {
            out.reliable = false;
            out.reason = QStringLiteral("TRACKING_JUMP_PENDING");
            return out;
        }
        if (tracking.contains(QStringLiteral("FRAGMENTED_RECOVERED"))) {
            out.reliable = false;
            out.reason = QStringLiteral("TRACKING_FRAGMENTED_RECOVERED");
            return out;
        }
        if (tracking.contains(QStringLiteral("TEMPLATE_ONLY_RECOVERED"))) {
            out.reliable = false;
            out.reason = QStringLiteral("TRACKING_TEMPLATE_ONLY_RECOVERED");
            return out;
        }
        if (std::isfinite(cycle.rawDirect) && std::abs(dual.center - cycle.rawDirect) > 6.0) {
            out.reliable = false;
            out.reason = QStringLiteral("MULTI_EVIDENCE_CONFLICT");
        }
        return out;
    }

    void resetIfIdentityChanged(PolarityIdentity identity)
    {
        if (identity == m_identity) return;
        m_identity = identity;
        m_confirmed.reset();
        clearPending();
        m_confirmedShiftActive = false;
    }
    void clearPending()
    {
        m_pending = false;
        m_pendingCenter = std::numeric_limits<double>::quiet_NaN();
        m_pendingRaw = std::numeric_limits<double>::quiet_NaN();
        m_pendingFile.clear();
        m_deferredCount = 0;
    }
    void updateConfirmed(double center)
    {
        const double predicted = m_confirmed.predicted();
        if (!std::isfinite(predicted) || std::abs(center - predicted) <= 6.0)
            m_confirmed.update(center);
        else
            m_confirmed.resetTo(center);
    }
};

struct OutputDecision
{
    double center = std::numeric_limits<double>::quiet_NaN();
    QString source;
    bool backfillPrevious = false;
    QString backfillFile;
    double backfillCenter = std::numeric_limits<double>::quiet_NaN();
    bool backfillUsedRaw = false;
};

class CandidateTofOutputChain
{
public:
    OutputDecision process(const QVariantMap &r)
    {
        const PolarityDecision polarity = m_selector.observe(r);
        const SingleAnchorDecision single = m_single.resolve(r, polarity);
        const DualAnchorDecision dual = m_dual.resolve(r, polarity, single);
        const CycleDecision cycle = m_cycle.resolve(r, polarity, dual);
        const ShiftDecision shift = m_shift.resolve(r, polarity, dual, cycle);

        OutputDecision out;
        out.center = shift.center;
        out.source = shift.source;
        out.backfillPrevious = shift.backfillPrevious;
        out.backfillFile = shift.backfillFile;
        out.backfillCenter = shift.backfillCenter;
        out.backfillUsedRaw = shift.backfillUsedRaw;
        return out;
    }

private:
    DualPolarityIdentitySelector m_selector;
    SingleAnchorFallback m_single;
    DualAnchorFallback m_dual;
    SamePolarityCycleGate m_cycle;
    PendingDeferredConfirmer m_shift;
};

} // namespace CandidateTof
