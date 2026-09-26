#pragma once

#include <vector>
#include <limits>

// 独立波包候选检测。
// 从LSTAlgorithmTest中的packet segmentation提取，仅保留实时测厚需要的
// 第一候选、第二候选以及显著性比(candidateRatio)。

struct CandidateDetectorParameters
{
    // 对应LSTAlgorithmTest参数，不改变原判据。
    double minLocalSnrDb = 3.0;
    double packetEnergyFraction = 0.25;
    int packetMergeGap = 3;
    int candidateSeparationSamples = 12;
    double minCandidateRatio = 1.5;
};

struct CandidateResult
{
    bool valid = false;
    int firstIndex = -1;
    int secondIndex = -1;
    int count = 0;
    double firstSignificance = 0.0;
    double secondSignificance = 0.0;
    double ratio = std::numeric_limits<double>::infinity();
    bool ambiguous = false;
};

class CandidateDetector
{
public:
    CandidateResult detect(const std::vector<double>& signal,
                           int left,
                           int right,
                           double noiseEnergy,
                           const CandidateDetectorParameters& parameters,
                           int predictionLeft = -1,
                           int predictionRight = -1,
                           bool lockActive = false) const;
};
