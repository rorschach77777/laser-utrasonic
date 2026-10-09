#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <QString>

enum class ThicknessDetectionMode
{
    Auto,
    Peak,
    Valley
};

enum class ThicknessEchoMode
{
    T1Only,
    T2Only
};

// 与 LSTAlgorithmTest 界面默认值一致的核心测厚参数。
// 主程序已有的采样率、带通频率、FIR阶数、寻峰模式、参考厚度和Delay Count不在此重复。
struct ThicknessAlgorithmParameters
{
    // 与 LSTAlgorithmTest 默认参数保持一致：Raw差分MAD脉冲检测。
    double impulseMadMultiplier = 20.0;
    double impulseMinJumpV = 1.0;
    double impulseIsolationRatio = 20.0;
    int impulseGuardSamples = 8;

    // SNR/NoiseEnergy局部交流能量窗口，与LSTAlgorithmTest一致。
    int energyWindowSamples = 7;
    double minLocalSnrDb = 7.0;
    double lowSnrSoftMinDb = 6.0;
    double lowSnrSoftMinTrackingCorrelation = 0.90;
    double lowSnrSoftMaxResidualSamples = 2.0;
    double lowSnrSoftMinFitR2 = 0.95;
    double noiseMadRejectMultiplier = 3.0;
    double noiseEwmaAlphaUp = 0.30;
    double noiseEwmaAlphaDown = 0.15;

    double minCandidateRatio = 1.2;
    int candidateSeparationSamples = 12;
    int predictionHistorySize = 5;
    int predictionMinValid = 3;
    int predictionHalfWidth = 16;
    int predictionMaxMisses = 3;
    double trackingResidualAcceptSamples = 6.0;
    double trackingResidualRejectSamples = 9.0;
    int reacquireConfirmFrames = 2;
    double reacquireConsistencySamples = 5.0;

    double longitudinalVelocityMps = 6459.7;
    double echoMinCorrelation = 0.70;
    double echoMinSnrDb = 3.0;
    double echoMinRelativeEnergy = 0.03;
    double echoPeriodTolerance = 0.20;
    int packetMinWidth = 8;
    int packetMaxWidth = 50;
    int packetMergeGap = 4;
    double packetEnergyFraction = 0.05;
    int lockedPacketMaxCoreWidth = 60;
    double lockedBroadMinTrackingCorrelation = 0.80;
    double lockedBroadEnergyPeakToleranceSamples = 7.0;

    // LOCK碎片化/双瓣/拟合失败恢复参数。保持普通SEARCH门不放宽，
    // 只在稳定LOCK身份下使用RECENT模板相关ToF做高精度恢复。
    int lockedFragmentedCoreMinWidth = 4;
    double lockedFragmentedMinMaxSnrDb = 7.0;
    double lockedFragmentedSupportSnrDb = 2.5;
    double lockedFragmentedSupportEnergyFraction = 0.025;
    double lockedFragmentedMinTrackingCorrelation = 0.88;
    double lockedFragmentedMaxPredictionResidualSamples = 2.5;
    double lockedFragmentedMinFitR2 = 0.95;
    double lockRecoveryDirectCorrelation = 0.87;
    double lockRecoveryDirectResidualSamples = 1.0;
    double lockRecoveryPendingCorrelation = 0.85;
    double lockRecoveryPendingResidualSamples = 2.5;
    double lockRecoveryConfirmConsistencySamples = 2.0;
    double lockTemplateOnlyMinMaxSnrDb = 10.0;
    int lockStraddlingMaxSeparationSamples = 16;

    double fitFootFraction = 0.10;
    int fitFootConfirmSamples = 3;
    int fitFootSmoothRadius = 1;
    double fitCenterHalfRange = 0.75;
    double fitAmplitudeMinRatio = 0.75;
    double fitAmplitudeMaxRatio = 1.50;
    double fitMinR2 = 0.50;

    // 测厚回波模式。T1Only保持当前正式算法完全不变；T2Only启用独立第二回波分支。
    ThicknessEchoMode echoMode = ThicknessEchoMode::T1Only;

    // T2-only绝对厚度标定。DeltaT只作为身份/周期证据，不参与最终厚度输出。
    double echoPairBiasMm = 0.425577;
    double secondEchoOrdinalCorrectionMm = 0.000320;
    double secondEchoT50BiasMm = 0.0280164674111623;

    // FirstEcho最终输出层Raw carrier phase小增益修正；0表示关闭。
    // 该参数只作用于FinalPrimary之后的发布值，不回写Prediction/模板/状态机。
    double firstEchoPhaseRefinementAlpha = 0.13;

    int adaptiveWidthHistorySize = 21;
    int adaptiveWidthMinHistory = 7;
    double adaptiveWidthMadMultiplier = 4.0;
    double adaptiveWidthMinToleranceSamples = 4.5;
    double adaptiveWidthOutlierMultiplier = 2.5;

    double trackingMinCorrelation = 0.65;
    double trackingCorrelationMargin = 0.10;
    double trackingEmaTemplateAlpha = 0.15;
    double trackingShapeTemplateAlpha = 0.30;
    double trackingMultiTemplateAgreementSamples = 2.0;
    double trackingBackupTemplateMaxResidualSamples = 5.0;
    double trackingBackupRecentBlendAlpha = 0.25;
    double trackingSoftCorrelation = 0.53;
    double trackingSoftFitPredictionToleranceSamples = 3.0;
    double trackingSoftMinFitR2 = 0.80;
    double trackingSoftMinMaxSnrDb = 15.0;

    // 每次界面参数修改后递增；处理线程据此清空旧参数建立的跟踪状态。
    std::uint64_t revision = 0;
};

struct ThicknessResult
{
    bool valid = false;
    double thicknessMm = std::numeric_limits<double>::quiet_NaN();
    double peakTimeSec = std::numeric_limits<double>::quiet_NaN();
    double peakVoltage = std::numeric_limits<double>::quiet_NaN();
    double snrDb = std::numeric_limits<double>::quiet_NaN();
    int peakIndex = -1;

    // Final-primary + Identity守护shadow：正式thicknessMm已由同一决策链接管；
    // 这些字段保留用于主程序/测试程序逐帧一致性回归和后续防回退检查。
    bool finalPrimaryShadowValid = false;
    double finalPrimaryShadowTofIndex = std::numeric_limits<double>::quiet_NaN();
    double finalPrimaryShadowThicknessMm = std::numeric_limits<double>::quiet_NaN();
    QString finalPrimaryShadowConfidenceClass;
    QString finalPrimaryShadowReason;

    // FinalPrimary之后的FirstEcho Raw phase输出层微调诊断。
    // T2-only分支只读诊断，便于主/测试程序逐帧一致性回归。
    QString t2OnlyStage;
    QString t2OnlyReason;
    bool t2OnlyPeriodEvidenceAvailable = false;
    QString t2OnlyFormalT1CoreStatus;
    QString t2OnlyFormalT1CoreTrackingState;
    double t2OnlyFormalT1TofSamples = std::numeric_limits<double>::quiet_NaN();
    double t2OnlyPeriodSamples = std::numeric_limits<double>::quiet_NaN();
    double t2OnlyTofSamples = std::numeric_limits<double>::quiet_NaN();
    double t2OnlyTemplateCorrelation = std::numeric_limits<double>::quiet_NaN();
    int t2OnlyCandidateCount = 0;
    QString t2OnlyCandidateSummary;
    QString t2OnlyPairStage;
    double t2OnlyPairPredictedPeriod = std::numeric_limits<double>::quiet_NaN();
    int t2OnlyPairSearchHalfWidth = 0;
    double t2OnlyPairEnvelopeCorrelation = std::numeric_limits<double>::quiet_NaN();
    double t2OnlyPairRawAbsCorrelation = std::numeric_limits<double>::quiet_NaN();
    double t2OnlyPairResidualSamples = std::numeric_limits<double>::quiet_NaN();
    bool t2OnlyPairPublished = false;

    bool firstEchoPhaseRefinementApplied = false;
    double firstEchoPhaseRefinementCorrectionSamples = 0.0;
    double firstEchoPhaseRefinementBaseTofIndex = std::numeric_limits<double>::quiet_NaN();
    double firstEchoPhaseRefinementBaseThicknessMm = std::numeric_limits<double>::quiet_NaN();
    QString firstEchoPhaseRefinementReason;

    // shadow backfill与正式回填使用同一历史目标，测试工具据此修正过去帧的shadow序列。
    bool hasFinalPrimaryShadowBackfill = false;
    int finalPrimaryShadowBackfillFramesAgo = 0;
    double finalPrimaryShadowBackfillTofIndex = std::numeric_limits<double>::quiet_NaN();
    double finalPrimaryShadowBackfillThicknessMm = std::numeric_limits<double>::quiet_NaN();
    QString finalPrimaryShadowBackfillConfidenceClass;
    QString finalPrimaryShadowBackfillReason;

    // 与测试程序的延迟确认回填一致：当前帧完成重锁时，修正此前的PENDING帧。
    bool hasBackfill = false;
    int backfillFramesAgo = 0;
    double backfillThicknessMm = std::numeric_limits<double>::quiet_NaN();
    double backfillPeakTimeSec = std::numeric_limits<double>::quiet_NaN();
    double backfillPeakVoltage = std::numeric_limits<double>::quiet_NaN();
    double backfillSnrDb = std::numeric_limits<double>::quiet_NaN();
    int backfillPeakIndex = -1;
};

class ThicknessProcessor
{
public:
    ThicknessProcessor();
    ~ThicknessProcessor();

    ThicknessProcessor(const ThicknessProcessor&) = delete;
    ThicknessProcessor& operator=(const ThicknessProcessor&) = delete;

    // Raw用于测试程序同源的差分/MAD门控及带通处理。
    // filteredOut可选返回同源核心实际使用的滤波波形，供主程序显示与保存复用。
    ThicknessResult process(const std::vector<double>& raw,
                            std::vector<double>* filteredOut,
                            double sampleRate,
                            double lowFreqMHz,
                            double highFreqMHz,
                            int delayCount,
                            double thicknessStdMm,
                            double thicknessToleranceMm,
                            int structLen,
                            int minWidth,
                            ThicknessDetectionMode detectMode,
                            int filterOrder,
                            const ThicknessAlgorithmParameters& parameters);

    // 新采集批次开始时清空跨帧预测、模板、噪声底与自适应脉宽状态。
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
