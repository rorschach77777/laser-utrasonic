#pragma once

// Carrier-crest localization for the ALREADY matched ultrasonic packet.
// May be called by the selected non-t50 tracker coordinate path or by the
// passive diagnostic; it performs no identity learning or history update.
// All coordinates are absolute sample indices, even for a cropped ROI.
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
        double sinc12Index = nan();
        double sinc24Index = nan();
        double parabolicHeight = nan();
        double sinc12Height = nan();
        double sinc24Height = nan();
        const char* status = "UNAVAILABLE";
    };

    static Result analyze(const std::vector<double>& filtered,
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

        const auto sample = [&](int index) -> double {
            return filtered[static_cast<size_t>(index-firstIndex)];
        };
        const int k = positivePeakIndex;
        const double ym = sample(k-1), y0 = sample(k), yp = sample(k+1);
        if (!std::isfinite(ym) || !std::isfinite(y0)
            || !std::isfinite(yp) || !(y0 > 0.0)
            || !(y0 >= ym) || !(y0 >= yp)) {
            out.status = "NOT_POSITIVE_LOCAL_MAXIMUM";
            return out;
        }
        out.integerIndex = k;
        const double curvature = ym - 2.0*y0 + yp;
        if (!std::isfinite(curvature) || curvature >= -1e-18) {
            out.status = "FLAT_PEAK";
            return out;
        }
        const double shift = std::clamp(0.5*(ym-yp)/curvature, -0.5, 0.5);
        out.parabolicIndex = static_cast<double>(k)+shift;
        out.parabolicHeight = y0
            - 0.25*(ym-yp)*shift;

        // Windowed sinc requires symmetric, REAL observations around the
        // chosen crest. No zero padding outside an ROI: mark unavailable.
        // 24 is a higher-order numerical reference, NOT ground truth.
        double position=nan(), height=nan();
        if (interpolatedMax(filtered, firstIndex, left, right,
                            k, 12, position, height)) {
            out.sinc12Index=position;
            out.sinc12Height=height;
        }
        if (interpolatedMax(filtered, firstIndex, left, right,
                            k, 24, position, height)) {
            out.sinc24Index=position;
            out.sinc24Height=height;
        }
        out.status = std::isfinite(out.sinc24Index)
            ? "PARABOLA_SINC12_SINC24"
            : (std::isfinite(out.sinc12Index) ? "PARABOLA_SINC12"
                                             : "PARABOLA_ONLY");
        return out;
    }

private:
    static constexpr double nan() { return std::numeric_limits<double>::quiet_NaN(); }

    static double sinc(double x)
    {
        if (std::abs(x) < 1e-12) return 1.0;
        const double a=3.141592653589793238462643383279502884*x;
        return std::sin(a)/a;
    }

    static double interpolate(const std::vector<double>& y,
                              int first, int k, double x, int radius)
    {
        double value=0.0;
        double weights=0.0;
        for (int i=k-radius; i<=k+radius; ++i) {
            const double u=x-static_cast<double>(i);
            if (std::abs(u)>=static_cast<double>(radius)) continue;
            const double w=sinc(u)*sinc(u/static_cast<double>(radius));
            const double yi=y[static_cast<size_t>(i-first)];
            value+=yi*w;
            weights+=w;
        }
        return std::abs(weights)>1e-12 ? value/weights : nan();
    }

    static bool interpolatedMax(const std::vector<double>& y, int first,
                                int left, int right, int k, int radius,
                                double& position, double& height)
    {
        if (k-radius < left || k+radius > right) return false;
        // Peak identity was established outside this function; search ONLY
        // the +/-0.5 sample Voronoi interval of this SAME carrier crest.
        double a=static_cast<double>(k)-0.5;
        double b=static_cast<double>(k)+0.5;
        constexpr double phi=0.6180339887498948482;
        double c=b-phi*(b-a), d=a+phi*(b-a);
        double fc=interpolate(y,first,k,c,radius);
        double fd=interpolate(y,first,k,d,radius);
        if (!std::isfinite(fc) || !std::isfinite(fd)) return false;
        for (int i=0; i<26; ++i) {
            if (fc > fd) {
                b=d; d=c; fd=fc; c=b-phi*(b-a);
                fc=interpolate(y,first,k,c,radius);
            } else {
                a=c; c=d; fc=fd; d=a+phi*(b-a);
                fd=interpolate(y,first,k,d,radius);
            }
        }
        const double x=0.5*(a+b);
        const double f=interpolate(y,first,k,x,radius);
        if (!std::isfinite(f) || f<=0.0
            || std::abs(x-static_cast<double>(k))>=0.495)
            return false; // possible neighboring carrier peak: no switch
        position=x;
        height=f;
        return true;
    }
};
