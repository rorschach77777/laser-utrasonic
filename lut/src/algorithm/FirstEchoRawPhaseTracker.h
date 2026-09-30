#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// FirstEchoRawPhaseTracker：FirstEcho 的独立 Raw carrier phase 旁路。
//
// Envelope-NCC 负责物理首回波 packet 身份；本 tracker 只在该 packet 内维护
// residual = rawExtremumTof - envelopeNccTof 的同相位分支。最终选择仍是原始
// 滤波波形局部极值的三点抛物线亚采样位置。tracker 不写 PeakHistory、Prediction、
// 模板或 FinalPrimary 状态；正式输出层仅在全部核心状态更新完成后只读使用该结果。
class FirstEchoRawPhaseTracker
{
public:
    struct Candidate {
        double tofSamples = std::numeric_limits<double>::quiet_NaN();
        double normalizedStrength = std::numeric_limits<double>::quiet_NaN();
    };

    struct Selection {
        bool modelReady = false;
        bool selected = false;
        bool branchMismatch = false;
        int historyCount = 0;
        int candidateCount = 0;
        int activeBranchIndex = 0;
        int bestAnyBranchIndex = 0;
        double carrierPeriodSamples = std::numeric_limits<double>::quiet_NaN();
        double anchorResidualSamples = std::numeric_limits<double>::quiet_NaN();
        double selectedTofSamples = std::numeric_limits<double>::quiet_NaN();
        double selectedStrength = std::numeric_limits<double>::quiet_NaN();
        double selectedResidualSamples = std::numeric_limits<double>::quiet_NaN();
        double residualErrorSamples = std::numeric_limits<double>::quiet_NaN();
        double bestAnyTofSamples = std::numeric_limits<double>::quiet_NaN();
        double bestAnyStrength = std::numeric_limits<double>::quiet_NaN();
        double bestAnyResidualSamples = std::numeric_limits<double>::quiet_NaN();
    };

    struct Config {
        int minHistory = 5;
        int maxHistory = 21;
        double residualGateSamples = 2.5;
        double bootstrapResidualLimitSamples = 4.5;
        double minCandidateStrength = 0.18;
        double minSelectedStrength = 0.25;
        double minCarrierPeriodSamples = 6.0;
        double maxCarrierPeriodSamples = 11.0;
        double defaultCarrierPeriodSamples = 8.0;
    };

    FirstEchoRawPhaseTracker() = default;
    explicit FirstEchoRawPhaseTracker(const Config &config) : m_config(config) {}

    void reset()
    {
        m_anchorResidualHistory.clear();
        m_carrierPeriodHistory.clear();
    }

    bool ready() const
    {
        return static_cast<int>(m_anchorResidualHistory.size()) >= m_config.minHistory;
    }

    int historyCount() const
    {
        return static_cast<int>(m_anchorResidualHistory.size());
    }

    double anchorResidualSamples() const
    {
        return median(m_anchorResidualHistory);
    }

    double carrierPeriodSamples() const
    {
        const double learned = median(m_carrierPeriodHistory);
        return std::isfinite(learned) ? learned : m_config.defaultCarrierPeriodSamples;
    }

    void observeReliable(double envelopeNccTof,
                         double rawTof,
                         double normalizedStrength,
                         const std::vector<Candidate> &samePacketCandidates)
    {
        if (!std::isfinite(envelopeNccTof) || !std::isfinite(rawTof)
            || !std::isfinite(normalizedStrength)
            || normalizedStrength < m_config.minSelectedStrength)
            return;

        observeCarrierPeriod(samePacketCandidates);
        const double residual = rawTof - envelopeNccTof;
        if (!ready()) {
            if (std::abs(residual) <= m_config.bootstrapResidualLimitSamples) {
                m_anchorResidualHistory.push_back(residual);
                trim(m_anchorResidualHistory, m_config.maxHistory);
            }
            return;
        }

        const double anchor = anchorResidualSamples();
        const double period = carrierPeriodSamples();
        if (!std::isfinite(anchor) || !(period > 0.0))
            return;
        const int branch = static_cast<int>(std::llround((residual - anchor) / period));
        if (branch != 0 || std::abs(residual - anchor) > m_config.residualGateSamples)
            return;

        m_anchorResidualHistory.push_back(residual);
        trim(m_anchorResidualHistory, m_config.maxHistory);
    }

    Selection select(double envelopeNccTof,
                     const std::vector<Candidate> &candidates) const
    {
        Selection out;
        out.modelReady = ready();
        out.historyCount = historyCount();
        out.candidateCount = static_cast<int>(candidates.size());
        out.carrierPeriodSamples = carrierPeriodSamples();
        out.anchorResidualSamples = anchorResidualSamples();
        if (!std::isfinite(envelopeNccTof) || candidates.empty())
            return out;

        int bestAny = -1;
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const Candidate &candidate = candidates[static_cast<size_t>(i)];
            if (!valid(candidate))
                continue;
            if (bestAny < 0 || candidate.normalizedStrength
                > candidates[static_cast<size_t>(bestAny)].normalizedStrength)
                bestAny = i;
        }
        if (bestAny < 0)
            return out;

        const Candidate &globalBest = candidates[static_cast<size_t>(bestAny)];
        out.bestAnyTofSamples = globalBest.tofSamples;
        out.bestAnyStrength = globalBest.normalizedStrength;
        out.bestAnyResidualSamples = globalBest.tofSamples - envelopeNccTof;

        if (!ready() || !std::isfinite(out.anchorResidualSamples))
            return out;

        const double period = out.carrierPeriodSamples;
        if (period > 0.0) {
            out.bestAnyBranchIndex = static_cast<int>(std::llround(
                (out.bestAnyResidualSamples - out.anchorResidualSamples) / period));
        }

        int selected = -1;
        double selectedError = std::numeric_limits<double>::infinity();
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const Candidate &candidate = candidates[static_cast<size_t>(i)];
            if (!valid(candidate) || candidate.normalizedStrength < m_config.minSelectedStrength)
                continue;
            const double residual = candidate.tofSamples - envelopeNccTof;
            const int branch = period > 0.0
                ? static_cast<int>(std::llround((residual - out.anchorResidualSamples) / period))
                : 0;
            if (branch != 0)
                continue;
            const double error = std::abs(residual - out.anchorResidualSamples);
            if (error > m_config.residualGateSamples)
                continue;
            if (selected < 0 || error < selectedError - 1e-9
                || (std::abs(error - selectedError) <= 1e-9
                    && candidate.normalizedStrength
                        > candidates[static_cast<size_t>(selected)].normalizedStrength)) {
                selected = i;
                selectedError = error;
            }
        }

        if (selected < 0) {
            out.branchMismatch = true;
            return out;
        }

        const Candidate &candidate = candidates[static_cast<size_t>(selected)];
        out.selected = true;
        out.selectedTofSamples = candidate.tofSamples;
        out.selectedStrength = candidate.normalizedStrength;
        out.selectedResidualSamples = candidate.tofSamples - envelopeNccTof;
        out.residualErrorSamples = selectedError;
        return out;
    }

private:
    bool valid(const Candidate &candidate) const
    {
        return std::isfinite(candidate.tofSamples)
            && std::isfinite(candidate.normalizedStrength)
            && candidate.normalizedStrength >= m_config.minCandidateStrength;
    }

    void observeCarrierPeriod(const std::vector<Candidate> &candidates)
    {
        std::vector<double> lags;
        for (const Candidate &candidate : candidates) {
            if (valid(candidate))
                lags.push_back(candidate.tofSamples);
        }
        std::sort(lags.begin(), lags.end());
        const double current = carrierPeriodSamples();
        double best = std::numeric_limits<double>::quiet_NaN();
        double bestDistance = std::numeric_limits<double>::infinity();
        for (size_t i = 1; i < lags.size(); ++i) {
            const double spacing = lags[i] - lags[i - 1];
            if (spacing < m_config.minCarrierPeriodSamples
                || spacing > m_config.maxCarrierPeriodSamples)
                continue;
            const double distance = std::abs(spacing - current);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = spacing;
            }
        }
        if (std::isfinite(best)) {
            m_carrierPeriodHistory.push_back(best);
            trim(m_carrierPeriodHistory, 15);
        }
    }

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
};
