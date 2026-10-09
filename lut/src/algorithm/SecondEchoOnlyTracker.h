#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

class SecondEchoOnlyTracker
{
public:
    enum class Stage { GlobalSearch, Warmup, Locked };

    struct Config {
        int startupConfirmations = 2;
        int startupWindowFrames = 3;
        double startupConsistencySamples = 12.0;
        int predictionHalfWidth = 16;
        int maxMissesBeforeGlobal = 3;
        double strongPeriodResidualSamples = 4.0;
        double acceptablePeriodResidualSamples = 10.0;
        double minCandidateSnrDb = 4.5;
        double lockedWeakCandidateSnrDb = 2.5;
        double lockMinTemplateCorrelation = 0.82;
        double reacquireMinTemplateCorrelation = 0.78;
        double templateUpdateMinCorrelation = 0.88;
        double uncorroboratedCertificationCorrelation = 0.90;
        double referencePeriodResidualSamples = 48.0;
        double templateEmaAlpha = 0.20;
        double uncorroboratedRecenterThresholdSamples = 6.0;
        double uncorroboratedRecenterConsistencySamples = 4.0;
        int uncorroboratedRecenterConfirmFrames = 2;
    };

    struct Candidate {
        double tofSamples = nan();
        double snrDb = nan();
        double score = 0.0;
        int rank = -1;
        std::vector<double> envelopeTemplate;
    };

    struct Frame {
        std::vector<Candidate> candidates;
        double referenceTofSamples = nan();

        bool periodEvidenceAvailable = false;
        double formalT1TofSamples = nan();
        double periodSamples = nan();
        // 当高质量DeltaT不可用时，允许用“Formal T1 + 产品参考厚度对应的参考周期”
        // 作为较弱的ordinal佐证。它只用于身份，不进入T2厚度公式。
        double referencePeriodSamples = nan();

        bool t2T3EvidenceAvailable = false;
        double t3TofSamples = nan();
    };

    struct Decision {
        bool candidateFound = false;
        bool available = false;
        bool certified = false;
        bool deltaTStrongSupport = false;
        bool deltaTAcceptableSupport = false;
        bool t2T3Support = false;
        bool referenceT1Support = false;

        Stage stage = Stage::GlobalSearch;
        double tofSamples = nan();
        double selectedSnrDb = nan();
        double selectedScore = nan();
        int selectedRank = -1;
        double predictedTofSamples = nan();
        double periodResidualSamples = nan();
        double t2T3ResidualSamples = nan();
        double templateCorrelation = nan();
        int startupVotes = 0;
        int consecutiveMisses = 0;
        const char *reason = "NO_T2_CANDIDATE";
    };

    SecondEchoOnlyTracker() = default;
    explicit SecondEchoOnlyTracker(const Config &cfg) : m_cfg(cfg) {}

    void reset()
    {
        m_stage = Stage::GlobalSearch;
        m_history.clear();
        m_pendingCenters.clear();
        m_frozenTofSamples = nan();
        m_template.clear();
        m_misses = 0;
        m_recenterPendingTof = nan();
        m_recenterPendingCount = 0;
    }

    Decision process(const Frame &f)
    {
        Decision out;
        out.stage = m_stage;
        out.predictedTofSamples = predictedTof();

        // LOCK阶段只有“当前真实周期”证据可以打断时序预测重新做全局候选竞争。
        // Formal T1 + reference period 只是弱身份先验，只用于GLOBAL/WARMUP/重捕获，
        // 不能让已经LOCK的T2每帧在大窗口内重新跳峰。
        const bool currentOrdinalEvidence =
            (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
             && std::isfinite(f.periodSamples))
            || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                && std::isfinite(f.periodSamples));

        const Candidate *selected = nullptr;
        if (m_stage == Stage::Locked && currentOrdinalEvidence) {
            // LOCK也服从当前物理ordinal证据：有DeltaT/T2-T3证据时，优先选与序列一致的
            // T2候选，防止预测链在弱信号段沿着错误近邻packet自洽漂移。
            selected = chooseGlobalCandidate(f);
        } else if (m_stage == Stage::Locked && std::isfinite(out.predictedTofSamples)) {
            static constexpr int expand[] = {1, 2, 4};
            const int factor = expand[std::clamp(m_misses, 0, 2)];
            const double gate = factor * std::max(1, m_cfg.predictionHalfWidth);
            selected = chooseLockedCandidate(f.candidates, out.predictedTofSamples, gate);
        } else {
            selected = chooseGlobalCandidate(f);
        }

        // 没有任何序列佐证时，连续跟踪只能在参考T2附近的身份区内延续。
        // 超出该区说明旧预测可能已沿着近邻packet漂移；此时必须LOST等待period/重新认证，
        // 不能把“大搜索窗允许发现”误解成“任何大窗峰都属于同一T2”。
        constexpr double uncorroboratedIdentityGateSamples = 128.0;
        if (selected && !currentOrdinalEvidence && std::isfinite(f.referenceTofSamples)
            && std::abs(selected->tofSamples - f.referenceTofSamples)
                > uncorroboratedIdentityGateSamples) {
            selected = nullptr;
        }

        if (!selected) {
            observeMiss();
            out.stage = m_stage;
            out.consecutiveMisses = m_misses;
            out.reason = m_stage == Stage::Locked
                ? "T2_LOCK_CANDIDATE_MISSING" : "T2_GLOBAL_CANDIDATE_MISSING";
            return out;
        }

        // LOCK中没有当前period佐证时，大幅recenter先做两帧确认，避免单帧跳到同窗
        // 的另一packet后下一帧又返回。真实厚度台阶若持续存在，第二帧即可确认通过。
        if (m_stage == Stage::Locked && !currentOrdinalEvidence
            && std::isfinite(out.predictedTofSamples)
            && std::abs(selected->tofSamples - out.predictedTofSamples)
                > m_cfg.uncorroboratedRecenterThresholdSamples) {
            if (!std::isfinite(m_recenterPendingTof)
                || std::abs(selected->tofSamples - m_recenterPendingTof)
                    > m_cfg.uncorroboratedRecenterConsistencySamples) {
                m_recenterPendingTof = selected->tofSamples;
                m_recenterPendingCount = 1;
                out.candidateFound = true;
                out.tofSamples = selected->tofSamples;
                out.selectedSnrDb = selected->snrDb;
                out.selectedScore = selected->score;
                out.selectedRank = selected->rank;
                out.templateCorrelation = candidateTemplateCorrelation(*selected);
                out.reason = "T2_LOCK_RECENTER_PENDING";
                return out;
            }
            ++m_recenterPendingCount;
            if (m_recenterPendingCount < m_cfg.uncorroboratedRecenterConfirmFrames) {
                out.candidateFound = true;
                out.tofSamples = selected->tofSamples;
                out.reason = "T2_LOCK_RECENTER_PENDING";
                return out;
            }
            m_recenterPendingTof = nan();
            m_recenterPendingCount = 0;
        } else {
            m_recenterPendingTof = nan();
            m_recenterPendingCount = 0;
        }

        out.candidateFound = true;
        out.tofSamples = selected->tofSamples;
        out.selectedSnrDb = selected->snrDb;
        out.selectedScore = selected->score;
        out.selectedRank = selected->rank;
        out.templateCorrelation = candidateTemplateCorrelation(*selected);

        applyEvidence(f, out);

        if (m_stage == Stage::GlobalSearch || m_stage == Stage::Warmup) {
            // 当前帧若已有强DeltaT/T2-T3 ordinal证据，候选身份已经被独立序列证据
            // 直接认证，不必再等待2-of-3。注意：证据只决定“这是T2”，厚度仍只用T2 TOF。
            const bool directIdentity = out.deltaTStrongSupport || out.t2T3Support;
            bool confirmed = false;
            if (directIdentity) {
                m_stage = Stage::Locked;
                m_history.clear();
                pushHistory(out.tofSamples);
                m_pendingCenters.clear();
                m_frozenTofSamples = nan();
                m_misses = 0;
                confirmed = true;
            } else {
                confirmed = observeStartup(out.tofSamples);
            }
            out.stage = m_stage;
            out.startupVotes = directIdentity ? 1 : static_cast<int>(m_pendingCenters.size());
            out.available = confirmed;
            if (confirmed)
                updateTemplate(selected->envelopeTemplate, true);
            out.certified = confirmed && identityCertified(out);
            out.reason = confirmed
                ? (directIdentity ? "T2_STARTUP_DIRECT_PERIOD_CERTIFIED"
                                  : (out.certified ? "T2_STARTUP_CERTIFIED" : "T2_STARTUP_TRACKED"))
                : "T2_STARTUP_PENDING";
            return out;
        }

        // LOCK: temporal identity is already established. DeltaT remains supporting evidence only.
        m_misses = 0;
        pushHistory(out.tofSamples);
        if (out.deltaTAcceptableSupport || out.t2T3Support
            || (std::isfinite(out.templateCorrelation)
                && out.templateCorrelation >= m_cfg.templateUpdateMinCorrelation)) {
            updateTemplate(selected->envelopeTemplate, false);
        }
        out.available = true;
        out.certified = identityCertified(out);
        out.stage = m_stage;
        out.reason = out.certified ? "T2_LOCK_CERTIFIED" : "T2_LOCK_TRACKED";
        return out;
    }

    Stage stage() const { return m_stage; }

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
    static double nan() { return std::numeric_limits<double>::quiet_NaN(); }

    const Candidate *chooseGlobalCandidate(const Frame &f) const
    {
        const Candidate *bestSupported = nullptr;
        double bestSupportedScore = -std::numeric_limits<double>::infinity();

        // GLOBAL/WARMUP优先使用当前序列证据确定ordinal。只要本帧已有可靠DeltaT
        // 或T2->T3 period证据，就不允许一个与period矛盾的更强峰先建立错误T2锁。
        const bool hasOrdinalEvidence =
            (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
             && std::isfinite(f.periodSamples))
            || (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                && std::isfinite(f.referencePeriodSamples))
            || (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                && std::isfinite(f.periodSamples));
        if (hasOrdinalEvidence) {
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                    || c.snrDb < m_cfg.minCandidateSnrDb)
                    continue;

                bool supported = false;
                double residualPenalty = 0.0;
                if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.periodSamples)) {
                    const double r = std::abs(
                        c.tofSamples - (f.formalT1TofSamples + f.periodSamples));
                    if (r <= m_cfg.acceptablePeriodResidualSamples) {
                        supported = true;
                        residualPenalty += 0.05 * r;
                    }
                }
                if (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
                    && std::isfinite(f.referencePeriodSamples)) {
                    const double r = std::abs(
                        c.tofSamples - (f.formalT1TofSamples + f.referencePeriodSamples));
                    if (r <= m_cfg.referencePeriodResidualSamples) {
                        supported = true;
                        residualPenalty += 0.015 * r;
                    }
                }
                if (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
                    && std::isfinite(f.periodSamples)) {
                    const double r = std::abs(
                        f.t3TofSamples - c.tofSamples - f.periodSamples);
                    if (r <= m_cfg.acceptablePeriodResidualSamples) {
                        supported = true;
                        residualPenalty += 0.05 * r;
                    }
                }
                if (!supported)
                    continue;

                const double composite = c.score - residualPenalty;
                if (!bestSupported || composite > bestSupportedScore) {
                    bestSupported = &c;
                    bestSupportedScore = composite;
                }
            }
            // 有ordinal证据却找不到一致候选时，宁可等待，不允许远处强峰建锁。
            return bestSupported;
        }

        if (std::isfinite(m_frozenTofSamples)) {
            const Candidate *bestFrozen = nullptr;
            double bestFrozenResidual = std::numeric_limits<double>::infinity();
            double bestFrozenScore = -std::numeric_limits<double>::infinity();
            constexpr double frozenGateSamples = 128.0;
            for (const auto &c : f.candidates) {
                if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                    || c.snrDb < m_cfg.minCandidateSnrDb)
                    continue;
                const double r = std::abs(c.tofSamples - m_frozenTofSamples);
                if (r > frozenGateSamples)
                    continue;
                const double corr = candidateTemplateCorrelation(c);
                if (!m_template.empty()
                    && (!std::isfinite(corr)
                        || corr < m_cfg.reacquireMinTemplateCorrelation))
                    continue;
                if (!bestFrozen || r < bestFrozenResidual - 1e-9
                    || (std::abs(r - bestFrozenResidual) <= 1e-9
                        && c.score > bestFrozenScore)) {
                    bestFrozen = &c;
                    bestFrozenResidual = r;
                    bestFrozenScore = c.score;
                }
            }
            return bestFrozen;
        }

        const Candidate *best = nullptr;
        double bestComposite = -std::numeric_limits<double>::infinity();
        constexpr double startupReferenceGateSamples = 128.0;
        for (const auto &c : f.candidates) {
            if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                || c.snrDb < m_cfg.minCandidateSnrDb)
                continue;

            double composite = c.score;
            if (std::isfinite(f.referenceTofSamples)) {
                const double residual = std::abs(c.tofSamples - f.referenceTofSamples);
                // 大窗口负责“发现”，建锁种子仍必须靠近参考厚度的物理T2中心；
                // 真正更远的新厚度只能由period证据或已建立的连续T2身份带过去。
                if (residual > startupReferenceGateSamples)
                    continue;
                composite -= 0.0040 * residual;
            }

            if (!best || composite > bestComposite) {
                best = &c;
                bestComposite = composite;
            }
        }
        return best;
    }

    const Candidate *chooseLockedCandidate(
        const std::vector<Candidate> &candidates,
        double predicted,
        double gate) const
    {
        const Candidate *best = nullptr;
        double bestResidual = std::numeric_limits<double>::infinity();
        double bestScore = -std::numeric_limits<double>::infinity();
        for (const auto &c : candidates) {
            if (!std::isfinite(c.tofSamples) || !std::isfinite(c.snrDb)
                || c.snrDb < m_cfg.lockedWeakCandidateSnrDb)
                continue;
            const double r = std::abs(c.tofSamples - predicted);
            if (r > gate)
                continue;
            const double corr = candidateTemplateCorrelation(c);
            if (!m_template.empty()
                && (!std::isfinite(corr)
                    || corr < m_cfg.lockMinTemplateCorrelation))
                continue;
            if (!best || r < bestResidual - 1e-9
                || (std::abs(r - bestResidual) <= 1e-9 && c.score > bestScore)) {
                best = &c;
                bestResidual = r;
                bestScore = c.score;
            }
        }
        return best;
    }

    void applyEvidence(const Frame &f, Decision &out) const
    {
        if (f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
            && std::isfinite(f.periodSamples)) {
            out.periodResidualSamples = out.tofSamples
                - (f.formalT1TofSamples + f.periodSamples);
            const double a = std::abs(out.periodResidualSamples);
            out.deltaTStrongSupport = a <= m_cfg.strongPeriodResidualSamples;
            out.deltaTAcceptableSupport = a <= m_cfg.acceptablePeriodResidualSamples;
        }

        if (!f.periodEvidenceAvailable && std::isfinite(f.formalT1TofSamples)
            && std::isfinite(f.referencePeriodSamples)) {
            const double residual = out.tofSamples
                - (f.formalT1TofSamples + f.referencePeriodSamples);
            out.referenceT1Support = std::abs(residual)
                <= m_cfg.referencePeriodResidualSamples;
        }

        if (f.t2T3EvidenceAvailable && std::isfinite(f.t3TofSamples)
            && std::isfinite(f.periodSamples)) {
            out.t2T3ResidualSamples = f.t3TofSamples
                - out.tofSamples - f.periodSamples;
            out.t2T3Support = std::abs(out.t2T3ResidualSamples)
                <= m_cfg.acceptablePeriodResidualSamples;
        }
    }

    bool identityCertified(const Decision &d) const
    {
        // DeltaT/period只是增强身份证据；没有它时，稳定LOCK仍允许T2-only输出。
        return d.deltaTAcceptableSupport || d.t2T3Support || d.referenceT1Support
            || (m_stage == Stage::Locked && m_history.size() >= 4
                && std::isfinite(d.templateCorrelation)
                && d.templateCorrelation >= m_cfg.uncorroboratedCertificationCorrelation);
    }

    bool observeStartup(double tof)
    {
        if (!std::isfinite(tof))
            return false;
        if (m_stage == Stage::GlobalSearch) {
            m_stage = Stage::Warmup;
            m_pendingCenters.clear();
            m_pendingCenters.push_back(tof);
            return false;
        }

        const double center = median(m_pendingCenters);
        if (std::abs(tof - center) > m_cfg.startupConsistencySamples) {
            m_pendingCenters.clear();
            m_pendingCenters.push_back(tof);
            return false;
        }

        m_pendingCenters.push_back(tof);
        while (static_cast<int>(m_pendingCenters.size()) > m_cfg.startupWindowFrames)
            m_pendingCenters.pop_front();

        if (static_cast<int>(m_pendingCenters.size()) < m_cfg.startupConfirmations)
            return false;

        m_stage = Stage::Locked;
        m_history.clear();
        for (double v : m_pendingCenters)
            pushHistory(v);
        m_pendingCenters.clear();
        m_frozenTofSamples = nan();
        m_misses = 0;
        return true;
    }

    void observeMiss()
    {
        if (m_stage == Stage::Locked) {
            ++m_misses;
            if (m_misses >= m_cfg.maxMissesBeforeGlobal) {
                m_frozenTofSamples = predictedTof();
                if (!std::isfinite(m_frozenTofSamples) && !m_history.empty())
                    m_frozenTofSamples = m_history.back();
                m_stage = Stage::GlobalSearch;
                m_history.clear();
                m_pendingCenters.clear();
                m_misses = 0;
            }
        }
    }

    double candidateTemplateCorrelation(const Candidate &candidate) const
    {
        if (m_template.empty() || candidate.envelopeTemplate.size() != m_template.size()
            || candidate.envelopeTemplate.empty())
            return nan();
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < m_template.size(); ++i) {
            meanA += m_template[i];
            meanB += candidate.envelopeTemplate[i];
        }
        meanA /= static_cast<double>(m_template.size());
        meanB /= static_cast<double>(m_template.size());
        double num = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < m_template.size(); ++i) {
            const double a = m_template[i] - meanA;
            const double b = candidate.envelopeTemplate[i] - meanB;
            num += a * b;
            aa += a * a;
            bb += b * b;
        }
        return aa > 1e-24 && bb > 1e-24 ? num / std::sqrt(aa * bb) : nan();
    }

    void updateTemplate(const std::vector<double> &shape, bool force)
    {
        if (shape.empty())
            return;
        if (force || m_template.size() != shape.size()) {
            m_template = shape;
            return;
        }
        const double alpha = std::clamp(m_cfg.templateEmaAlpha, 0.01, 1.0);
        for (size_t i = 0; i < shape.size(); ++i)
            m_template[i] = (1.0 - alpha) * m_template[i] + alpha * shape[i];
    }

    void pushHistory(double tof)
    {
        if (!std::isfinite(tof))
            return;
        m_history.push_back(tof);
        while (m_history.size() > 7)
            m_history.pop_front();
    }

    double predictedTof() const
    {
        if (m_history.empty())
            return nan();
        if (m_history.size() < 3)
            return m_history.back();

        std::vector<double> velocity;
        for (size_t i = 1; i < m_history.size(); ++i)
            velocity.push_back(m_history[i] - m_history[i - 1]);
        const double v = median(velocity);
        return m_history.back() + v;
    }

    static double median(const std::deque<double> &values)
    {
        return median(std::vector<double>(values.begin(), values.end()));
    }

    static double median(std::vector<double> values)
    {
        if (values.empty())
            return nan();
        std::sort(values.begin(), values.end());
        const size_t m = values.size() / 2;
        return values.size() % 2
            ? values[m] : 0.5 * (values[m - 1] + values[m]);
    }

    Config m_cfg;
    Stage m_stage = Stage::GlobalSearch;
    std::deque<double> m_history;
    std::deque<double> m_pendingCenters;
    double m_frozenTofSamples = nan();
    std::vector<double> m_template;
    int m_misses = 0;
    double m_recenterPendingTof = nan();
    int m_recenterPendingCount = 0;
};
