#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace SecondEchoSearchWindow
{
struct Config {
    double sampleRateHz = 160000000.0;
    double longitudinalVelocityMps = 6459.7;
    int delayCount = 240;
    double firstEchoBiasMm = 1.1;
    double deltaTBiasMm = 0.425577;
    double t2CorrectionMm = 0.000320;
    double referenceThicknessMm = 5.6;
    double modeUpperThicknessMm = 15.0;
    double minimumHalfRangeMm = 3.0;
    double relativeHalfRange = 0.25;
    int packetMaxWidth = 50;
    int predictionHalfWidth = 16;
    int filterOrder = 50;
};

struct Result {
    bool valid = false;
    double halfRangeMm = 0.0;
    double minThicknessMm = std::numeric_limits<double>::quiet_NaN();
    double maxThicknessMm = std::numeric_limits<double>::quiet_NaN();
    double referenceTofSamples = std::numeric_limits<double>::quiet_NaN();
    double minTofSamples = std::numeric_limits<double>::quiet_NaN();
    double maxTofSamples = std::numeric_limits<double>::quiet_NaN();
    int safetyMarginSamples = 0;
    int requestedLeftIndex = 0;
    int requestedRightIndex = 0;
    int minimumRequiredSampleCount = 0;
};

inline Result calculate(const Config &cfg)
{
    Result out;
    if (!(cfg.sampleRateHz > 0.0) || !(cfg.longitudinalVelocityMps > 0.0)
        || !(cfg.referenceThicknessMm > 0.0) || !(cfg.modeUpperThicknessMm > 0.0))
        return out;

    const double k = cfg.longitudinalVelocityMps / cfg.sampleRateHz * 0.5 * 1000.0;
    if (!(k > 0.0))
        return out;
    const double a = -static_cast<double>(cfg.delayCount)
        + (cfg.firstEchoBiasMm - cfg.deltaTBiasMm) / k;
    const auto wallToT2 = [&](double h) {
        const double d = (h + cfg.deltaTBiasMm - cfg.t2CorrectionMm) / k;
        return a + 2.0 * d;
    };

    out.halfRangeMm = std::max(
        cfg.minimumHalfRangeMm,
        cfg.relativeHalfRange * cfg.referenceThicknessMm);
    out.minThicknessMm = std::max(0.5, cfg.referenceThicknessMm - out.halfRangeMm);
    out.maxThicknessMm = std::min(
        cfg.modeUpperThicknessMm,
        cfg.referenceThicknessMm + out.halfRangeMm);
    if (out.maxThicknessMm <= out.minThicknessMm)
        return out;

    out.referenceTofSamples = wallToT2(cfg.referenceThicknessMm);
    out.minTofSamples = wallToT2(out.minThicknessMm);
    out.maxTofSamples = wallToT2(out.maxThicknessMm);
    out.safetyMarginSamples = std::max({
        8,
        std::max(0, cfg.packetMaxWidth / 2),
        2 * std::max(1, cfg.predictionHalfWidth),
        std::max(0, cfg.filterOrder / 2)
    });
    out.requestedLeftIndex = static_cast<int>(std::floor(out.minTofSamples))
        - out.safetyMarginSamples;
    out.requestedRightIndex = static_cast<int>(std::ceil(out.maxTofSamples))
        + out.safetyMarginSamples;
    out.minimumRequiredSampleCount = out.requestedRightIndex + 1;
    out.valid = std::isfinite(out.referenceTofSamples)
        && out.requestedRightIndex > out.requestedLeftIndex;
    return out;
}
} // namespace SecondEchoSearchWindow
