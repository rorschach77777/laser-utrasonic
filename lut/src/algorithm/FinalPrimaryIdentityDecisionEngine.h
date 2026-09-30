#pragma once

#include "EvidenceFusion.h"
#include "IdentityOffsetModel.h"
#include "TofEvidenceFrame.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace FinalPrimary {

struct Decision
{
    double outputCenter = std::numeric_limits<double>::quiet_NaN();
    double finalCenter = std::numeric_limits<double>::quiet_NaN();
    double candidateCenter = std::numeric_limits<double>::quiet_NaN();
    double rawOffset = std::numeric_limits<double>::quiet_NaN();
    double offsetBaseline = std::numeric_limits<double>::quiet_NaN();
    double offsetSigma = std::numeric_limits<double>::quiet_NaN();
    double residual = std::numeric_limits<double>::quiet_NaN();
    double anomalyThreshold = std::numeric_limits<double>::quiet_NaN();
    double alignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();
    double predictedFinalCenter = std::numeric_limits<double>::quiet_NaN();
    double finalTemporalResidual = std::numeric_limits<double>::quiet_NaN();
    double alignedTemporalResidual = std::numeric_limits<double>::quiet_NaN();
    double predictedAlignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();
    double alignedCandidateTrackResidual = std::numeric_limits<double>::quiet_NaN();
    int historyCount = 0;
    int motionSupportingGroups = 0;
    int envelopeEnergySupportCount = 0;
    int localWaveSupportCount = 0;
    int identitySupportCount = 0;
    bool modelReady = false;
    bool corrected = false;
    bool temporalCorrection = false;
    bool learnedThisFrame = false;
    bool physicalMotionProtected = false;
    bool envelopeEnergyGroupSupport = false;
    bool localWaveGroupSupport = false;
    bool identityGroupSupport = false;
    bool correctionEvidencePassed = false;
    bool correctionEnvelopeSupport = false;
    bool correctionLocalWaveSupport = false;
    bool correctionEnvelopeFinalSupport = false;
    bool correctionLocalWaveFinalSupport = false;
    QString motionEvidenceSummary;
    QString correctionEvidenceSummary;
    QString confidenceClass = QStringLiteral("FINAL_NORMAL");
    QString reason = QStringLiteral("FINAL_PRIMARY");
};

class DecisionEngine
{
public:
    Decision resolve(const TofEvidenceFrame &frame,
                     double candidateCenter,
                     const QString &candidateSource)
    {
        const IdentityOffsetAlignmentDecision alignment =
            m_offsetModel.inspect(frame, candidateCenter, candidateSource);

        Decision out;
        applyOffsetAlignment(alignment, out);
        out.outputCenter = frame.finalTof;
        out.predictedFinalCenter = predictedOutputCenter();

        if (!std::isfinite(frame.finalTof)) {
            out.reason = QStringLiteral("FINAL_UNAVAILABLE");
            resetOutputTrack();
            m_evidenceFusion.observe(frame);
            return out;
        }
        if (std::isfinite(out.predictedFinalCenter))
            out.finalTemporalResidual = frame.finalTof - out.predictedFinalCenter;

        if (alignment.identityChanged)
            resetIdentityDecisionState();

        const DualPolarityIdentity identity = frame.identity.selectedIdentity;
        if (identity == DualPolarityIdentity::Unknown) {
            if (std::isfinite(out.finalTemporalResidual)
                && std::abs(out.finalTemporalResidual) >= kCandidateUnavailableLargeJumpSamples) {
                out.confidenceClass = QStringLiteral("AMBIGUOUS");
                out.reason = QStringLiteral("IDENTITY_UNKNOWN_LARGE_FINAL_JUMP_AMBIGUOUS");
            } else {
                out.reason = QStringLiteral("IDENTITY_UNKNOWN_FINAL_ONLY");
            }
            updateOutputTrack(out.outputCenter);
            m_evidenceFusion.observe(frame);
            return out;
        }

        if (!std::isfinite(candidateCenter)) {
            if (std::isfinite(out.finalTemporalResidual)
                && std::abs(out.finalTemporalResidual) >= kCandidateUnavailableLargeJumpSamples) {
                applyPhysicalMotionEvidence(
                    m_evidenceFusion.assessPhysicalMotion(
                        frame, out.finalTemporalResidual, out.alignedTemporalResidual), out);
                if (out.physicalMotionProtected) {
                    out.confidenceClass = QStringLiteral("PHYSICAL_MOTION");
                    out.reason = QStringLiteral("CANDIDATE_UNAVAILABLE_PHYSICAL_MOTION_SUPPORTED");
                } else {
                    out.confidenceClass = QStringLiteral("AMBIGUOUS");
                    out.reason = QStringLiteral("CANDIDATE_UNAVAILABLE_LARGE_FINAL_JUMP_AMBIGUOUS");
                }
            } else {
                out.reason = QStringLiteral("CANDIDATE_UNAVAILABLE_FINAL_ONLY");
            }
            updateOutputTrack(out.outputCenter);
            m_evidenceFusion.observe(frame);
            return out;
        }

        if (alignment.learningPhase) {
            out.reason = out.modelReady
                ? QStringLiteral("OFFSET_MODEL_JUST_READY_FINAL_ONLY")
                : QStringLiteral("OFFSET_MODEL_LEARNING_FINAL_ONLY");
            updateOutputTrack(out.outputCenter);
            m_evidenceFusion.observe(frame);
            return out;
        }

        if (std::isfinite(out.predictedFinalCenter) && std::isfinite(out.alignedCandidateCenter))
            out.alignedTemporalResidual = out.alignedCandidateCenter - out.predictedFinalCenter;
        out.predictedAlignedCandidateCenter = predictedAlignedCandidateCenter();
        if (std::isfinite(out.predictedAlignedCandidateCenter)
            && std::isfinite(out.alignedCandidateCenter)) {
            out.alignedCandidateTrackResidual =
                out.alignedCandidateCenter - out.predictedAlignedCandidateCenter;
        }

        const bool temporalIdentityMismatch = std::abs(out.residual) >= kModerateResidualSamples
            && std::isfinite(out.finalTemporalResidual)
            && std::isfinite(out.alignedTemporalResidual)
            && std::abs(out.finalTemporalResidual) >= kTemporalFinalLargeResidualSamples
            && std::abs(out.alignedTemporalResidual) <= kTemporalAlignedMaxResidualSamples
            && std::abs(out.alignedTemporalResidual) + kTemporalImprovementMarginSamples
                < std::abs(out.finalTemporalResidual);

        const bool strongResidualNearGateMismatch = std::isfinite(out.anomalyThreshold)
            && std::abs(out.residual) >= out.anomalyThreshold
            && std::isfinite(out.finalTemporalResidual)
            && std::isfinite(out.alignedTemporalResidual)
            && std::abs(out.finalTemporalResidual) >= kStrongResidualExtendedFinalMinSamples
            && std::abs(out.alignedTemporalResidual) > kTemporalAlignedMaxResidualSamples
            && std::abs(out.alignedTemporalResidual) <= kStrongResidualExtendedAlignedMaxSamples
            && std::abs(out.alignedTemporalResidual) + kTemporalImprovementMarginSamples
                < std::abs(out.finalTemporalResidual);

        const bool alignedIdentityTrackMismatch = std::abs(out.residual) >= kModerateResidualSamples
            && std::isfinite(out.finalTemporalResidual)
            && std::isfinite(out.alignedCandidateTrackResidual)
            && std::abs(out.finalTemporalResidual) >= kCandidateTrackFinalLargeResidualSamples
            && std::abs(out.alignedCandidateTrackResidual) <= kCandidateTrackAlignedMaxResidualSamples
            && std::abs(out.alignedCandidateTrackResidual) + kCandidateTrackImprovementMarginSamples
                < std::abs(out.finalTemporalResidual);

        if (temporalIdentityMismatch || strongResidualNearGateMismatch || alignedIdentityTrackMismatch) {
            applyPhysicalMotionEvidence(
                m_evidenceFusion.assessPhysicalMotion(
                    frame, out.finalTemporalResidual, out.alignedTemporalResidual), out);
            if (out.physicalMotionProtected) {
                out.confidenceClass = QStringLiteral("PHYSICAL_MOTION");
                out.reason = QStringLiteral("PHYSICAL_MOTION_SUPPORTED_FINAL_ONLY");
                updateAlignedCandidateTrack(out.alignedCandidateCenter);
                updateOutputTrack(out.outputCenter);
                m_evidenceFusion.observe(frame);
                return out;
            }

            applyCorrectionEvidence(
                m_evidenceFusion.assessCorrection(frame, out.finalCenter, out.alignedCandidateCenter), out);
            if (!out.correctionEvidencePassed) {
                out.confidenceClass = QStringLiteral("AMBIGUOUS");
                out.reason = QStringLiteral("CORRECTION_EVIDENCE_INSUFFICIENT_FINAL_ONLY");
                updateAlignedCandidateTrack(out.alignedCandidateCenter);
                updateOutputTrack(out.outputCenter);
                m_evidenceFusion.observe(frame);
                return out;
            }

            const bool strongResidual = std::abs(out.residual) >= out.anomalyThreshold;
            const bool directBackedCandidate = candidateSource.contains(QStringLiteral("DUAL_DIRECT"));
            const bool dualIndependentCandidateSupport = out.correctionEnvelopeSupport
                && out.correctionLocalWaveSupport
                && !out.correctionEnvelopeFinalSupport
                && !out.correctionLocalWaveFinalSupport;

            if (!strongResidual && !directBackedCandidate && !dualIndependentCandidateSupport) {
                out.confidenceClass = QStringLiteral("AMBIGUOUS");
                out.reason = QStringLiteral("MODERATE_FIT_CORRECTION_DEFERRED_FINAL_ONLY");
                updateAlignedCandidateTrack(out.alignedCandidateCenter);
                updateOutputTrack(out.outputCenter);
                m_evidenceFusion.observe(frame);
                return out;
            }

            const double alignedResidualAbs = std::isfinite(out.alignedTemporalResidual)
                ? std::abs(out.alignedTemporalResidual)
                : std::numeric_limits<double>::infinity();
            const bool extremeResidual = strongResidual
                && std::abs(out.residual) >= kExtremeResidualMultiplier * out.anomalyThreshold;
            const bool strongCorrectionConfidence = strongResidual
                && alignedResidualAbs <= kStrongCorrectionMaxAlignedResidualSamples;
            const bool moderateDirectCorrectionConfidence = !strongResidual
                && directBackedCandidate
                && alignedResidualAbs <= kModerateDirectMaxAlignedResidualSamples;
            const bool moderateDualEvidenceFitConfidence = !strongResidual
                && dualIndependentCandidateSupport
                && alignedResidualAbs <= kModerateDirectMaxAlignedResidualSamples;
            const bool extremeCorrectionConfidence = extremeResidual
                && alignedResidualAbs <= kTemporalAlignedMaxResidualSamples;
            if (!(strongCorrectionConfidence || moderateDirectCorrectionConfidence
                  || moderateDualEvidenceFitConfidence || extremeCorrectionConfidence)) {
                out.confidenceClass = QStringLiteral("AMBIGUOUS");
                out.reason = QStringLiteral("CORRECTION_CONFIDENCE_INSUFFICIENT_FINAL_ONLY");
                updateAlignedCandidateTrack(out.alignedCandidateCenter);
                updateOutputTrack(out.outputCenter);
                m_evidenceFusion.observe(frame);
                return out;
            }

            out.outputCenter = out.alignedCandidateCenter;
            out.corrected = true;
            out.confidenceClass = QStringLiteral("IDENTITY_CORRECTED");
            out.temporalCorrection = true;
            if (alignedIdentityTrackMismatch && !temporalIdentityMismatch) {
                out.reason = std::abs(out.residual) >= out.anomalyThreshold
                    ? QStringLiteral("IDENTITY_STRONG_RESIDUAL_CANDIDATE_TRACK_CONFIRMED")
                    : QStringLiteral("IDENTITY_MODERATE_RESIDUAL_CANDIDATE_TRACK_CONFIRMED");
            } else {
                out.reason = std::abs(out.residual) >= out.anomalyThreshold
                    ? QStringLiteral("IDENTITY_STRONG_RESIDUAL_TEMPORAL_CONFIRMED")
                    : QStringLiteral("IDENTITY_MODERATE_RESIDUAL_TEMPORAL_CONFIRMED");
            }
            updateAlignedCandidateTrack(out.alignedCandidateCenter);
            updateOutputTrack(out.outputCenter);
            m_evidenceFusion.observe(frame);
            return out;
        }

        const IdentityOffsetAlignmentDecision committedAlignment = m_offsetModel.commitNormal(alignment);
        if (committedAlignment.learnedThisFrame)
            applyOffsetAlignment(committedAlignment, out);
        out.reason = QStringLiteral("IDENTITY_NORMAL_FINAL_PRIMARY");
        updateAlignedCandidateTrack(out.alignedCandidateCenter);
        updateOutputTrack(out.outputCenter);
        m_evidenceFusion.observe(frame);
        return out;
    }

    void reset() { *this = DecisionEngine{}; }

private:
    static constexpr double kModerateResidualSamples = 3.5;
    static constexpr double kTemporalFinalLargeResidualSamples = 5.0;
    static constexpr double kTemporalAlignedMaxResidualSamples = 6.0;
    static constexpr double kStrongResidualExtendedAlignedMaxSamples = 7.0;
    static constexpr double kStrongResidualExtendedFinalMinSamples = 10.0;
    static constexpr double kTemporalImprovementMarginSamples = 2.0;
    static constexpr double kCandidateTrackFinalLargeResidualSamples = 8.0;
    static constexpr double kCandidateTrackAlignedMaxResidualSamples = 6.0;
    static constexpr double kCandidateTrackImprovementMarginSamples = 3.0;
    static constexpr double kOutputTrackMaxVelocitySamples = 3.0;
    static constexpr double kAlignedCandidateTrackMaxVelocitySamples = 3.0;
    static constexpr double kCandidateUnavailableLargeJumpSamples = 8.0;
    static constexpr double kStrongCorrectionMaxAlignedResidualSamples = 4.0;
    static constexpr double kModerateDirectMaxAlignedResidualSamples = 2.0;
    static constexpr double kExtremeResidualMultiplier = 1.75;

    IdentityOffsetModel m_offsetModel;
    EvidenceFusion m_evidenceFusion;
    double m_previousOutputCenter = std::numeric_limits<double>::quiet_NaN();
    double m_lastOutputCenter = std::numeric_limits<double>::quiet_NaN();
    double m_previousAlignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();
    double m_lastAlignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();

    double predictedOutputCenter() const
    {
        if (!std::isfinite(m_lastOutputCenter)) return std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(m_previousOutputCenter)) return m_lastOutputCenter;
        const double velocity = std::clamp(m_lastOutputCenter - m_previousOutputCenter,
            -kOutputTrackMaxVelocitySamples, kOutputTrackMaxVelocitySamples);
        return m_lastOutputCenter + velocity;
    }

    double predictedAlignedCandidateCenter() const
    {
        if (!std::isfinite(m_lastAlignedCandidateCenter)) return std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(m_previousAlignedCandidateCenter)) return m_lastAlignedCandidateCenter;
        const double velocity = std::clamp(m_lastAlignedCandidateCenter - m_previousAlignedCandidateCenter,
            -kAlignedCandidateTrackMaxVelocitySamples, kAlignedCandidateTrackMaxVelocitySamples);
        return m_lastAlignedCandidateCenter + velocity;
    }

    static void applyOffsetAlignment(const IdentityOffsetAlignmentDecision &alignment, Decision &out)
    {
        out.finalCenter = alignment.finalCenter;
        out.candidateCenter = alignment.candidateCenter;
        out.rawOffset = alignment.rawOffset;
        out.offsetBaseline = alignment.offsetBaseline;
        out.offsetSigma = alignment.offsetSigma;
        out.residual = alignment.residual;
        out.anomalyThreshold = alignment.anomalyThreshold;
        out.alignedCandidateCenter = alignment.alignedCandidateCenter;
        out.historyCount = alignment.historyCount;
        out.modelReady = alignment.modelReady;
        out.learnedThisFrame = alignment.learnedThisFrame;
    }

    static void applyPhysicalMotionEvidence(const PhysicalMotionEvidenceDecision &evidence, Decision &out)
    {
        out.motionSupportingGroups = evidence.supportingGroups;
        out.envelopeEnergySupportCount = evidence.envelopeEnergySupportCount;
        out.localWaveSupportCount = evidence.localWaveSupportCount;
        out.identitySupportCount = evidence.identitySupportCount;
        out.physicalMotionProtected = evidence.protectedMotion;
        out.envelopeEnergyGroupSupport = evidence.envelopeEnergyGroupSupport;
        out.localWaveGroupSupport = evidence.localWaveGroupSupport;
        out.identityGroupSupport = evidence.identityGroupSupport;
        out.motionEvidenceSummary = evidence.summary;
    }

    static void applyCorrectionEvidence(const CorrectionEvidenceDecision &evidence, Decision &out)
    {
        out.correctionEvidencePassed = evidence.passed;
        out.correctionEnvelopeSupport = evidence.envelopeCandidateSupport;
        out.correctionLocalWaveSupport = evidence.localWaveCandidateSupport;
        out.correctionEnvelopeFinalSupport = evidence.envelopeFinalSupport;
        out.correctionLocalWaveFinalSupport = evidence.localWaveFinalSupport;
        out.correctionEvidenceSummary = evidence.summary;
    }

    void updateAlignedCandidateTrack(double center)
    {
        if (!std::isfinite(center)) return;
        m_previousAlignedCandidateCenter = m_lastAlignedCandidateCenter;
        m_lastAlignedCandidateCenter = center;
    }

    void updateOutputTrack(double center)
    {
        if (!std::isfinite(center)) {
            resetOutputTrack();
            return;
        }
        m_previousOutputCenter = m_lastOutputCenter;
        m_lastOutputCenter = center;
    }

    void resetOutputTrack()
    {
        m_previousOutputCenter = std::numeric_limits<double>::quiet_NaN();
        m_lastOutputCenter = std::numeric_limits<double>::quiet_NaN();
    }

    void resetIdentityDecisionState()
    {
        m_previousAlignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();
        m_lastAlignedCandidateCenter = std::numeric_limits<double>::quiet_NaN();
        m_evidenceFusion.reset();
    }
};

} // namespace FinalPrimary
