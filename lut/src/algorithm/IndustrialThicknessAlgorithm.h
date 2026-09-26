#pragma once

#include <vector>
#include <deque>
#include <limits>

#include "PacketPeakFit.h"
#include "PeakTracker.h"
#include "CandidateDetector.h"

// 工业在线测厚算法核心接口。
// 从LSTAlgorithmTest中提取：Raw脉冲检测、SNR质量评价、波包寻峰、
// 亚采样定位和跨帧模板跟踪。测试界面的失败原因、拟合曲线显示不迁移。

struct IndustrialThicknessResult
{
    bool valid = false;
    bool noSignal = false;
    bool invalidPulse = false;
    double thicknessMm = std::numeric_limits<double>::quiet_NaN();
    double tofSample = std::numeric_limits<double>::quiet_NaN();
    double snrDb = std::numeric_limits<double>::quiet_NaN();

    // 候选波包检测质量参数（迁移自LSTAlgorithmTest）
    int candidateCount = 0;
    double candidateRatio = std::numeric_limits<double>::quiet_NaN();
    double firstSignificance = std::numeric_limits<double>::quiet_NaN();
    double secondSignificance = std::numeric_limits<double>::quiet_NaN();

    // 拟合与跨帧跟踪质量参数
    double fitR2 = std::numeric_limits<double>::quiet_NaN();
    double trackingCorrelation = std::numeric_limits<double>::quiet_NaN();
};

class IndustrialThicknessAlgorithm
{
public:
    IndustrialThicknessAlgorithm() = default;

    IndustrialThicknessResult process(
        const std::vector<double>& raw,
        const std::vector<double>& filtered,
        double sampleRateHz,
        double thicknessVelocity,
        int delayCount);

    void reset();

private:
    bool detectImpulse(const std::vector<double>& raw);
    double estimateNoise(const std::vector<double>& signal,
                         int excludeLeft = -1,
                         int excludeRight = -1);
    double findPeak(const std::vector<double>& signal, int left, int right);

    std::deque<double> m_peakHistory;
    double m_noiseFloorDb = std::numeric_limits<double>::quiet_NaN();
    // 背景噪声能量，用于CandidateDetector门限计算。
    // 保持与LSTAlgorithmTest中的noiseEnergy定义一致。
    double m_noiseEnergy = std::numeric_limits<double>::quiet_NaN();

    // 等效高斯波包拟合与跨帧跟踪状态。
    PacketPeakFit::FitWorkspace m_fitWorkspace;
    PeakTracker m_tracker;
    CandidateDetector m_candidateDetector;
};
