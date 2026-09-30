#pragma once

#include "RobustOffsetTracker.h"
#include "TofEvidenceFrame.h"

#include <QString>

#include <cmath>
#include <limits>

struct IdentityOffsetAlignmentDecision
{
    double finalCenter = std::numeric_limits<double>::quiet_NaN();
    double candidateCenter = std::numeric_limits<double>::quiet_NaN();
    double rawOffset = std::numeric_limits<double>::quiet_NaN();
    double offsetBaseline = std::numeric_limits<double>::quiet_NaN();
    double offsetSigma = std::numeric_limits<double>::quiet_NaN();
    double residual = std::numeric_limits<double>::quiet_NaN();
    double anomalyThreshold = std::numeric_limits<double>::quiet_NaN();
    double alignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();

    int historyCount = 0;
    bool modelReady = false;
    bool learnedThisFrame = false;
    bool stableTrainingFrame = false;
    bool learningPhase = false;
    bool identityChanged = false;
};

class IdentityOffsetModel
{
public:
    IdentityOffsetModel()
        : m_tracker(trackerConfig())
    {
    }

    IdentityOffsetAlignmentDecision inspect(const TofEvidenceFrame &frame,
                                            double candidateCenter,
                                            const QString &candidateSource)
    {
        IdentityOffsetAlignmentDecision out;
        out.finalCenter = frame.finalTof;
        out.candidateCenter = candidateCenter;
        if (!std::isfinite(out.finalCenter))
            return out;

        const DualPolarityIdentity identity = frame.identity.selectedIdentity;
        if (identity != m_identity) {
            m_identity = identity;
            m_tracker.reset();
            out.identityChanged = true;
        }
        if (identity == DualPolarityIdentity::Unknown || !std::isfinite(candidateCenter))
            return out;

        out.rawOffset = candidateCenter - out.finalCenter;
        out.stableTrainingFrame = isStableTrainingFrame(frame, candidateSource);
        out.learningPhase = !m_tracker.ready();
        if (out.learningPhase && out.stableTrainingFrame)
            out.learnedThisFrame = m_tracker.update(out.rawOffset);
        fillSnapshot(out);
        return out;
    }

    IdentityOffsetAlignmentDecision commitNormal(const IdentityOffsetAlignmentDecision &inspected)
    {
        IdentityOffsetAlignmentDecision out = inspected;
        if (!out.modelReady || !out.stableTrainingFrame || !std::isfinite(out.rawOffset))
            return out;
        if (m_tracker.update(out.rawOffset)) {
            out.learnedThisFrame = true;
            fillSnapshot(out);
        }
        return out;
    }

private:
    static constexpr int kHistorySize = 31;
    static constexpr int kMinHistory = 9;
    static constexpr double kBootstrapMaxAbsOffsetSamples = 10.0;
    static constexpr double kSigmaFloorSamples = 0.35;
    static constexpr double kAnomalyFloorSamples = 5.0;
    static constexpr double kAnomalyCeilingSamples = 7.0;
    static constexpr double kAnomalySigmaMultiplier = 6.0;
    static constexpr double kUpdateFloorSamples = 2.0;
    static constexpr double kUpdateCeilingSamples = 4.0;
    static constexpr double kUpdateSigmaMultiplier = 4.0;

    DualPolarityIdentity m_identity = DualPolarityIdentity::Unknown;
    RobustOffsetTracker m_tracker;

    static RobustOffsetTracker::Config trackerConfig()
    {
        RobustOffsetTracker::Config config;
        config.historySize = kHistorySize;
        config.minimumHistory = kMinHistory;
        config.bootstrapMaxAbsOffset = kBootstrapMaxAbsOffsetSamples;
        config.sigmaFloor = kSigmaFloorSamples;
        config.updateSigmaMultiplier = kUpdateSigmaMultiplier;
        config.updateFloor = kUpdateFloorSamples;
        config.updateCeiling = kUpdateCeilingSamples;
        return config;
    }

    static double anomalyThreshold(double sigmaValue)
    {
        return RobustOffsetTracker::robustGate(
            sigmaValue, kSigmaFloorSamples, kAnomalySigmaMultiplier,
            kAnomalyFloorSamples, kAnomalyCeilingSamples);
    }

    static bool isStableTrainingFrame(const TofEvidenceFrame &frame,
                                      const QString &candidateSource)
    {
        if (frame.trackingState.compare(QStringLiteral("LOCK"), Qt::CaseInsensitive) != 0)
            return false;
        if (candidateSource.contains(QStringLiteral("PENDING"))
            || candidateSource.contains(QStringLiteral("DEFERRED"))
            || candidateSource.contains(QStringLiteral("UNRESOLVED"))
            || candidateSource.contains(QStringLiteral("ISOLATED")))
            return false;
        return true;
    }

    void fillSnapshot(IdentityOffsetAlignmentDecision &out) const
    {
        const RobustOffsetTracker::Snapshot snapshot = m_tracker.inspect(out.rawOffset);
        out.historyCount = snapshot.count;
        out.modelReady = snapshot.ready;
        if (!out.modelReady)
            return;
        out.offsetBaseline = snapshot.baseline;
        out.offsetSigma = snapshot.sigma;
        out.residual = snapshot.residual;
        if (std::isfinite(out.offsetBaseline)) {
            out.alignedCandidateCenter = out.candidateCenter - out.offsetBaseline;
            out.anomalyThreshold = anomalyThreshold(out.offsetSigma);
        }
    }
};
