#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// RawPhaseBranchTracker：同一Envelope物理包内部的载波相位支路身份。
//
// 坐标使用 residual = rawLag - envelopeLag，避免真实钢管运动造成Envelope/Raw
// 共同平移时被误判为换支路。模型显式维护：
//   1) anchor residual：branch 0 的包内相位位置；
//   2) carrier period：相邻Raw相关峰的样点间距；
//   3) active branch index：当前跟随哪一个载波相位峰。
//
// select()只给出当前帧应该输出哪个Raw峰；调用端不得用弱phase选择去改变
// EchoPairTracker的物理周期状态。branch切换必须由连续可靠观测确认。
class RawPhaseBranchTracker
{
public:
    struct Candidate {
        double lagSamples = std::numeric_limits<double>::quiet_NaN();
        double absoluteCorrelation = std::numeric_limits<double>::quiet_NaN();
    };

    struct Selection {
        bool modelReady = false;
        bool selected = false;
        bool branchMismatch = false;
        int branchIndex = 0;
        double carrierPeriodSamples = std::numeric_limits<double>::quiet_NaN();
        double branchCenterResidualSamples = std::numeric_limits<double>::quiet_NaN();
        double selectedLagSamples = std::numeric_limits<double>::quiet_NaN();
        double selectedCorrelation = std::numeric_limits<double>::quiet_NaN();
        double selectedResidualSamples = std::numeric_limits<double>::quiet_NaN();
        double residualErrorSamples = std::numeric_limits<double>::quiet_NaN();
        double bestAnyLagSamples = std::numeric_limits<double>::quiet_NaN();
        double bestAnyCorrelation = std::numeric_limits<double>::quiet_NaN();
        double bestAnyResidualSamples = std::numeric_limits<double>::quiet_NaN();
        int bestAnyBranchIndex = 0;
    };

    struct Config {
        int minHistory = 4;
        int maxHistory = 11;
        double minCandidateCorrelation = 0.60;
        double sameBranchResidualGateSamples = 2.5;
        double maxCorrelationLossFromBest = 0.20;
        double minCarrierPeriodSamples = 5.0;
        double maxCarrierPeriodSamples = 11.0;
        double defaultCarrierPeriodSamples = 7.5;
        int branchSwitchConfirmFrames = 2;
    };

    RawPhaseBranchTracker() = default;
    explicit RawPhaseBranchTracker(const Config &config) : m_config(config) {}

    void reset()
    {
        m_anchorResidualHistory.clear();
        m_carrierPeriodHistory.clear();
        m_activeBranchIndex = 0;
        m_pendingBranchIndex = 0;
        m_pendingBranchCount = 0;
    }

    bool ready() const { return static_cast<int>(m_anchorResidualHistory.size()) >= m_config.minHistory; }
    int historyCount() const { return static_cast<int>(m_anchorResidualHistory.size()); }
    int branchIndex() const { return m_activeBranchIndex; }
    double carrierPeriodSamples() const
    {
        const double learned = median(m_carrierPeriodHistory);
        return std::isfinite(learned) ? learned : m_config.defaultCarrierPeriodSamples;
    }
    double anchorResidualSamples() const { return median(m_anchorResidualHistory); }
    double branchCenterResidualSamples() const
    {
        const double anchor = anchorResidualSamples();
        if (!std::isfinite(anchor))
            return std::numeric_limits<double>::quiet_NaN();
        return anchor + m_activeBranchIndex * carrierPeriodSamples();
    }

    // 强Envelope+Raw帧学习phase anchor。carrier period由同一物理包内相邻Raw峰差学习。
    // 当前active branch的观测先归一化回branch0再进入anchor历史，因此允许active branch
    // 切换后继续稳定学习，而不会把不同branch混成一个residual中心。
    void observeReliable(double envelopeLagSamples,
                         double rawLagSamples,
                         double absoluteCorrelation,
                         const std::vector<Candidate> &samePacketCandidates = {})
    {
        if (!finitePositive(rawLagSamples) || !std::isfinite(envelopeLagSamples)
            || !std::isfinite(absoluteCorrelation)
            || absoluteCorrelation < m_config.minCandidateCorrelation)
            return;

        observeCarrierPeriod(samePacketCandidates);
        const double period = carrierPeriodSamples();
        const double residual = rawLagSamples - envelopeLagSamples;

        if (!ready()) {
            m_anchorResidualHistory.push_back(residual);
            trim(m_anchorResidualHistory, m_config.maxHistory);
            return;
        }

        const double anchor = anchorResidualSamples();
        if (!std::isfinite(anchor) || !(period > 0.0))
            return;
        const int observedBranch = static_cast<int>(std::llround((residual - anchor) / period));
        const double normalized = residual - observedBranch * period;
        if (std::abs(normalized - anchor) > m_config.sameBranchResidualGateSamples)
            return;

        if (observedBranch != m_activeBranchIndex) {
            if (observedBranch == m_pendingBranchIndex)
                ++m_pendingBranchCount;
            else {
                m_pendingBranchIndex = observedBranch;
                m_pendingBranchCount = 1;
            }
            if (m_pendingBranchCount < m_config.branchSwitchConfirmFrames)
                return;
            m_activeBranchIndex = observedBranch;
            m_pendingBranchCount = 0;
        } else {
            m_pendingBranchCount = 0;
        }

        m_anchorResidualHistory.push_back(normalized);
        trim(m_anchorResidualHistory, m_config.maxHistory);
    }

    Selection select(double envelopeLagSamples,
                     const std::vector<Candidate> &candidates) const
    {
        Selection out;
        out.modelReady = ready();
        out.branchIndex = m_activeBranchIndex;
        out.carrierPeriodSamples = carrierPeriodSamples();
        out.branchCenterResidualSamples = branchCenterResidualSamples();
        if (!std::isfinite(envelopeLagSamples) || candidates.empty())
            return out;

        int bestAny = -1;
        int validCount = 0;
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            if (!valid(candidates[static_cast<size_t>(i)]))
                continue;
            ++validCount;
            if (bestAny < 0 || candidates[static_cast<size_t>(i)].absoluteCorrelation
                > candidates[static_cast<size_t>(bestAny)].absoluteCorrelation)
                bestAny = i;
        }
        if (bestAny < 0)
            return out;

        const auto &globalBest = candidates[static_cast<size_t>(bestAny)];
        out.bestAnyLagSamples = globalBest.lagSamples;
        out.bestAnyCorrelation = globalBest.absoluteCorrelation;
        out.bestAnyResidualSamples = globalBest.lagSamples - envelopeLagSamples;

        if (!ready() || validCount < 2)
            return out;

        const double anchor = anchorResidualSamples();
        const double period = carrierPeriodSamples();
        const double targetResidual = branchCenterResidualSamples();
        if (!std::isfinite(anchor) || !std::isfinite(targetResidual) || !(period > 0.0))
            return out;
        out.bestAnyBranchIndex = static_cast<int>(std::llround(
            (out.bestAnyResidualSamples - anchor) / period));

        int selected = -1;
        double selectedError = std::numeric_limits<double>::infinity();
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const auto &candidate = candidates[static_cast<size_t>(i)];
            if (!valid(candidate))
                continue;
            const double residual = candidate.lagSamples - envelopeLagSamples;
            const int candidateBranch = static_cast<int>(std::llround((residual - anchor) / period));
            if (candidateBranch != m_activeBranchIndex)
                continue;
            const double error = std::abs(residual - targetResidual);
            if (error > m_config.sameBranchResidualGateSamples)
                continue;
            if (candidate.absoluteCorrelation
                < globalBest.absoluteCorrelation - m_config.maxCorrelationLossFromBest)
                continue;
            if (selected < 0 || error < selectedError - 1e-9
                || (std::abs(error - selectedError) <= 1e-9
                    && candidate.absoluteCorrelation
                    > candidates[static_cast<size_t>(selected)].absoluteCorrelation)) {
                selected = i;
                selectedError = error;
            }
        }

        if (selected < 0) {
            out.branchMismatch = true;
            return out;
        }

        const auto &candidate = candidates[static_cast<size_t>(selected)];
        out.selected = true;
        out.selectedLagSamples = candidate.lagSamples;
        out.selectedCorrelation = candidate.absoluteCorrelation;
        out.selectedResidualSamples = candidate.lagSamples - envelopeLagSamples;
        out.residualErrorSamples = selectedError;
        return out;
    }

private:
    bool valid(const Candidate &c) const
    {
        return finitePositive(c.lagSamples)
            && std::isfinite(c.absoluteCorrelation)
            && c.absoluteCorrelation >= m_config.minCandidateCorrelation;
    }

    void observeCarrierPeriod(const std::vector<Candidate> &candidates)
    {
        std::vector<double> lags;
        for (const auto &c : candidates) {
            if (valid(c))
                lags.push_back(c.lagSamples);
        }
        std::sort(lags.begin(), lags.end());
        double best = std::numeric_limits<double>::quiet_NaN();
        double bestDistance = std::numeric_limits<double>::infinity();
        const double current = carrierPeriodSamples();
        for (size_t i = 1; i < lags.size(); ++i) {
            const double d = lags[i] - lags[i - 1];
            if (d < m_config.minCarrierPeriodSamples || d > m_config.maxCarrierPeriodSamples)
                continue;
            const double distance = std::abs(d - current);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = d;
            }
        }
        if (std::isfinite(best)) {
            m_carrierPeriodHistory.push_back(best);
            trim(m_carrierPeriodHistory, 9);
        }
    }

    static bool finitePositive(double v) { return std::isfinite(v) && v > 0.0; }
    static void trim(std::vector<double> &values, int maxCount)
    {
        while (static_cast<int>(values.size()) > maxCount)
            values.erase(values.begin());
    }
    static double median(std::vector<double> values)
    {
        if (values.empty())
            return std::numeric_limits<double>::quiet_NaN();
        std::sort(values.begin(), values.end());
        const size_t mid = values.size() / 2;
        return values.size() % 2 ? values[mid] : 0.5 * (values[mid - 1] + values[mid]);
    }

    Config m_config;
    std::vector<double> m_anchorResidualHistory;
    std::vector<double> m_carrierPeriodHistory;
    int m_activeBranchIndex = 0;
    int m_pendingBranchIndex = 0;
    int m_pendingBranchCount = 0;
};
