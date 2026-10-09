#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// RecoveryTemporalConfirmTracker：远 RECENTER / Echo3 恢复候选的输出授权器。
//
// 与 EchoPairTracker 完全隔离：这里只看最近 3 个物理帧里的恢复候选，
// 同一周期族在窗口内出现至少 2 票才允许当前帧输出。中间允许 1 帧无候选
// 或出现其它候选；任何结果都不得反向修改 EchoPairTracker 的物理周期状态。
class RecoveryTemporalConfirmTracker
{
public:
    struct Result {
        bool pending = false;
        bool confirmed = false;
        int votes = 0;
        int windowFrames = 0;
        double confirmedPeriodSamples = std::numeric_limits<double>::quiet_NaN();
        int sourceMask = 0;
    };

    struct Config {
        int windowFrames = 3;
        int requiredVotes = 2;
        double minGateSamples = 3.0;
        double relativeGate = 0.01;
    };

    RecoveryTemporalConfirmTracker() = default;
    explicit RecoveryTemporalConfirmTracker(const Config &config) : m_config(config) {}

    void reset()
    {
        m_frameIndex = -1;
        m_observations.clear();
    }

    void beginFrame()
    {
        ++m_frameIndex;
        trimExpired();
    }

    Result observe(double periodSamples, int sourceMask)
    {
        Result out;
        out.windowFrames = m_config.windowFrames;
        if (!std::isfinite(periodSamples) || periodSamples <= 0.0 || m_frameIndex < 0
            || sourceMask == 0)
            return out;

        m_observations.push_back({m_frameIndex, periodSamples, sourceMask});
        trimExpired();

        std::vector<double> matching;
        matching.reserve(m_observations.size());
        int commonSourceMask = sourceMask;
        for (const auto &observation : m_observations) {
            // 两票必须至少共享一种身份依据：Echo3不能与“仅phase-resolved RECENTER”
            // 拼成2-of-3，反之亦然；Both可与任一共享对应证据。
            if ((sourceMask & observation.sourceMask) != 0
                && matches(periodSamples, observation.periodSamples)) {
                matching.push_back(observation.periodSamples);
                commonSourceMask &= observation.sourceMask;
            }
        }
        out.votes = static_cast<int>(matching.size());
        out.pending = out.votes < m_config.requiredVotes;
        if (out.votes < m_config.requiredVotes)
            return out;

        std::sort(matching.begin(), matching.end());
        const size_t mid = matching.size() / 2;
        out.confirmedPeriodSamples = matching.size() % 2
            ? matching[mid]
            : 0.5 * (matching[mid - 1] + matching[mid]);
        out.sourceMask = commonSourceMask != 0 ? commonSourceMask : sourceMask;
        out.confirmed = true;
        out.pending = false;

        // 一次确认只授权当前帧。确认后清空旧票，避免同一事件在后续帧连续重复确认。
        m_observations.clear();
        return out;
    }

    int pendingObservationCount() const
    {
        return static_cast<int>(m_observations.size());
    }

private:
    struct Observation {
        int frameIndex = -1;
        double periodSamples = std::numeric_limits<double>::quiet_NaN();
        int sourceMask = 0;
    };

    bool matches(double a, double b) const
    {
        if (!std::isfinite(a) || !std::isfinite(b))
            return false;
        const double center = 0.5 * (std::abs(a) + std::abs(b));
        const double gate = std::max(m_config.minGateSamples, m_config.relativeGate * center);
        return std::abs(a - b) <= gate;
    }

    void trimExpired()
    {
        const int firstValidFrame = m_frameIndex - (std::max(1, m_config.windowFrames) - 1);
        m_observations.erase(
            std::remove_if(m_observations.begin(), m_observations.end(),
                           [&](const Observation &observation) {
                               return observation.frameIndex < firstValidFrame;
                           }),
            m_observations.end());
    }

    Config m_config;
    int m_frameIndex = -1;
    std::vector<Observation> m_observations;
};
