#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// WeakRawTemporalTracker 只负责 Envelope/Enhanced/单帧Raw辅证均未授权时，
// 对 matched-raw top1/top2 弱候选做跨帧身份积累。它不维护正式 Echo Pair 周期，
// 不做 GLOBAL 搜索，也不使用参考厚度；确认成功后由调用端决定是否写入 EchoPairTracker。
//
// 关键点：同一帧 top1/top2 会并行形成弱身份假设，不能只保留一个 pending。
// 这样当 top1 单帧落错载波支路、top2 才是连续真实 Echo2 时，后续帧仍能让正确链胜出。
class WeakRawTemporalTracker
{
public:
    enum class State {
        Idle,
        PendingTwoFrame,
        PendingThreeFrame
    };

    struct Candidate {
        double lagSamples = std::numeric_limits<double>::quiet_NaN();
        double absoluteCorrelation = std::numeric_limits<double>::quiet_NaN();
        double energyRatio = std::numeric_limits<double>::quiet_NaN();
        int rank = 0; // 1=top1, 2=top2
    };

    struct Result {
        State state = State::Idle;
        bool hasCandidate = false;
        bool pending = false;
        bool confirmed = false;
        bool reseeded = false;
        int requiredFrames = 0;
        int consecutiveFrames = 0;
        int selectedRank = 0;
        int activeHypotheses = 0;
        double selectedLagSamples = std::numeric_limits<double>::quiet_NaN();
        double selectedCorrelation = std::numeric_limits<double>::quiet_NaN();
        double selectedEnergyRatio = std::numeric_limits<double>::quiet_NaN();
        double pendingCenterSamples = std::numeric_limits<double>::quiet_NaN();
        double confirmedLagSamples = std::numeric_limits<double>::quiet_NaN();
    };

    struct Config {
        // 时序积累允许明显弱于正式单帧Raw辅证，但仍要求存在可测波形和局部能量。
        double minCandidateCorrelation = 0.55;
        double minCandidateEnergyRatio = 0.02;
        // 紧贴历史的候选只需2帧；离历史稍远但仍合理的候选要求3帧。
        double twoFrameHistoryResidualSamples = 3.0;
        double threeFrameHistoryResidualSamples = 5.0;
        // 同一弱身份候选跨帧允许的最大漂移。
        double interFrameConsistencySamples = 3.0;
        int maxHypotheses = 4;
    };

    WeakRawTemporalTracker() = default;
    explicit WeakRawTemporalTracker(const Config &config)
        : m_config(config)
    {
    }

    void reset()
    {
        m_hypotheses.clear();
    }

    State state() const
    {
        const Hypothesis *best = bestHypothesis();
        if (!best)
            return State::Idle;
        return best->requiredFrames == 2 ? State::PendingTwoFrame : State::PendingThreeFrame;
    }

    int pendingCount() const
    {
        const Hypothesis *best = bestHypothesis();
        return best ? static_cast<int>(best->lags.size()) : 0;
    }

    int requiredFrames() const
    {
        const Hypothesis *best = bestHypothesis();
        return best ? best->requiredFrames : 0;
    }

    double pendingCenter() const
    {
        const Hypothesis *best = bestHypothesis();
        return best ? median(best->lags) : std::numeric_limits<double>::quiet_NaN();
    }

    Result observe(double predictedPeriodSamples,
                   const std::vector<Candidate> &candidates)
    {
        Result out;
        if (!std::isfinite(predictedPeriodSamples) || predictedPeriodSamples <= 0.0) {
            reset();
            return out;
        }

        std::vector<Candidate> valid;
        valid.reserve(candidates.size());
        for (const auto &candidate : candidates) {
            if (!std::isfinite(candidate.lagSamples)
                || !std::isfinite(candidate.absoluteCorrelation)
                || !std::isfinite(candidate.energyRatio)
                || candidate.lagSamples <= 0.0
                || candidate.absoluteCorrelation < m_config.minCandidateCorrelation
                || candidate.energyRatio < m_config.minCandidateEnergyRatio)
                continue;
            const double historyResidual =
                std::abs(candidate.lagSamples - predictedPeriodSamples);
            if (historyResidual <= m_config.threeFrameHistoryResidualSamples)
                valid.push_back(candidate);
        }

        if (valid.empty()) {
            reset();
            return out;
        }

        std::vector<Hypothesis> next;
        next.reserve(static_cast<size_t>(m_config.maxHypotheses) + valid.size());

        // 先推进上一帧已经存在的所有假设。每条假设可从当前top1/top2中选择
        // 与上一时刻最连续的候选，而不是强制沿当前top1走。
        for (const auto &hypothesis : m_hypotheses) {
            const double historyGate = hypothesis.requiredFrames == 2
                ? m_config.twoFrameHistoryResidualSamples
                : m_config.threeFrameHistoryResidualSamples;
            int bestIndex = -1;
            double bestStep = std::numeric_limits<double>::infinity();
            for (int i = 0; i < static_cast<int>(valid.size()); ++i) {
                const auto &candidate = valid[static_cast<size_t>(i)];
                const double step = std::abs(candidate.lagSamples - hypothesis.lastLag());
                if (step > m_config.interFrameConsistencySamples
                    || std::abs(candidate.lagSamples - predictedPeriodSamples) > historyGate)
                    continue;
                if (bestIndex < 0 || step < bestStep - 1e-9
                    || (std::abs(step - bestStep) <= 1e-9
                        && candidate.absoluteCorrelation
                            > valid[static_cast<size_t>(bestIndex)].absoluteCorrelation)) {
                    bestIndex = i;
                    bestStep = step;
                }
            }
            if (bestIndex < 0)
                continue;

            Hypothesis advanced = hypothesis;
            const auto &candidate = valid[static_cast<size_t>(bestIndex)];
            advanced.lags.push_back(candidate.lagSamples);
            if (advanced.lags.size() > 3)
                advanced.lags.erase(advanced.lags.begin());
            advanced.lastCandidate = candidate;
            next.push_back(std::move(advanced));
        }

        // 当前帧top1/top2同时可以成为新的pending种子，保留替代身份支路。
        for (const auto &candidate : valid) {
            Hypothesis seed;
            const double residual = std::abs(candidate.lagSamples - predictedPeriodSamples);
            seed.requiredFrames = residual <= m_config.twoFrameHistoryResidualSamples ? 2 : 3;
            seed.lags.push_back(candidate.lagSamples);
            seed.lastCandidate = candidate;
            next.push_back(std::move(seed));
        }

        deduplicate(next, predictedPeriodSamples);
        m_hypotheses = std::move(next);
        if (m_hypotheses.empty())
            return out;

        // 任一旧假设达到所需连续帧数即可确认；优先帧数更长、历史残差更小、相关更高。
        int confirmedIndex = -1;
        for (int i = 0; i < static_cast<int>(m_hypotheses.size()); ++i) {
            if (static_cast<int>(m_hypotheses[static_cast<size_t>(i)].lags.size())
                < m_hypotheses[static_cast<size_t>(i)].requiredFrames)
                continue;
            if (confirmedIndex < 0
                || better(m_hypotheses[static_cast<size_t>(i)],
                          m_hypotheses[static_cast<size_t>(confirmedIndex)],
                          predictedPeriodSamples))
                confirmedIndex = i;
        }

        const Hypothesis *chosen = nullptr;
        if (confirmedIndex >= 0)
            chosen = &m_hypotheses[static_cast<size_t>(confirmedIndex)];
        else
            chosen = bestHypothesis(predictedPeriodSamples);
        if (!chosen)
            return out;

        out.hasCandidate = true;
        out.selectedRank = chosen->lastCandidate.rank;
        out.selectedLagSamples = chosen->lastCandidate.lagSamples;
        out.selectedCorrelation = chosen->lastCandidate.absoluteCorrelation;
        out.selectedEnergyRatio = chosen->lastCandidate.energyRatio;
        out.requiredFrames = chosen->requiredFrames;
        out.consecutiveFrames = static_cast<int>(chosen->lags.size());
        out.pendingCenterSamples = median(chosen->lags);
        out.state = chosen->requiredFrames == 2
            ? State::PendingTwoFrame : State::PendingThreeFrame;
        out.activeHypotheses = static_cast<int>(m_hypotheses.size());

        if (confirmedIndex >= 0) {
            out.confirmed = true;
            out.confirmedLagSamples = chosen->lastCandidate.lagSamples;
            reset();
            return out;
        }

        out.pending = true;
        return out;
    }

    static const char *stateName(State state)
    {
        switch (state) {
        case State::Idle: return "IDLE";
        case State::PendingTwoFrame: return "PENDING_2";
        case State::PendingThreeFrame: return "PENDING_3";
        }
        return "IDLE";
    }

private:
    struct Hypothesis {
        int requiredFrames = 0;
        std::vector<double> lags;
        Candidate lastCandidate;

        double lastLag() const
        {
            return lags.empty() ? std::numeric_limits<double>::quiet_NaN() : lags.back();
        }
    };

    static double median(std::vector<double> values)
    {
        if (values.empty())
            return std::numeric_limits<double>::quiet_NaN();
        std::sort(values.begin(), values.end());
        const size_t mid = values.size() / 2;
        if (values.size() % 2 == 1)
            return values[mid];
        return 0.5 * (values[mid - 1] + values[mid]);
    }

    static bool better(const Hypothesis &a,
                       const Hypothesis &b,
                       double predictedPeriodSamples)
    {
        if (a.lags.size() != b.lags.size())
            return a.lags.size() > b.lags.size();
        const double ar = std::abs(median(a.lags) - predictedPeriodSamples);
        const double br = std::abs(median(b.lags) - predictedPeriodSamples);
        if (std::abs(ar - br) > 1e-9)
            return ar < br;
        if (std::abs(a.lastCandidate.absoluteCorrelation - b.lastCandidate.absoluteCorrelation) > 1e-9)
            return a.lastCandidate.absoluteCorrelation > b.lastCandidate.absoluteCorrelation;
        return a.lastCandidate.rank < b.lastCandidate.rank;
    }

    const Hypothesis *bestHypothesis(double predictedPeriodSamples) const
    {
        if (m_hypotheses.empty())
            return nullptr;
        int best = 0;
        for (int i = 1; i < static_cast<int>(m_hypotheses.size()); ++i) {
            if (better(m_hypotheses[static_cast<size_t>(i)],
                       m_hypotheses[static_cast<size_t>(best)],
                       predictedPeriodSamples))
                best = i;
        }
        return &m_hypotheses[static_cast<size_t>(best)];
    }

    const Hypothesis *bestHypothesis() const
    {
        if (m_hypotheses.empty())
            return nullptr;
        // 无外部prediction时只用于状态诊断：优先累计帧数，再优先2帧模式。
        int best = 0;
        for (int i = 1; i < static_cast<int>(m_hypotheses.size()); ++i) {
            const auto &a = m_hypotheses[static_cast<size_t>(i)];
            const auto &b = m_hypotheses[static_cast<size_t>(best)];
            if (a.lags.size() > b.lags.size()
                || (a.lags.size() == b.lags.size() && a.requiredFrames < b.requiredFrames))
                best = i;
        }
        return &m_hypotheses[static_cast<size_t>(best)];
    }

    void deduplicate(std::vector<Hypothesis> &hypotheses,
                     double predictedPeriodSamples) const
    {
        std::sort(hypotheses.begin(), hypotheses.end(),
            [&](const Hypothesis &a, const Hypothesis &b) {
                return better(a, b, predictedPeriodSamples);
            });
        std::vector<Hypothesis> kept;
        kept.reserve(static_cast<size_t>(m_config.maxHypotheses));
        for (auto &candidate : hypotheses) {
            bool duplicate = false;
            for (const auto &existing : kept) {
                if (candidate.requiredFrames == existing.requiredFrames
                    && candidate.lags.size() == existing.lags.size()
                    && std::abs(candidate.lastLag() - existing.lastLag()) < 0.75) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                kept.push_back(std::move(candidate));
            if (static_cast<int>(kept.size()) >= m_config.maxHypotheses)
                break;
        }
        hypotheses = std::move(kept);
    }

    Config m_config;
    std::vector<Hypothesis> m_hypotheses;
};
