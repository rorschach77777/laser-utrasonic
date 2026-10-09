#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// EchoPairTracker 现在按 period-first 语义管理“相邻回波差分周期D”的时序状态。
// D 可以来自 T1->T2，也可以来自 T2->T3；Tracker本身不假定左侧回波一定是T1，
// 不参与绝对回波序号判定，也不保存绝对TOF/参考厚度。GLOBAL_SEARCH 负责周期发现，WARMUP做两帧确认，
// LOCK后由当前帧Echo1模板在预测周期附近做小窗NCC；连续失败时8->16->32点扩窗，
// 第3次失败回到GLOBAL_SEARCH。
class EchoPairTracker
{
public:
    enum class Stage {
        GlobalSearch,
        Warmup,
        Locked
    };

    struct SearchPlan {
        Stage stage = Stage::GlobalSearch;
        bool useGlobalSearch = true;
        double predictedPeriodSamples = std::numeric_limits<double>::quiet_NaN();
        double frozenPreviousPeriodSamples = std::numeric_limits<double>::quiet_NaN();
        bool reacquiringFromFrozenPeriod = false;
        int halfWidthSamples = 0;
        int consecutiveMisses = 0;
    };

    struct StartupCandidate {
        double periodSamples = std::numeric_limits<double>::quiet_NaN();
        double score = 0.0;
        bool periodicFamily = false;
    };

    struct StartupConsensus {
        bool available = false;
        double periodSamples = std::numeric_limits<double>::quiet_NaN();
        int framesObserved = 0;
        int frameVotes = 0;
        int periodicFamilyVotes = 0;
        int clusterCount = 0;
        double meanScore = 0.0;
    };

    void reset()
    {
        m_stage = Stage::GlobalSearch;
        m_periodHistory.clear();
        m_pendingPeriod = std::numeric_limits<double>::quiet_NaN();
        m_pendingCount = 0;
        m_pendingMisses = 0;
        m_lockMisses = 0;
        m_frozenPreviousPeriod = std::numeric_limits<double>::quiet_NaN();
        m_expandedRecoveryPendingPeriod = std::numeric_limits<double>::quiet_NaN();
        m_startupFrames.clear();
    }

    SearchPlan plan() const
    {
        SearchPlan out;
        out.stage = m_stage;
        out.consecutiveMisses = m_stage == Stage::Locked ? m_lockMisses : m_pendingMisses;
        out.predictedPeriodSamples = predictedPeriod();
        out.frozenPreviousPeriodSamples = m_frozenPreviousPeriod;
        out.reacquiringFromFrozenPeriod = m_stage != Stage::Locked
            && std::isfinite(m_frozenPreviousPeriod);
        if (m_stage != Stage::Locked || !std::isfinite(out.predictedPeriodSamples)) {
            out.useGlobalSearch = true;
            return out;
        }

        out.useGlobalSearch = false;
        static constexpr int widths[] = {8, 16, 32};
        out.halfWidthSamples = widths[std::clamp(m_lockMisses, 0, 2)];
        return out;
    }

    // 首次冷启动专用：保留最近3个GLOBAL帧的全部可信周期候选，按周期聚类。
    // 排序优先级为“跨帧出现次数 -> fundamental周期族票数 -> 平均身份质量”；若一个
    // 较大簇约等于另一个至少2帧稳定小簇的2x/3x，则把较大簇视为谐波别名，优先小簇。
    // 该逻辑只用于没有frozen previous period的首次启动，不参与LOST重捕获或LOCK。
    StartupConsensus observeStartupCandidates(const std::vector<StartupCandidate> &candidates)
    {
        if (m_stage == Stage::Locked || std::isfinite(m_frozenPreviousPeriod))
            return {};

        std::vector<StartupCandidate> frame;
        frame.reserve(candidates.size());
        for (const auto &candidate : candidates) {
            if (std::isfinite(candidate.periodSamples) && candidate.periodSamples > 0.0)
                frame.push_back(candidate);
        }
        std::sort(frame.begin(), frame.end(), [](const StartupCandidate &a, const StartupCandidate &b) {
            return a.periodSamples < b.periodSamples;
        });
        m_startupFrames.push_back(std::move(frame));
        if (m_startupFrames.size() > 3)
            m_startupFrames.erase(m_startupFrames.begin());

        struct Point {
            double period = 0.0;
            double score = 0.0;
            bool periodic = false;
            int frame = 0;
        };
        std::vector<Point> points;
        for (int frameIndex = 0; frameIndex < static_cast<int>(m_startupFrames.size()); ++frameIndex) {
            for (const auto &candidate : m_startupFrames[static_cast<size_t>(frameIndex)])
                points.push_back({candidate.periodSamples, candidate.score,
                                  candidate.periodicFamily, frameIndex});
        }
        std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) {
            return a.period < b.period;
        });

        struct Cluster {
            double weightedPeriod = 0.0;
            double weight = 0.0;
            double scoreSum = 0.0;
            int scoreCount = 0;
            int periodicVotes = 0;
            std::vector<int> frames;

            double period() const { return weight > 0.0 ? weightedPeriod / weight : 0.0; }
            double meanScore() const { return scoreCount > 0 ? scoreSum / scoreCount : 0.0; }
            int frameVotes() const { return static_cast<int>(frames.size()); }
        };
        std::vector<Cluster> clusters;
        for (const auto &point : points) {
            int best = -1;
            double bestResidual = std::numeric_limits<double>::infinity();
            for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
                const double center = clusters[static_cast<size_t>(i)].period();
                const double gate = std::max(12.0, 0.05 * std::abs(center));
                const double residual = std::abs(point.period - center);
                if (residual <= gate && residual < bestResidual) {
                    bestResidual = residual;
                    best = i;
                }
            }
            if (best < 0) {
                Cluster cluster;
                const double weight = std::max(0.20, point.score);
                cluster.weightedPeriod = point.period * weight;
                cluster.weight = weight;
                cluster.scoreSum = point.score;
                cluster.scoreCount = 1;
                cluster.periodicVotes = point.periodic ? 1 : 0;
                cluster.frames.push_back(point.frame);
                clusters.push_back(std::move(cluster));
                continue;
            }

            auto &cluster = clusters[static_cast<size_t>(best)];
            const double weight = std::max(0.20, point.score);
            cluster.weightedPeriod += point.period * weight;
            cluster.weight += weight;
            cluster.scoreSum += point.score;
            ++cluster.scoreCount;
            if (point.periodic)
                ++cluster.periodicVotes;
            if (std::find(cluster.frames.begin(), cluster.frames.end(), point.frame) == cluster.frames.end())
                cluster.frames.push_back(point.frame);
        }

        StartupConsensus out;
        out.framesObserved = static_cast<int>(m_startupFrames.size());
        out.clusterCount = static_cast<int>(clusters.size());
        if (out.framesObserved < 2)
            return out;

        auto harmonicOfStableSmaller = [&](int index) {
            const auto &larger = clusters[static_cast<size_t>(index)];
            for (int j = 0; j < static_cast<int>(clusters.size()); ++j) {
                if (j == index)
                    continue;
                const auto &smaller = clusters[static_cast<size_t>(j)];
                if (smaller.frameVotes() < 2 || !(smaller.period() < larger.period()))
                    continue;
                for (int multiple = 2; multiple <= 3; ++multiple) {
                    const double target = multiple * smaller.period();
                    const double gate = std::max(12.0, 0.05 * target);
                    if (std::abs(larger.period() - target) <= gate)
                        return true;
                }
            }
            return false;
        };

        int best = -1;
        for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
            const auto &candidate = clusters[static_cast<size_t>(i)];
            if (candidate.frameVotes() < 2 || harmonicOfStableSmaller(i))
                continue;
            if (best < 0) {
                best = i;
                continue;
            }
            const auto &current = clusters[static_cast<size_t>(best)];
            if (candidate.frameVotes() != current.frameVotes()) {
                if (candidate.frameVotes() > current.frameVotes())
                    best = i;
                continue;
            }
            if (candidate.periodicVotes != current.periodicVotes) {
                if (candidate.periodicVotes > current.periodicVotes)
                    best = i;
                continue;
            }
            if (std::abs(candidate.meanScore() - current.meanScore()) > 1e-9) {
                if (candidate.meanScore() > current.meanScore())
                    best = i;
                continue;
            }
            if (candidate.period() < current.period())
                best = i;
        }
        if (best < 0)
            return out;

        const auto &winner = clusters[static_cast<size_t>(best)];
        out.available = true;
        out.periodSamples = winner.period();
        out.frameVotes = winner.frameVotes();
        out.periodicFamilyVotes = winner.periodicVotes;
        out.meanScore = winner.meanScore();
        return out;
    }

    void clearStartupConsensus()
    {
        m_startupFrames.clear();
    }

    // GLOBAL/WARMUP候选只来自当前帧独立Echo2全局搜索。返回true表示本帧完成锁定，
    // 可以正式输出；首个seed只建立WARMUP，不输出，避免单帧偶然相关直接建锁。
    bool observePeriodCandidate(double periodSamples, int requiredConfirmations = 2)
    {
        return observeGlobalCandidate(periodSamples, requiredConfirmations);
    }

    bool observeGlobalCandidate(double periodSamples, int requiredConfirmations = 2)
    {
        if (!std::isfinite(periodSamples) || periodSamples <= 0.0)
            return false;
        requiredConfirmations = std::clamp(requiredConfirmations, 2, 4);

        if (m_stage == Stage::Locked)
            return observeLocked(periodSamples);

        if (m_stage == Stage::GlobalSearch || !std::isfinite(m_pendingPeriod)) {
            m_stage = Stage::Warmup;
            m_pendingPeriod = periodSamples;
            m_pendingCount = 1;
            m_pendingMisses = 0;
            return false;
        }

        const double gate = std::max(8.0, 0.04 * std::abs(m_pendingPeriod));
        if (std::abs(periodSamples - m_pendingPeriod) > gate) {
            // 新候选与上一seed不一致：重新以当前候选开始两帧确认，不沿错误族累计。
            m_pendingPeriod = periodSamples;
            m_pendingCount = 1;
            m_pendingMisses = 0;
            return false;
        }

        m_pendingPeriod = 0.5 * m_pendingPeriod + 0.5 * periodSamples;
        ++m_pendingCount;
        m_pendingMisses = 0;
        if (m_pendingCount < requiredConfirmations)
            return false;

        m_stage = Stage::Locked;
        m_periodHistory.clear();
        m_periodHistory.push_back(m_pendingPeriod);
        m_periodHistory.push_back(periodSamples);
        m_lockMisses = 0;
        m_pendingCount = 0;
        m_pendingMisses = 0;
        m_frozenPreviousPeriod = std::numeric_limits<double>::quiet_NaN();
        m_startupFrames.clear();
        return true;
    }

    bool observeLocked(double periodSamples)
    {
        if (!std::isfinite(periodSamples) || periodSamples <= 0.0)
            return false;
        m_stage = Stage::Locked;
        m_lockMisses = 0;
        m_expandedRecoveryPendingPeriod = std::numeric_limits<double>::quiet_NaN();
        m_periodHistory.push_back(periodSamples);
        constexpr size_t MaxHistory = 9;
        if (m_periodHistory.size() > MaxHistory)
            m_periodHistory.erase(m_periodHistory.begin());
        return true;
    }

    void setExpandedRecoveryPending(double periodSamples)
    {
        if (m_stage == Stage::Locked && std::isfinite(periodSamples) && periodSamples > 0.0)
            m_expandedRecoveryPendingPeriod = periodSamples;
    }

    bool hasExpandedRecoveryPending() const
    {
        return std::isfinite(m_expandedRecoveryPendingPeriod);
    }

    double expandedRecoveryPendingPeriod() const
    {
        return m_expandedRecoveryPendingPeriod;
    }

    bool expandedRecoveryMatches(double periodSamples) const
    {
        if (!std::isfinite(periodSamples) || !std::isfinite(m_expandedRecoveryPendingPeriod))
            return false;
        const double gate = std::max(6.0, 0.01 * std::abs(m_expandedRecoveryPendingPeriod));
        return std::abs(periodSamples - m_expandedRecoveryPendingPeriod) <= gate;
    }

    bool observeConfirmedExpandedRecovery(double periodSamples)
    {
        if (m_stage != Stage::Locked || !std::isfinite(periodSamples) || periodSamples <= 0.0)
            return false;
        m_stage = Stage::Locked;
        m_lockMisses = 0;
        m_periodHistory.clear();
        m_periodHistory.push_back(periodSamples);
        m_periodHistory.push_back(periodSamples);
        m_expandedRecoveryPendingPeriod = std::numeric_limits<double>::quiet_NaN();
        return true;
    }

    void observeMiss()
    {
        if (m_stage == Stage::Locked) {
            ++m_lockMisses;
            if (m_lockMisses >= 3) {
                // LOST前冻结最后一个稳定median周期。GLOBAL_REACQUIRE不把它当作硬搜索窗，
                // 只作为身份先验：优先回到同一fundamental，并拒绝直接锁到2x/3x谐波族。
                m_frozenPreviousPeriod = predictedPeriod();
                m_stage = Stage::GlobalSearch;
                m_periodHistory.clear();
                m_startupFrames.clear();
                m_expandedRecoveryPendingPeriod = std::numeric_limits<double>::quiet_NaN();
                m_pendingPeriod = std::numeric_limits<double>::quiet_NaN();
                m_pendingCount = 0;
                m_pendingMisses = 0;
                m_lockMisses = 0;
            }
            return;
        }

        if (m_stage == Stage::Warmup) {
            ++m_pendingMisses;
            if (m_pendingMisses > 2) {
                m_stage = Stage::GlobalSearch;
                m_pendingPeriod = std::numeric_limits<double>::quiet_NaN();
                m_pendingCount = 0;
                m_pendingMisses = 0;
            }
        }
    }

    Stage stage() const { return m_stage; }
    bool locked() const { return m_stage == Stage::Locked; }
    int historyCount() const { return static_cast<int>(m_periodHistory.size()); }
    int consecutiveMisses() const { return m_stage == Stage::Locked ? m_lockMisses : m_pendingMisses; }
    bool hasFrozenPreviousPeriod() const { return std::isfinite(m_frozenPreviousPeriod); }
    double frozenPreviousPeriod() const { return m_frozenPreviousPeriod; }

    double predictedPeriod() const
    {
        if (m_periodHistory.empty())
            return std::numeric_limits<double>::quiet_NaN();
        std::vector<double> values = m_periodHistory;
        const size_t mid = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
        if (values.size() % 2 == 1)
            return values[mid];
        const double upper = values[mid];
        const double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
        return 0.5 * (lower + upper);
    }

    static const char *stageName(Stage stage)
    {
        switch (stage) {
        case Stage::GlobalSearch: return "GLOBAL_SEARCH";
        case Stage::Warmup: return "WARMUP";
        case Stage::Locked: return "LOCK";
        }
        return "GLOBAL_SEARCH";
    }

private:
    Stage m_stage = Stage::GlobalSearch;
    std::vector<double> m_periodHistory;
    double m_pendingPeriod = std::numeric_limits<double>::quiet_NaN();
    int m_pendingCount = 0;
    int m_pendingMisses = 0;
    int m_lockMisses = 0;
    double m_frozenPreviousPeriod = std::numeric_limits<double>::quiet_NaN();
    double m_expandedRecoveryPendingPeriod = std::numeric_limits<double>::quiet_NaN();
    std::vector<std::vector<StartupCandidate>> m_startupFrames;
};
