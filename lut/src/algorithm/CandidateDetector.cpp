#include "CandidateDetector.h"

#include <algorithm>
#include <cmath>

CandidateResult CandidateDetector::detect(const std::vector<double>& signal,
                                          int left,
                                          int right,
                                          double noiseEnergy,
                                          const CandidateDetectorParameters& parameters,
                                          int predictionLeft,
                                          int predictionRight,
                                          bool lockActive) const
{
    CandidateResult result;
    if (signal.empty())
        return result;

    left = std::max(1, left);
    right = std::min(right, static_cast<int>(signal.size()) - 2);
    if (left >= right)
        return result;

    struct PacketCandidate {
        int index = -1;
        double significance = 0.0;
    };

    std::vector<PacketCandidate> candidates;

    // 保持测试程序逻辑：LOCK状态优先使用prediction gate，避免旁瓣/第二回波竞争。
    int searchLeft = left;
    int searchRight = right;
    if (lockActive && predictionLeft >= left && predictionRight <= right) {
        searchLeft = predictionLeft;
        searchRight = predictionRight;
    }

    const double noiseThreshold = std::max(noiseEnergy *
        std::pow(10.0, parameters.minLocalSnrDb / 10.0), 1e-18);

    double referenceEnergy = 0.0;
    for (int i = searchLeft; i <= searchRight; ++i)
        referenceEnergy = std::max(referenceEnergy, signal[i] * signal[i]);

    const double segmentationThreshold = std::max(
        noiseThreshold,
        parameters.packetEnergyFraction * referenceEnergy);

    // 与测试程序一致：基于局部能量寻找独立候选峰。
    for (int i = searchLeft + 1; i < searchRight; ++i) {
        const double energy = signal[i] * signal[i];
        if (energy < segmentationThreshold)
            continue;

        if (energy >= signal[i - 1] * signal[i - 1]
            && energy >= signal[i + 1] * signal[i + 1]) {
            candidates.push_back({i, energy});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PacketCandidate& a, const PacketCandidate& b) {
                  return a.significance > b.significance;
              });

    if (candidates.empty())
        return result;

    result.firstIndex = candidates.front().index;
    result.firstSignificance = candidates.front().significance;
    result.count = 1;

    for (size_t i = 1; i < candidates.size(); ++i) {
        if (std::abs(candidates[i].index - result.firstIndex)
            >= parameters.candidateSeparationSamples) {
            result.secondIndex = candidates[i].index;
            result.secondSignificance = candidates[i].significance;
            result.count = 2;
            break;
        }
    }

    if (result.secondSignificance > 1e-18)
        result.ratio = result.firstSignificance / result.secondSignificance;

    result.ambiguous = result.ratio < parameters.minCandidateRatio;
    result.valid = !result.ambiguous;
    return result;
}
