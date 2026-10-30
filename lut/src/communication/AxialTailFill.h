#pragma once

#include "RadarLaserGeometry.h"
#include "PipeEndCriteria.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// DISPLAY/CSV tail reconstruction ONLY. Never feed these points back into
// live thickness detection, physical radar geometry or frame matching. The
// measured thicknesses (not estimated x values) may enter final pipe stats.
namespace AxialTailFill {
// Up to ~82 seconds of 100Hz post-radar-loss thickness; bounded memory.
inline constexpr std::size_t MaxTailFrames = 8192;
// A pipe is complete when the last 10 consecutive acquired frames contain
// >=5 NaNs, including a run of >=3 NaNs. The FIRST NaN in that confirming
// window is the exclusive cutoff for tail thickness and final statistics.
// SNR and thickness spread remain irrelevant to this decision.
inline constexpr std::size_t EndVoteWindowFrames = PipeEndCriteria::WindowFrames;

struct RealPoint {
    std::uint64_t frameId = 0;
    double xMm = 0.0;
    double thicknessMm = 0.0;
    double snrDb = 0.0;
};
struct TailSample {
    std::uint64_t frameId = 0;
    std::int64_t frameTimeUs = -1;
    double thicknessMm = 0.0;
    double snrDb = 0.0;
    bool valid = false;
    bool backfilled = false;
    bool hasRadarPosition = false;
};
struct EstimatedPoint {
    double xMm = 0.0; // ONLY the axis position is estimated
    double thicknessMm = 0.0; // actual post-radar-loss ultrasonic result
    std::uint64_t frameId = 0;
    double snrDb = 0.0;
    bool backfilled = false;
};

// Take NEW thickness frames acquired AFTER effective radar output ended.
// Real radar-positioned points are never duplicated into the tail. The
// physical distance is divided by the number of accepted, ordered frames;
// do not infer distance from the now-frozen radar TotalLength.
inline PipeEndCriteria::Decision endDecision(
    const std::deque<TailSample> &frames,
    std::int64_t radarLossUs, const RealPoint &lastReal)
{
    if (radarLossUs < 0 || !lastReal.frameId)
        return {};
    PipeEndCriteria::VoteWindow vote;
    bool sawThicknessAfterRadarLoss = false;
    for (const auto &p : frames) {
        if (p.frameId <= lastReal.frameId || p.frameTimeUs <= radarLossUs)
            continue;
        if (p.hasRadarPosition) {
            vote.reset();
            continue;
        }
        const bool validThickness = p.valid && std::isfinite(p.thicknessMm);
        if (validThickness)
            sawThicknessAfterRadarLoss = true;
        // No ultrasonic evidence behind the radar means there is no
        // measurable tail to terminate.
        if (!sawThicknessAfterRadarLoss)
            continue;
        const auto decision = vote.observe(p.frameId, !validThickness);
        if (decision.confirmed)
            return decision;
    }
    return {};
}

inline std::vector<EstimatedPoint> build(
    const std::deque<TailSample> &frames, std::int64_t radarLossUs,
    const RealPoint &lastReal, double fillLengthMm)
{
    std::vector<EstimatedPoint> result;
    if (radarLossUs < 0 || !std::isfinite(fillLengthMm) ||
        fillLengthMm <= RadarLaserGeometry::RadarToLb2Mm ||
        fillLengthMm > RadarLaserGeometry::MaximumRadarToLaserMm ||
        !std::isfinite(lastReal.xMm) || lastReal.xMm < 0.0)
        return result;

    const auto decision = endDecision(frames, radarLossUs, lastReal);
    std::vector<TailSample> accepted;
    accepted.reserve(frames.size());
    for (const TailSample &p : frames) {
        if (p.frameTimeUs <= radarLossUs || p.frameId <= lastReal.frameId)
            continue;
        if (decision.confirmed && p.frameId >= decision.firstInvalidFrameId)
            break; // discard ALL later samples, including transient valid echoes
        if (p.hasRadarPosition || !p.valid || !std::isfinite(p.thicknessMm))
            continue;
        accepted.push_back(p); // no SNR/spread gating
    }
    if (accepted.empty())
        return result;
    const double dx = fillLengthMm / static_cast<double>(accepted.size());
    result.reserve(accepted.size());
    for (size_t i = 0; i < accepted.size(); ++i) {
        const TailSample &p = accepted[i];
        result.push_back({lastReal.xMm + dx * double(i + 1),
                          p.thicknessMm, p.frameId, p.snrDb, p.backfilled});
    }
    return result;
}

// Still independent of DAQ Stop: post-radar-loss thickness arms the
// sliding 10-frame vote, then five NaNs (three contiguous) confirm completion.
inline bool endConfirmed(const std::deque<TailSample> &frames,
                         std::int64_t radarLossUs, const RealPoint &lastReal)
{
    return endDecision(frames, radarLossUs, lastReal).confirmed;
}
} // namespace AxialTailFill
