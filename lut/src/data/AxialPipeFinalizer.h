#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Final display reconstruction only. Never turn interpolated values into
// measured ultrasound records or include them in pipe statistics.
namespace AxialPipeFinalizer {
struct Point {
    std::uint64_t frameId = 0;
    double xMm = 0.0;
    double thicknessMm = 0.0;
    bool interpolated = false;
};
struct TailPoint {
    double xMm = 0.0;
    double thicknessMm = 0.0;
};
struct Result {
    std::vector<Point> curve;
    double meanMm = 0.0;
    double minMm = 0.0;
    double maxMm = 0.0;
    double lengthMm = 0.0; // head-relative endpoint, not calibrated physical length
    std::size_t measuredCount = 0;
    std::size_t interpolatedCount = 0;
    bool valid = false;
};

inline Result build(const std::vector<Point> &measured,
                    const std::vector<TailPoint> &tail)
{
    Result result;
    std::vector<Point> real;
    real.reserve(measured.size());
    for (const auto &p : measured) {
        if (p.frameId && std::isfinite(p.xMm) && p.xMm >= 0.0 &&
            std::isfinite(p.thicknessMm) && p.thicknessMm > 0.0)
            real.push_back({p.frameId, p.xMm, p.thicknessMm, false});
    }
    std::stable_sort(real.begin(), real.end(), [](const auto &a, const auto &b) {
        return a.frameId < b.frameId;
    });
    // Latest revision wins for each real Frame ID.
    std::vector<Point> latest;
    latest.reserve(real.size());
    for (const auto &p : real) {
        if (!latest.empty() && latest.back().frameId == p.frameId)
            latest.back() = p;
        else
            latest.push_back(p);
    }
    real.swap(latest);
    if (real.empty()) return result;

    double sum = 0.0;
    result.minMm = std::numeric_limits<double>::infinity();
    result.maxMm = -std::numeric_limits<double>::infinity();
    const auto countMeasured = [&](double h, Result &out, double &total) {
        out.minMm = std::min(out.minMm, h);
        out.maxMm = std::max(out.maxMm, h);
        total += h;
        ++out.measuredCount;
    };

    result.curve.reserve(real.size());
    for (std::size_t i = 0; i < real.size(); ++i) {
        const auto &p = real[i];
        countMeasured(p.thicknessMm, result, sum);
        if (i) {
            const auto &prev = real[i - 1];
            const auto gap = p.frameId - prev.frameId;
            const double dx = p.xMm - prev.xMm;
            // Do not bridge a long/unverified radar dropout or two pipes.
            // A short internal miss is linearly interpolated in frame time
            // and x; it remains visually tagged and excluded from stats.
            if (gap > 1 && gap <= 251 && dx >= 0.0 && dx <= 1000.0) {
                for (std::uint64_t j = 1; j < gap; ++j) {
                    const double t = double(j) / double(gap);
                    result.curve.push_back({
                        prev.frameId + j,
                        prev.xMm + dx * t,
                        prev.thicknessMm + (p.thicknessMm - prev.thicknessMm) * t,
                        true
                    });
                    ++result.interpolatedCount;
                }
            }
        }
        result.curve.push_back(p);
    }
    for (const auto &p : real)
        result.lengthMm = std::max(result.lengthMm, p.xMm);
    double lastTailX = real.back().xMm;
    for (const auto &p : tail) {
        if (!std::isfinite(p.thicknessMm) || p.thicknessMm <= 0.0 ||
            !std::isfinite(p.xMm) || p.xMm <= lastTailX)
            continue;
        countMeasured(p.thicknessMm, result, sum);
        lastTailX = p.xMm;
        result.lengthMm = std::max(result.lengthMm, p.xMm);
    }
    result.meanMm = sum / double(result.measuredCount);
    result.valid = true;
    return result;
}
} // namespace AxialPipeFinalizer
