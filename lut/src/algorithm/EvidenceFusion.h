#pragma once

#include "TofEvidenceFrame.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

struct PhysicalMotionEvidenceDecision
{
    int supportingGroups = 0;
    int envelopeEnergySupportCount = 0;
    int localWaveSupportCount = 0;
    int identitySupportCount = 0;
    bool protectedMotion = false;
    bool envelopeEnergyGroupSupport = false;
    bool localWaveGroupSupport = false;
    bool identityGroupSupport = false;
    QString summary;
};

struct CorrectionEvidenceDecision
{
    bool passed = false;
    bool envelopeCandidateSupport = false;
    bool localWaveCandidateSupport = false;
    bool envelopeFinalSupport = false;
    bool localWaveFinalSupport = false;
    QString summary;
};

class EvidenceFusion
{
public:
    PhysicalMotionEvidenceDecision assessPhysicalMotion(
        const TofEvidenceFrame &frame,
        double finalTemporalResidual,
        double alignedTemporalResidual) const
    {
        PhysicalMotionEvidenceDecision out;
        if (!std::isfinite(finalTemporalResidual)
            || std::abs(finalTemporalResidual) < kTemporalFinalLargeResidualSamples)
            return out;

        EvidenceGroupResult envelopeEnergy;
        addMotionVote(frame.hilbertEnvelopePeakTof, m_hilbertTrack,
                      finalTemporalResidual, kMotionEvidenceMinResidualSamples, envelopeEnergy);
        addMotionVote(frame.energyMedianT50Tof, m_t50Track,
                      finalTemporalResidual, kMotionEvidenceMinResidualSamples, envelopeEnergy);
        envelopeEnergy.supported = envelopeEnergy.support >= 1 && envelopeEnergy.oppose == 0;

        EvidenceGroupResult localWave;
        addMotionVote(frame.rawDirectTof, m_rawDirectTrack,
                      finalTemporalResidual, kMotionEvidenceMinResidualSamples, localWave);
        addMotionVote(frame.cfd30Tof, m_cfdTrack,
                      finalTemporalResidual, kMotionEvidenceMinResidualSamples, localWave);
        addMotionVote(frame.leadingEdgeFitTof, m_leadingEdgeTrack,
                      finalTemporalResidual, kMotionEvidenceMinResidualSamples, localWave);
        localWave.supported = localWave.support >= 2
            || (localWave.support >= 1 && localWave.oppose == 0
                && localWave.strongestSupport >= kLocalSingleStrongResidualSamples);

        EvidenceGroupResult identityGroup;
        addMotionVote(identityFitCenter(frame), m_identityFitTrack,
                      finalTemporalResidual, kIdentityMotionEvidenceMinResidualSamples, identityGroup);
        addMotionVote(identityDirectCenter(frame), m_identityDirectTrack,
                      finalTemporalResidual, kIdentityMotionEvidenceMinResidualSamples, identityGroup);
        identityGroup.supported = identityGroup.support >= 1 && identityGroup.oppose == 0;

        out.envelopeEnergySupportCount = envelopeEnergy.support;
        out.localWaveSupportCount = localWave.support;
        out.identitySupportCount = identityGroup.support;
        out.envelopeEnergyGroupSupport = envelopeEnergy.supported;
        out.localWaveGroupSupport = localWave.supported;
        out.identityGroupSupport = identityGroup.supported;
        out.supportingGroups = (envelopeEnergy.supported ? 1 : 0)
            + (localWave.supported ? 1 : 0)
            + (identityGroup.supported ? 1 : 0);

        const bool envelopeDominantAmbiguousMotion = envelopeEnergy.supported
            && std::isfinite(alignedTemporalResidual)
            && std::abs(alignedTemporalResidual) > kEnvelopeOnlyAlignedResidualProtectionSamples;
        out.protectedMotion = out.supportingGroups >= 2 || envelopeDominantAmbiguousMotion;

        out.summary = QStringLiteral("A%1(%2/%3)|B%4(%5/%6)|C%7(%8/%9)")
            .arg(envelopeEnergy.supported ? 1 : 0)
            .arg(envelopeEnergy.support).arg(envelopeEnergy.oppose)
            .arg(localWave.supported ? 1 : 0)
            .arg(localWave.support).arg(localWave.oppose)
            .arg(identityGroup.supported ? 1 : 0)
            .arg(identityGroup.support).arg(identityGroup.oppose);
        return out;
    }

    CorrectionEvidenceDecision assessCorrection(
        const TofEvidenceFrame &frame,
        double finalCenter,
        double alignedCandidateCenter) const
    {
        CorrectionEvidenceDecision out;

        CorrectionGroupResult envelope;
        addCorrectionVote(frame.hilbertEnvelopePeakTof, finalCenter, alignedCandidateCenter, envelope);
        addCorrectionVote(frame.energyMedianT50Tof, finalCenter, alignedCandidateCenter, envelope);
        envelope.candidateSupported = envelope.candidateVotes >= 1 && envelope.finalVotes == 0;
        envelope.finalSupported = envelope.finalVotes >= 1 && envelope.candidateVotes == 0;

        CorrectionGroupResult localWave;
        const int rawVote = correctionVote(frame.rawDirectTof, finalCenter, alignedCandidateCenter);
        const int cfdVote = correctionVote(frame.cfd30Tof, finalCenter, alignedCandidateCenter);
        const int leadingVote = correctionVote(frame.leadingEdgeFitTof, finalCenter, alignedCandidateCenter);
        addCorrectionVote(frame.rawDirectTof, finalCenter, alignedCandidateCenter, localWave);
        addCorrectionVote(frame.cfd30Tof, finalCenter, alignedCandidateCenter, localWave);
        addCorrectionVote(frame.leadingEdgeFitTof, finalCenter, alignedCandidateCenter, localWave);

        const bool rawSupportsCandidate = rawVote > 0;
        const bool secondaryPairBothOpposeCandidate = cfdVote < 0 && leadingVote < 0;
        localWave.candidateSupported = rawSupportsCandidate && !secondaryPairBothOpposeCandidate;
        localWave.finalSupported = localWave.finalVotes >= 1 && localWave.candidateVotes == 0;

        out.envelopeCandidateSupport = envelope.candidateSupported;
        out.localWaveCandidateSupport = localWave.candidateSupported;
        out.envelopeFinalSupport = envelope.finalSupported;
        out.localWaveFinalSupport = localWave.finalSupported;
        out.passed = envelope.candidateSupported || localWave.candidateSupported;
        out.summary = QStringLiteral("A:C%1/F%2(%3/%4)|B:C%5/F%6(%7/%8)")
            .arg(envelope.candidateSupported ? 1 : 0)
            .arg(envelope.finalSupported ? 1 : 0)
            .arg(envelope.candidateVotes).arg(envelope.finalVotes)
            .arg(localWave.candidateSupported ? 1 : 0)
            .arg(localWave.finalSupported ? 1 : 0)
            .arg(localWave.candidateVotes).arg(localWave.finalVotes);
        return out;
    }

    void observe(const TofEvidenceFrame &frame)
    {
        m_hilbertTrack.update(frame.hilbertEnvelopePeakTof);
        m_t50Track.update(frame.energyMedianT50Tof);
        m_rawDirectTrack.update(frame.rawDirectTof);
        m_cfdTrack.update(frame.cfd30Tof);
        m_leadingEdgeTrack.update(frame.leadingEdgeFitTof);
        m_identityFitTrack.update(identityFitCenter(frame));
        m_identityDirectTrack.update(identityDirectCenter(frame));
    }

    void reset()
    {
        m_hilbertTrack.reset();
        m_t50Track.reset();
        m_rawDirectTrack.reset();
        m_cfdTrack.reset();
        m_leadingEdgeTrack.reset();
        m_identityFitTrack.reset();
        m_identityDirectTrack.reset();
    }

private:
    static constexpr double kTemporalFinalLargeResidualSamples = 5.0;
    static constexpr double kMotionTrackMaxVelocitySamples = 3.0;
    static constexpr double kMotionEvidenceMinResidualSamples = 3.0;
    static constexpr double kIdentityMotionEvidenceMinResidualSamples = 2.5;
    static constexpr double kLocalSingleStrongResidualSamples = 4.0;
    static constexpr double kMotionEvidenceMinFinalFraction = 0.50;
    static constexpr double kEnvelopeOnlyAlignedResidualProtectionSamples = 6.0;
    static constexpr double kCorrectionEvidenceAdvantageSamples = 1.5;

    struct MotionTrack
    {
        double previous = std::numeric_limits<double>::quiet_NaN();
        double last = std::numeric_limits<double>::quiet_NaN();

        double predicted() const
        {
            if (!std::isfinite(last)) return std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(previous)) return last;
            const double velocity = std::clamp(last - previous,
                -kMotionTrackMaxVelocitySamples, kMotionTrackMaxVelocitySamples);
            return last + velocity;
        }

        void update(double value)
        {
            if (!std::isfinite(value)) return;
            previous = last;
            last = value;
        }

        void reset()
        {
            previous = std::numeric_limits<double>::quiet_NaN();
            last = std::numeric_limits<double>::quiet_NaN();
        }
    };

    struct EvidenceGroupResult
    {
        int support = 0;
        int oppose = 0;
        double strongestSupport = 0.0;
        bool supported = false;
    };

    struct CorrectionGroupResult
    {
        int candidateVotes = 0;
        int finalVotes = 0;
        double strongestCandidateAdvantage = 0.0;
        double strongestFinalAdvantage = 0.0;
        bool candidateSupported = false;
        bool finalSupported = false;
    };

    MotionTrack m_hilbertTrack;
    MotionTrack m_t50Track;
    MotionTrack m_rawDirectTrack;
    MotionTrack m_cfdTrack;
    MotionTrack m_leadingEdgeTrack;
    MotionTrack m_identityFitTrack;
    MotionTrack m_identityDirectTrack;

    static double identityFitCenter(const TofEvidenceFrame &frame)
    {
        const IdentityBranchObservation &branch = frame.identity.selectedBranch();
        return branch.fitAvailable() ? branch.fitCenter : std::numeric_limits<double>::quiet_NaN();
    }

    static double identityDirectCenter(const TofEvidenceFrame &frame)
    {
        return frame.identity.selectedDirectAvailable()
            ? frame.identity.selectedBranch().directCenter
            : std::numeric_limits<double>::quiet_NaN();
    }

    static void addMotionVote(double value, const MotionTrack &track,
                              double finalTemporalResidual, double minResidual,
                              EvidenceGroupResult &group)
    {
        const double predicted = track.predicted();
        if (!std::isfinite(value) || !std::isfinite(predicted)
            || !std::isfinite(finalTemporalResidual)) return;
        const double residual = value - predicted;
        const double effectiveThreshold = std::max(
            minResidual, kMotionEvidenceMinFinalFraction * std::abs(finalTemporalResidual));
        if (std::abs(residual) < effectiveThreshold) return;
        if (residual * finalTemporalResidual > 0.0) {
            ++group.support;
            group.strongestSupport = std::max(group.strongestSupport, std::abs(residual));
        } else {
            ++group.oppose;
        }
    }

    static int correctionVote(double value, double finalCenter, double alignedCandidateCenter)
    {
        if (!std::isfinite(value) || !std::isfinite(finalCenter)
            || !std::isfinite(alignedCandidateCenter)) return 0;
        const double candidateAdvantage = std::abs(value - finalCenter)
            - std::abs(value - alignedCandidateCenter);
        if (candidateAdvantage >= kCorrectionEvidenceAdvantageSamples) return 1;
        if (candidateAdvantage <= -kCorrectionEvidenceAdvantageSamples) return -1;
        return 0;
    }

    static void addCorrectionVote(double value, double finalCenter,
                                  double alignedCandidateCenter, CorrectionGroupResult &group)
    {
        const int vote = correctionVote(value, finalCenter, alignedCandidateCenter);
        if (vote == 0) return;
        const double candidateAdvantage = std::abs(value - finalCenter)
            - std::abs(value - alignedCandidateCenter);
        if (vote > 0) {
            ++group.candidateVotes;
            group.strongestCandidateAdvantage = std::max(group.strongestCandidateAdvantage, candidateAdvantage);
        } else {
            ++group.finalVotes;
            group.strongestFinalAdvantage = std::max(group.strongestFinalAdvantage, -candidateAdvantage);
        }
    }
};
