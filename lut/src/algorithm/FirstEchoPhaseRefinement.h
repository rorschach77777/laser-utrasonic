#pragma once

#include <cmath>
#include <limits>

// FirstEcho Phase Refinement：仅用于最终输出层的小增益亚采样修正。
//
// 隔离原则：
// 1) 不参与 Candidate / Identity / FinalPrimary 判定；
// 2) 不写 PeakHistory、Prediction、模板或任何跟踪状态；
// 3) 仅当正式输出仍等于 Envelope-NCC、未走主算法 fallback、未被 FinalPrimary
//    Identity 纠正，且 RawPhase branch 已可靠建立时才允许应用。
//
//   TOF_refined = TOF_NCC
//       + alpha * ((TOF_RawPhase - TOF_NCC) - anchorResidual)
struct FirstEchoPhaseRefinementInput
{
    bool enabled = false;
    double alpha = 0.13;
    bool formalOutputValid = false;
    bool triEvidenceUsedMainFallback = false;
    bool finalPrimaryIdentityCorrected = false;
    bool rawPhaseModelReady = false;
    bool rawPhaseSelected = false;

    double formalOutputTofSamples = std::numeric_limits<double>::quiet_NaN();
    double formalOutputThicknessMm = std::numeric_limits<double>::quiet_NaN();
    double envelopeNccTofSamples = std::numeric_limits<double>::quiet_NaN();
    double rawPhaseTofSamples = std::numeric_limits<double>::quiet_NaN();
    double anchorResidualSamples = std::numeric_limits<double>::quiet_NaN();
    double mmPerSample = std::numeric_limits<double>::quiet_NaN();
};

struct FirstEchoPhaseRefinementDecision
{
    bool eligible = false;
    bool applied = false;
    double correctionSamples = 0.0;
    double outputTofSamples = std::numeric_limits<double>::quiet_NaN();
    double outputThicknessMm = std::numeric_limits<double>::quiet_NaN();
    const char *reason = "UNAVAILABLE";
};

inline FirstEchoPhaseRefinementDecision evaluateFirstEchoPhaseRefinement(
    const FirstEchoPhaseRefinementInput &in)
{
    FirstEchoPhaseRefinementDecision out;
    out.outputTofSamples = in.formalOutputTofSamples;
    out.outputThicknessMm = in.formalOutputThicknessMm;

    if (!in.formalOutputValid
        || !std::isfinite(in.formalOutputTofSamples)
        || !std::isfinite(in.formalOutputThicknessMm)) {
        out.reason = "FORMAL_OUTPUT_UNAVAILABLE";
        return out;
    }
    if (in.triEvidenceUsedMainFallback) {
        out.reason = "MAIN_FALLBACK_PROTECTED";
        return out;
    }
    if (in.finalPrimaryIdentityCorrected) {
        out.reason = "IDENTITY_CORRECTION_PROTECTED";
        return out;
    }
    if (!std::isfinite(in.envelopeNccTofSamples)
        || std::abs(in.formalOutputTofSamples - in.envelopeNccTofSamples) > 1e-6) {
        out.reason = "FORMAL_OUTPUT_NOT_ENVELOPE_NCC";
        return out;
    }
    if (!in.rawPhaseModelReady || !in.rawPhaseSelected
        || !std::isfinite(in.rawPhaseTofSamples)
        || !std::isfinite(in.anchorResidualSamples)) {
        out.reason = "RAW_PHASE_UNAVAILABLE";
        return out;
    }
    if (!std::isfinite(in.mmPerSample) || in.mmPerSample <= 0.0) {
        out.reason = "INVALID_SCALE";
        return out;
    }

    out.eligible = true;
    if (!in.enabled) {
        out.reason = "DISABLED";
        return out;
    }
    if (!std::isfinite(in.alpha) || in.alpha <= 0.0) {
        out.reason = "ALPHA_ZERO_OR_INVALID";
        return out;
    }

    const double residual = in.rawPhaseTofSamples - in.envelopeNccTofSamples;
    const double correction = in.alpha * (residual - in.anchorResidualSamples);
    if (!std::isfinite(correction)) {
        out.reason = "NONFINITE_CORRECTION";
        return out;
    }

    out.correctionSamples = correction;
    out.outputTofSamples = in.envelopeNccTofSamples + correction;
    out.outputThicknessMm = in.formalOutputThicknessMm + correction * in.mmPerSample;
    out.applied = true;
    out.reason = "APPLIED";
    return out;
}
