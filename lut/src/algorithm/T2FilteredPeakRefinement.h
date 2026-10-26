#pragma once

// Sub-sample localization of the ALREADY matched positive carrier crest.
// This is a pure three-point parabolic fit; it never selects the packet,
// updates identity/history, or performs the removed Sinc12/Sinc24 searches.
// Coordinates remain absolute sample indices even for cropped ROI input.
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class T2FilteredPeakRefinement
{
public:
    struct Result {
        int integerIndex = -1;
        double parabolicIndex = nan();
        double parabolicHeight = nan();
        const char *status = "UNAVAILABLE";
    };

    static Result analyzeParabolic(const std::vector<double> &filtered,
                                   int firstIndex, int positivePeakIndex,
                                   int physicalLeft, int physicalRight)
    {
        Result out;
        if (firstIndex < 0 || filtered.empty() || positivePeakIndex < 0)
            return out;
        const int end = firstIndex + static_cast<int>(filtered.size()) - 1;
        const int left = std::max(firstIndex, physicalLeft);
        const int right = std::min(end, physicalRight);
        if (positivePeakIndex <= left || positivePeakIndex >= right)
            return out;

        const int k = positivePeakIndex;
        const double ym = filtered[static_cast<size_t>(k - 1 - firstIndex)];
        const double y0 = filtered[static_cast<size_t>(k - firstIndex)];
        const double yp = filtered[static_cast<size_t>(k + 1 - firstIndex)];
        if (!std::isfinite(ym) || !std::isfinite(y0)
            || !std::isfinite(yp) || !(y0 > 0.0)
            || !(y0 >= ym) || !(y0 >= yp)) {
            out.status = "NOT_POSITIVE_LOCAL_MAXIMUM";
            return out;
        }
        out.integerIndex = k;
        const double curvature = ym - 2.0 * y0 + yp;
        if (!std::isfinite(curvature) || curvature >= -1e-18) {
            out.status = "FLAT_PEAK";
            return out;
        }
        const double shift = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
        out.parabolicIndex = static_cast<double>(k) + shift;
        out.parabolicHeight = y0 - 0.25 * (ym - yp) * shift;
        out.status = "PARABOLA_ONLY";
        return out;
    }

private:
    static constexpr double nan() { return std::numeric_limits<double>::quiet_NaN(); }
};
