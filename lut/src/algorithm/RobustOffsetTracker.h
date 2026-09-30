#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class RobustOffsetTracker
{
public:
    struct Config
    {
        int historySize = 31;
        int minimumHistory = 9;
        double bootstrapMaxAbsOffset = 10.0;
        double sigmaFloor = 0.35;
        double updateSigmaMultiplier = 4.0;
        double updateFloor = 2.0;
        double updateCeiling = 4.0;
    };

    struct Snapshot
    {
        int count = 0;
        bool ready = false;
        double baseline = std::numeric_limits<double>::quiet_NaN();
        double sigma = std::numeric_limits<double>::quiet_NaN();
        double residual = std::numeric_limits<double>::quiet_NaN();
        double updateGate = std::numeric_limits<double>::quiet_NaN();
    };

    explicit RobustOffsetTracker(const Config &config)
        : m_config(config)
    {
    }

    bool ready() const { return static_cast<int>(m_values.size()) >= m_config.minimumHistory; }
    int count() const { return static_cast<int>(m_values.size()); }

    double baseline() const { return median(m_values); }

    double sigma() const
    {
        if (m_values.empty())
            return std::numeric_limits<double>::quiet_NaN();
        const double center = baseline();
        if (!std::isfinite(center))
            return std::numeric_limits<double>::quiet_NaN();
        std::vector<double> deviations;
        deviations.reserve(m_values.size());
        for (double value : m_values)
            deviations.push_back(std::abs(value - center));
        const double mad = median(std::move(deviations));
        return std::isfinite(mad) ? 1.4826 * mad
                                  : std::numeric_limits<double>::quiet_NaN();
    }

    Snapshot inspect(double offset) const
    {
        Snapshot out;
        out.count = count();
        out.ready = ready();
        if (!out.ready)
            return out;
        out.baseline = baseline();
        out.sigma = sigma();
        if (std::isfinite(offset) && std::isfinite(out.baseline))
            out.residual = offset - out.baseline;
        out.updateGate = robustGate(out.sigma,
                                    m_config.sigmaFloor,
                                    m_config.updateSigmaMultiplier,
                                    m_config.updateFloor,
                                    m_config.updateCeiling);
        return out;
    }

    bool update(double offset)
    {
        if (!std::isfinite(offset))
            return false;
        if (!ready()) {
            if (std::abs(offset) > m_config.bootstrapMaxAbsOffset)
                return false;
            push(offset);
            return true;
        }
        const Snapshot current = inspect(offset);
        if (!std::isfinite(current.residual)
            || !std::isfinite(current.updateGate)
            || std::abs(current.residual) > current.updateGate)
            return false;
        push(offset);
        return true;
    }

    void reset() { m_values.clear(); }

    static double robustGate(double sigmaValue,
                             double sigmaFloor,
                             double sigmaMultiplier,
                             double floor,
                             double ceiling)
    {
        const double robust = std::max(
            sigmaFloor,
            std::isfinite(sigmaValue) ? sigmaValue : sigmaFloor);
        return std::clamp(sigmaMultiplier * robust, floor, ceiling);
    }

private:
    Config m_config;
    std::vector<double> m_values;

    static double median(std::vector<double> values)
    {
        if (values.empty())
            return std::numeric_limits<double>::quiet_NaN();
        const size_t middle = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + middle, values.end());
        const double upper = values[middle];
        if (values.size() % 2)
            return upper;
        const double lower = *std::max_element(values.begin(), values.begin() + middle);
        return 0.5 * (lower + upper);
    }

    void push(double value)
    {
        m_values.push_back(value);
        if (static_cast<int>(m_values.size()) > m_config.historySize)
            m_values.erase(m_values.begin());
    }
};
