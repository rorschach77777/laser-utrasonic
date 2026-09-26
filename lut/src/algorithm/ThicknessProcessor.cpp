#include "ThicknessProcessor.h"
#include "PacketSpline.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QTextStream>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <deque>
#include <limits>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double CURRENT_FIXED_BIAS_MM = 1.1;

// -----------------------------------------------------------------------------
// 参数
// -----------------------------------------------------------------------------
//
// 波形测厚链路：Raw脉冲拒绝 -> 双向FIR -> 背景/局部能量SNR -> 波包分割
// -> 首波包到时/形态判定 -> 独立波包歧义判定 -> 整包单峰能量包络拟合
// -> 拟合峰中心亚采样定位；理论窗截断时传统单峰回退 -> 厚度与历史门控。
// 后续回波相关仅作辅助证据：第二回波较弱时不会单独否决首波包。
//
// 调参速查：
//   impulseMadMultiplier 控制全局MAD门；impulseIsolationRatio用于高门限下补抓局部孤立尖峰。
//   minLocalSnrDb：提高可减少底噪误报，但弱纵波也可能失检。
//   packetMinWidth / packetMaxWidth：限制局部能量波包宽度，同一包内的局部尖峰统一拟合。
//   echoMinCorrelation / echoPeriodTolerance：只控制后续回波辅助确认，不是一票否决门限。
//   fitMinR2：等效单脉冲“累计能量分布”与固定脉宽高斯的一致性下限；
//             新评分不要求逐点贴合双峰原形，默认0.55需与旧0.70区分。
//   minCandidateRatio：仅比较独立波包，不能拿同一波包内部起伏做歧义拒绝。
//   predictionHalfWidth：历史粗门，限制整包拟合中心的跨帧漂移。
//   assertToleranceMm：仅用于PASS/FAIL评价，不参与信号识别或拟合。
//
struct TestParameters
{
    // 测试模式：auto自动识别CSV类型；waveform做逐帧波形测厚；spectrum做壁厚序列频谱分析。
    QString mode = QStringLiteral("auto");

    // ------------------------- 波形预处理参数 -------------------------
    // 采样率 [Hz]。决定时间轴、理论峰位置、亚采样时间和厚度换算。
    // 必须与采集卡真实采样率一致，不应把它当作防误检阈值随意调整。
    double sampleRateHz = 160000000.0;

    // FIR带通上下截止频率 [MHz]。
    // 它们不是直接“通过/拒绝阈值”，但会改变进入寻峰算法的信号和噪声能量：
    //   lowFreqMHz调高 -> 更强地抑制低频漂移，同时可能削弱真实低频超声分量；
    //   highFreqMHz调低 -> 更强地抑制高频噪声/尖锐成分，同时可能使真实波包变宽、变弱。
    double lowFreqMHz = 3.0;
    double highFreqMHz = 30.0;

    // FIR阶数。阶数越高，频率过渡通常越陡，但时域脉冲响应也更长。
    // 注意：Raw尖峰污染保护范围 guard 中包含 filterOrder，因此调大阶数会间接扩大脉冲影响区。
    int filterOrder = 30;

    // ------------------------- 基础寻峰参数 -------------------------
    // auto：搜索窗内正负幅值比较后自动选正峰/负峰；peak：只找正峰；valley：只找负峰。
    QString detectMode = QStringLiteral("auto");

    // 标称/参考厚度 [mm]。仅用于计算理论粗搜索位置和结果评价；
    // 当前代码计算理论位置时还叠加固定 CURRENT_FIXED_BIAS_MM = 1.1 mm。
    // 改它会“移动搜索窗”，不是改变信号强弱阈值。
    double thicknessStdMm = 5.6;

    // 厚度参考容差 [mm]：保留用于界面显示/评价兼容，不参与 measurementStatus 判废。
    // 只要寻峰、波包、SNR、跟踪等算法质量判据通过，就输出实际计算厚度。
    double thicknessToleranceMm = 0.8;

    // 兼容已有参数：只保留为Raw脉冲扩散保护的尺度，不再用于形态学开闭寻峰。
    // 实际波包宽度改由packetMinWidth/packetMaxWidth限制。
    int structLen = 9;

    // ★最小半高宽阈值 [sample]。候选峰宽度小于该值时直接淘汰。
    // 调大 -> 更严格地拒绝窄尖峰/噪声峰；调小 -> 弱窄峰更容易通过，但误检风险增加。
    int minWidth = 1;

    // 系统固定延时对应的采样点补偿。影响理论峰位置和最终厚度。
    // 设置错误会让搜索窗整体偏移，可能造成“在错误区域找到一个很强的峰”。
    // 它是标定参数，不是防误检阈值。
    int delayCount = 240;

    // ------------------------- 新增：脉冲污染判废 -------------------------
    // 设计目的：像Frame24这类尖峰若覆盖纵波搜索区，直接 INVALID，绝不插值修复后继续报厚度。
    // Raw差分脉冲阈值：
    //   T_impulse = max(impulseMinJumpV,
    //                   impulseMadMultiplier * 1.4826 * MAD(diff(raw)))
    // 其中 MAD 基于整帧Raw一阶差分，具有较强的抗异常值能力。

    // ★Raw差分MAD倍数阈值。
    // 调大 -> 脉冲检测更宽松（只有更大的跳变才算脉冲，漏检风险↑、误报↓）；
    // 调小 -> 脉冲检测更敏感（更容易判污染，漏检↓、误报↑）。
    double impulseMadMultiplier = 12.0;

    // ★Raw相邻点绝对跳变的最低阈值 [V]。
    // 防止底噪极低/MAD接近0时，把量化抖动当脉冲。
    // 调大 -> 小脉冲不再触发；调小 -> 更敏感。
    double impulseMinJumpV = 0.05;

    // ★局部孤立度阈值。当前差分相对邻域稳健尺度超过该倍数时，
    // 即使全局MAD倍数设得很高，也仍按孤立尖峰处理。
    // 调小 -> 更容易补抓孤立电尖峰；过小会把波包最陡边沿误判为脉冲。
    double impulseIsolationRatio = 20.0;

    // 脉冲保护区额外扩展 [sample]。
    // 实际污染范围还会自动加上 filterOrder 和 structLen/energyWindow 的影响范围。
    // 调大 -> 更保守，只要尖峰靠近搜索窗就更容易整帧判废；调小 -> 污染保护范围缩小。
    // 它主要改变“污染覆盖范围”，不是改变尖峰本身的幅值阈值。
    int impulseGuardSamples = 8;

    // ------------------------- 新增：无信号/底噪拒绝 -------------------------
    // 局部交流能量窗口长度 [sample]，强制为奇数。
    // 搜索窗信号和背景噪声都用同样长度计算局部能量。
    // 调大 -> 能量估计更平滑，更适合较宽波包；调小 -> 更强调局部尖锐变化。
    // 它本身不是通过阈值，但会改变计算出的SNR，因此会间接影响 NO_SIGNAL 判定。
    int energyWindowSamples = 17;

    // ★局部能量最小SNR阈值 [dB]，是解决“只有底噪也误检厚度”的核心门限。
    // 搜索窗最大局部能量SNR低于该值 -> NO_SIGNAL，不进入寻峰；
    // 最终选中的候选峰局部SNR低于该值 -> 同样 NO_SIGNAL。
    // 调大 -> 更严格，底噪误检更少，但弱信号漏检增加；
    // 调小 -> 更容易检出弱信号，但纯底噪误检风险增加。
    double minLocalSnrDb = 7.0;
    // 稳定LOCK状态下开放6~7 dB低SNR软恢复；普通搜索硬门仍保持7 dB。
    // 软恢复必须同时满足唯一候选、高拟合质量、RECENT高相关和小预测残差。
    double lowSnrSoftMinDb = 6.0;
    double lowSnrSoftMinTrackingCorrelation = 0.90;
    double lowSnrSoftMaxResidualSamples = 2.0;
    double lowSnrSoftMinFitR2 = 0.95;

    // 背景噪声鲁棒模型：先把背景局部能量转为dB，在dB域用MAD剔除异常高能量窗，
    // 再对单帧Noise Floor做跨帧快升慢降EWMA。这样既避免后续回波/局部干扰抬高
    // 单帧噪声中值，也避免偶然低噪声帧把SNR虚高。
    double noiseMadRejectMultiplier = 3.0;
    double noiseEwmaAlphaUp = 0.30;    // 噪声升高时快速跟随
    double noiseEwmaAlphaDown = 0.15;  // 噪声降低时慢速释放；回归表明0.05滞后过强

    // ------------------------- 新增：候选峰歧义拒绝 -------------------------
    // ★第一/第二独立候选显著性最小比值。
    // candidateRatio = firstSignificance / secondSignificance。
    // 仅比较通过纵波判据的独立波包，显著性为扣除背景后的积分能量。
    // 若存在第二候选且比值小于该阈值 -> AMBIGUOUS_PACKETS，拒绝输出厚度。
    // 调大 -> 要求第一波包明显强于第二波包；调小 -> 更容易接受第一波包。
    // 同一波包内的左右峰不互相竞争，否则加热中等幅阶段会被错误拒绝。
    double minCandidateRatio = 1.2;

    // 两个局部峰至少相隔多少采样点才当作两个独立候选。
    // 小于该距离的局部起伏被认为属于同一波包，避免一个回波内部的小波动被重复计数。
    // 调大 -> 更多近邻峰被合并，第二候选变少；调小 -> 更容易形成多个独立候选。
    // 它会间接改变 candidateRatio 判定结果，但不是比值阈值本身。
    int candidateSeparationSamples = 12;

    // ------------------------- 新增：跨帧预测门 -------------------------
    // 只使用之前 measurementStatus == VALID 的亚采样峰位置建立历史；
    // INVALID / NO_SIGNAL / 脉冲污染帧不会写入历史，也不会用预测值代替当前测量值。

    // 历史中最多保存多少个最近有效的整包拟合峰；用峰位差分的中位速度预测加热漂移。
    // 只有通过纵波判据、拟合或受控回退、亚采样质量及厚度门限的帧才更新历史。
    int predictionHistorySize = 5;

    // 启用预测门所需的最少有效历史帧数。
    // 调大 -> 锁定更谨慎、初始阶段更久使用宽理论窗；
    // 调小 -> 更快锁定，但少量偶然正确峰也可能过早建立预测门。
    int predictionMinValid = 3;

    // ★预测门半宽 [sample]：实际寻峰区域约为 predictedPeak ± predictionHalfWidth，
    // 同时不会越过理论搜索窗。
    // 调小 -> 更严格地阻止跳到远处错误峰；过小则真实峰轻微漂移时可能失检。
    // 调大 -> 容忍更大峰位变化，但抗远处误峰能力下降。
    int predictionHalfWidth = 16;

    // 连续多少次未得到有效测量后清空预测历史，重新回到宽理论窗捕获。
    // 调大 -> 短时间失信号后仍维持原锁定位置；
    // 调小 -> 更快放弃旧锁定并重新搜索。
    // 它影响状态保持时间，不直接改变单帧峰值阈值。
    int predictionMaxMisses = 3;

    // ------------------------- 新增：LOCK位置残差与重捕获 -------------------------
    // LOCK状态下，当前整波包相关ToF相对历史趋势预测的允许残差。
    // <= accept：直接接受；(accept, reject]：进入待确认；> reject：视为严重跳变，仍不直接输出。
    double trackingResidualAcceptSamples = 6.0;
    double trackingResidualRejectSamples = 9.0;
    // LOST或位置跳变后，连续多少帧在相近位置出现才重新LOCK并恢复厚度输出。
    int reacquireConfirmFrames = 2;
    // 重捕获连续候选之间允许的位置差 [sample]。
    double reacquireConsistencySamples = 5.0;

    // ------------------------- 纵波波包判据与拟合 -------------------------
    // 纵波主判据来自首波包：理论到时、局部SNR、有限宽度和整包单峰拟合质量。
    // 后续回波只用于增强可信度；其能量或相关不足不会单独把清晰首波包判废。
    // 这表示“符合当前纵波模型”，不是单通道波形对物理模态的绝对鉴定。
    double longitudinalVelocityMps = 6459.7;
    double echoMinCorrelation = 0.70; // 辅助回波的相关阈值，不是有效厚度硬门。
    double echoMinSnrDb = 3.0;        // 弱后续回波搜索门，低于首波包10dB默认门限。
    double echoMinRelativeEnergy = 0.03; // 防止短窗噪声偶然高相关被标成辅助回波。
    double echoPeriodTolerance = 0.20;
    int packetMinWidth = 8;
    int packetMaxWidth = 80;
    int packetMergeGap = 6;
    double packetEnergyFraction = 0.05; // 相对能量边界，抑制缓慢基线形成的超长假波包。
    // 第三步优化：仅在LOCK且历史预测位置本身有足够SNR时，允许略超packetMaxWidth的
    // 合并核心进入后续拟合/模板相关；必须再通过更严格的相关和位置一致性门。
    int lockedPacketMaxCoreWidth = 60;
    double lockedBroadMinTrackingCorrelation = 0.80;
    double lockedBroadEnergyPeakToleranceSamples = 7.0;

    // LOCK碎片化宽波包恢复：不降低全局packetMinWidth，先由低阈值支持区重建，
    // 再用RECENT模板高相关/小预测残差确认，最终厚度只使用模板相关ToF。
    int lockedFragmentedCoreMinWidth = 4;
    double lockedFragmentedMinMaxSnrDb = 7.0;
    double lockedFragmentedSupportSnrDb = 2.5;
    double lockedFragmentedSupportEnergyFraction = 0.025;
    double lockedFragmentedMinTrackingCorrelation = 0.88;
    double lockedFragmentedMaxPredictionResidualSamples = 2.5;
    double lockedFragmentedMinFitR2 = 0.95;

    // LOCK专用高精度恢复门：双瓣合并、碎片恢复和拟合失败模板恢复只在LOCK身份下启用。
    double lockRecoveryDirectCorrelation = 0.87;
    double lockRecoveryDirectResidualSamples = 1.0;
    double lockRecoveryPendingCorrelation = 0.85;
    double lockRecoveryPendingResidualSamples = 2.5;
    double lockRecoveryConfirmConsistencySamples = 2.0;
    double lockTemplateOnlyMinMaxSnrDb = 10.0;
    int lockStraddlingMaxSeparationSamples = 16;

    // 等效单高斯拟合参数。它们只影响“从主波包脚底构造单高斯脉冲”的方式。
    // 脚底相对峰高比例：调大 -> 拟合窗口变窄；调小 -> 窗口向两侧延伸。
    double fitFootFraction = 0.10;
    // 连续低于脚底阈值的确认点数：调大 -> 更不容易被局部谷值截断；过大可能延长窗口。
    int fitFootConfirmSamples = 3;
    // 脚底检测前的轻微平滑半径：0不平滑，1为3点均值；调大可抑制噪声但会拓宽边沿。
    int fitFootSmoothRadius = 1;
    // 高斯中心相对左右脚底中点允许微调的半宽 [sample]。
    double fitCenterHalfRange = 0.75;
    // 中心粗搜索步长；最终仍在最佳粗搜点附近以0.01 sample精搜。
    double fitCenterSearchCoarseStep = 0.08;
    // 等效高斯峰值相对实测主峰的上下限比例。
    double fitAmplitudeMinRatio = 0.75;
    double fitAmplitudeMaxRatio = 1.50;
    // 累计能量分布与固定脉宽高斯的一致性质量门。
    double fitMinR2 = 0.50;

    // ------------------------- 自适应拟合脉宽模型 -------------------------
    // 仅作为质量证据，不直接改变 VALID / INVALID。模型使用最近高质量拟合脉宽的
    // rolling median + robust MAD 形成动态基线，避免给热态/冷态共用一个过窄绝对硬门。
    int adaptiveWidthHistorySize = 21;
    int adaptiveWidthMinHistory = 7;
    // sigma_W = 1.4826 * median(|W - median(W)|)，正常容差取
    // max(adaptiveWidthMinToleranceSamples, adaptiveWidthMadMultiplier * sigma_W)。
    double adaptiveWidthMadMultiplier = 4.0;
    double adaptiveWidthMinToleranceSamples = 4.5;
    // 偏离超过1倍动态容差记为WIDTH_TRANSITION，超过该倍数记为WIDTH_OUTLIER。
    // TRANSITION仍可缓慢更新模型；OUTLIER不写入历史，避免异常宽包污染基线。
    double adaptiveWidthOutlierMultiplier = 2.5;

    double trackingMinCorrelation = 0.65;
    double trackingCorrelationMargin = 0.10;
    // LOCK多模板跟踪：RECENT保留最近模板，EMA缓慢跟随形态漂移，SHAPE仅学习高质量
    // WIDTH_TRANSITION波形。多模板都不降低0.65硬相关门；若多个硬通过模板给出的ToF
    // 分歧过大则拒绝，避免不同内部瓣之间发生模板投票式跳转。
    double trackingEmaTemplateAlpha = 0.15;
    double trackingShapeTemplateAlpha = 0.30;
    double trackingMultiTemplateAgreementSamples = 2.0;
    // EMA/SHAPE只是RECENT失配时的后备身份模板，接管必须比普通LOCK更贴近历史预测。
    // 该门只限制后备模板，不改变RECENT原有6点正常残差门。
    double trackingBackupTemplateMaxResidualSamples = 5.0;
    // 后备模板救援成功后，不直接替换RECENT，而是小步混入当前波形以适应形态变化。
    double trackingBackupRecentBlendAlpha = 0.25;
    // 第二步优化：仅在LOCK状态且拟合中心与历史预测高度一致时，允许较低相关帧软恢复。
    // 该通道不降低主相关门，而是用位置、拟合质量、候选唯一性和能量共同约束。
    double trackingSoftCorrelation = 0.53;
    double trackingSoftFitPredictionToleranceSamples = 3.0;
    double trackingSoftMinFitR2 = 0.80;
    double trackingSoftMinMaxSnrDb = 15.0;

    // ------------------------- 自动测试评价参数 -------------------------
    // 人工指定期望厚度 [mm]。<0表示未指定，此时可用CSV中原算法Thickness作为回归参考。
    // 只用于结果评价，不参与真实寻峰。
    double expectedThicknessMm = -1.0;

    // ★PASS/FAIL测试容差 [mm]。
    // abs(当前测量厚度 - referenceThickness) <= assertToleranceMm -> PASS，否则FAIL。
    // 只改变测试通过率，不会改变 measurementStatus 或寻峰结果。
    double assertToleranceMm = 0.05;

    // ------------------------- 壁厚序列频谱分析参数 -------------------------
    // 长周期/短周期的分界波长 [m]；改变频谱能量归类边界，不参与逐帧寻峰。
    double cutoffWavelengthM = 1.0;

    // ★频谱诊断的最小壁厚波动标准差阈值 [mm]。
    // 调大 -> 只有更明显的壁厚起伏才被认为有工艺波动；调小 -> 诊断更敏感。
    double minVariationMm = 0.05;

    // ★长/短周期能量“占优”阈值，解析时限制在0.5~0.95。
    // 调大 -> 必须某一类周期能量占比非常明显才判为占优；调小 -> 更容易形成占优判断。
    double dominanceThreshold = 0.65;
};

TestParameters parseParameters(const QVariantMap &map)
{
    TestParameters p;

    if (map.contains("mode"))
        p.mode = map.value("mode").toString();

    if (map.contains("sampleRateHz"))
        p.sampleRateHz = map.value("sampleRateHz").toDouble();

    if (map.contains("lowFreqMHz"))
        p.lowFreqMHz = map.value("lowFreqMHz").toDouble();

    if (map.contains("highFreqMHz"))
        p.highFreqMHz = map.value("highFreqMHz").toDouble();

    if (map.contains("filterOrder"))
        p.filterOrder = map.value("filterOrder").toInt();

    if (map.contains("detectMode"))
        p.detectMode = map.value("detectMode").toString();

    if (map.contains("thicknessStdMm"))
        p.thicknessStdMm = map.value("thicknessStdMm").toDouble();

    if (map.contains("thicknessToleranceMm"))
        p.thicknessToleranceMm = map.value("thicknessToleranceMm").toDouble();

    if (map.contains("structLen"))
        p.structLen = map.value("structLen").toInt();

    if (map.contains("minWidth"))
        p.minWidth = map.value("minWidth").toInt();

    if (map.contains("delayCount"))
        p.delayCount = map.value("delayCount").toInt();

    if (map.contains("expectedThicknessMm"))
        p.expectedThicknessMm = map.value("expectedThicknessMm").toDouble();

    if (map.contains("assertToleranceMm"))
        p.assertToleranceMm = map.value("assertToleranceMm").toDouble();

    if (map.contains("cutoffWavelengthM"))
        p.cutoffWavelengthM = map.value("cutoffWavelengthM").toDouble();

    if (map.contains("minVariationMm"))
        p.minVariationMm = map.value("minVariationMm").toDouble();

    if (map.contains("dominanceThreshold"))
        p.dominanceThreshold = map.value("dominanceThreshold").toDouble();

    // 非法门限恢复为默认值，避免 NaN 或空输入使防误检判定失效。
    auto positiveDouble = [&map](const char *key, double fallback) {
        bool ok = false;
        const double value = map.value(key, fallback).toDouble(&ok);
        return ok && std::isfinite(value) && value > 0.0 ? value : fallback;
    };
    auto boundedInt = [&map](const char *key, int fallback, int low, int high) {
        bool ok = false;
        const int value = map.value(key, fallback).toInt(&ok);
        return ok ? std::clamp(value, low, high) : fallback;
    };
    p.impulseMadMultiplier = positiveDouble("impulseMadMultiplier", p.impulseMadMultiplier);
    p.impulseMinJumpV = positiveDouble("impulseMinJumpV", p.impulseMinJumpV);
    p.impulseIsolationRatio = std::clamp(
        positiveDouble("impulseIsolationRatio", p.impulseIsolationRatio), 5.0, 100.0);
    p.impulseGuardSamples = boundedInt("impulseGuardSamples", 8, 0, 1000);
    p.energyWindowSamples = boundedInt("energyWindowSamples", 17, 3, 255);
    p.energyWindowSamples |= 1; // 奇数窗口，能量窗口中心与采样点对齐。
    p.minLocalSnrDb = positiveDouble("minLocalSnrDb", p.minLocalSnrDb);
    p.lowSnrSoftMinDb = std::clamp(
        positiveDouble("lowSnrSoftMinDb", 6.0), 0.1, p.minLocalSnrDb);
    p.noiseMadRejectMultiplier = std::clamp(
        positiveDouble("noiseMadRejectMultiplier", 3.0), 1.0, 10.0);
    p.noiseEwmaAlphaUp = std::clamp(
        positiveDouble("noiseEwmaAlphaUp", 0.30), 0.01, 1.0);
    p.noiseEwmaAlphaDown = std::clamp(
        positiveDouble("noiseEwmaAlphaDown", 0.15), 0.001, p.noiseEwmaAlphaUp);
    p.minCandidateRatio = std::max(1.01, positiveDouble("minCandidateRatio", p.minCandidateRatio));
    p.candidateSeparationSamples = boundedInt("candidateSeparationSamples", 12, 1, 1000);
    p.predictionHistorySize = boundedInt("predictionHistorySize", 5, 1, 32);
    p.predictionMinValid = boundedInt("predictionMinValid", 3, 1, p.predictionHistorySize);
    p.predictionHalfWidth = boundedInt("predictionHalfWidth", 16, 1, 1000);
    p.predictionMaxMisses = boundedInt("predictionMaxMisses", 3, 1, 100);
    p.trackingResidualAcceptSamples = std::clamp(
        positiveDouble("trackingResidualAcceptSamples", 6.0), 0.5, 100.0);
    p.trackingResidualRejectSamples = std::clamp(
        positiveDouble("trackingResidualRejectSamples", 9.0),
        p.trackingResidualAcceptSamples, 200.0);
    p.reacquireConfirmFrames = boundedInt("reacquireConfirmFrames", 2, 2, 10);
    p.reacquireConsistencySamples = std::clamp(
        positiveDouble("reacquireConsistencySamples", 5.0), 0.5, 100.0);

    p.longitudinalVelocityMps = positiveDouble("longitudinalVelocityMps", p.longitudinalVelocityMps);
    p.echoMinCorrelation = std::clamp(positiveDouble("echoMinCorrelation", 0.70), 0.1, 1.0);
    p.echoMinSnrDb = std::clamp(positiveDouble("echoMinSnrDb", 3.0), 0.1, 30.0);
    p.echoMinRelativeEnergy = std::clamp(positiveDouble("echoMinRelativeEnergy", 0.03), 0.001, 0.5);
    p.echoPeriodTolerance = std::clamp(positiveDouble("echoPeriodTolerance", 0.20), 0.01, 0.4);
    p.packetMinWidth = boundedInt("packetMinWidth", 8, 5, 100);
    p.packetMaxWidth = boundedInt("packetMaxWidth", 80, p.packetMinWidth + 1, 250);
    p.packetMergeGap = boundedInt("packetMergeGap", 6, 0, 20);
    p.packetEnergyFraction = std::clamp(positiveDouble("packetEnergyFraction", 0.05), 0.001, 0.5);
    p.lockedPacketMaxCoreWidth = boundedInt(
        "lockedPacketMaxCoreWidth", std::max(60, p.packetMaxWidth), p.packetMaxWidth, 250);
    p.lockedBroadEnergyPeakToleranceSamples = std::clamp(
        positiveDouble("lockedBroadEnergyPeakToleranceSamples", 7.0), 0.5, 50.0);
    p.lockedFragmentedCoreMinWidth = boundedInt(
        "lockedFragmentedCoreMinWidth", 4, 2, std::max(2, p.packetMinWidth - 1));
    p.lockedFragmentedMinMaxSnrDb = std::clamp(
        positiveDouble("lockedFragmentedMinMaxSnrDb", 7.0), p.minLocalSnrDb, 60.0);
    p.lockedFragmentedSupportSnrDb = std::clamp(
        positiveDouble("lockedFragmentedSupportSnrDb", 2.5), 0.1, p.minLocalSnrDb);
    p.lockedFragmentedSupportEnergyFraction = std::clamp(
        positiveDouble("lockedFragmentedSupportEnergyFraction", 0.025), 0.001, p.packetEnergyFraction);
    p.lockedFragmentedMinTrackingCorrelation = std::clamp(
        positiveDouble("lockedFragmentedMinTrackingCorrelation", 0.88), p.trackingMinCorrelation, 1.0);
    p.lockedFragmentedMaxPredictionResidualSamples = std::clamp(
        positiveDouble("lockedFragmentedMaxPredictionResidualSamples", 2.5), 0.25,
        p.trackingResidualAcceptSamples);
    p.lockedFragmentedMinFitR2 = std::clamp(
        positiveDouble("lockedFragmentedMinFitR2", 0.95), p.fitMinR2, 0.9999);
    p.lockRecoveryDirectCorrelation = std::clamp(
        positiveDouble("lockRecoveryDirectCorrelation", 0.87), p.trackingMinCorrelation, 1.0);
    p.lockRecoveryDirectResidualSamples = std::clamp(
        positiveDouble("lockRecoveryDirectResidualSamples", 1.0), 0.25, p.trackingResidualAcceptSamples);
    p.lockRecoveryPendingCorrelation = std::clamp(
        positiveDouble("lockRecoveryPendingCorrelation", 0.85), p.trackingMinCorrelation,
        p.lockRecoveryDirectCorrelation);
    p.lockRecoveryPendingResidualSamples = std::clamp(
        positiveDouble("lockRecoveryPendingResidualSamples", 2.5),
        p.lockRecoveryDirectResidualSamples, p.trackingResidualAcceptSamples);
    p.lockRecoveryConfirmConsistencySamples = std::clamp(
        positiveDouble("lockRecoveryConfirmConsistencySamples", 2.0), 0.25,
        p.reacquireConsistencySamples);
    p.lockTemplateOnlyMinMaxSnrDb = std::clamp(
        positiveDouble("lockTemplateOnlyMinMaxSnrDb", 10.0), p.minLocalSnrDb, 60.0);
    p.lockStraddlingMaxSeparationSamples = boundedInt(
        "lockStraddlingMaxSeparationSamples", 16, p.candidateSeparationSamples, 64);
    p.fitFootFraction = std::clamp(positiveDouble("fitFootFraction", p.fitFootFraction), 0.01, 0.80);
    p.fitFootConfirmSamples = boundedInt("fitFootConfirmSamples", p.fitFootConfirmSamples, 1, 20);
    p.fitFootSmoothRadius = boundedInt("fitFootSmoothRadius", p.fitFootSmoothRadius, 0, 8);
    p.fitCenterHalfRange = std::clamp(positiveDouble("fitCenterHalfRange", p.fitCenterHalfRange), 0.01, 10.0);
    p.fitCenterSearchCoarseStep = std::clamp(
        positiveDouble("fitCenterSearchCoarseStep", p.fitCenterSearchCoarseStep), 0.01, 1.0);
    p.fitAmplitudeMinRatio = std::clamp(positiveDouble("fitAmplitudeMinRatio", p.fitAmplitudeMinRatio), 0.05, 3.0);
    p.fitAmplitudeMaxRatio = std::clamp(positiveDouble("fitAmplitudeMaxRatio", p.fitAmplitudeMaxRatio),
                                        p.fitAmplitudeMinRatio, 5.0);
    p.fitMinR2 = std::clamp(positiveDouble("fitMinR2", p.fitMinR2), 0.1, 0.9999);
    p.adaptiveWidthHistorySize = boundedInt("adaptiveWidthHistorySize", 21, 5, 101);
    p.adaptiveWidthMinHistory = boundedInt(
        "adaptiveWidthMinHistory", 7, 3, p.adaptiveWidthHistorySize);
    p.adaptiveWidthMadMultiplier = std::clamp(
        positiveDouble("adaptiveWidthMadMultiplier", 4.0), 1.0, 12.0);
    p.adaptiveWidthMinToleranceSamples = std::clamp(
        positiveDouble("adaptiveWidthMinToleranceSamples", 4.5), 0.5, 20.0);
    p.adaptiveWidthOutlierMultiplier = std::clamp(
        positiveDouble("adaptiveWidthOutlierMultiplier", 2.5), 1.1, 5.0);
    p.trackingMinCorrelation = std::clamp(positiveDouble("trackingMinCorrelation", 0.65), 0.1, 1.0);
    p.trackingCorrelationMargin = std::clamp(positiveDouble("trackingCorrelationMargin", 0.10), 0.001, 1.0);
    p.trackingEmaTemplateAlpha = std::clamp(
        positiveDouble("trackingEmaTemplateAlpha", 0.15), 0.01, 1.0);
    p.trackingShapeTemplateAlpha = std::clamp(
        positiveDouble("trackingShapeTemplateAlpha", 0.30), 0.01, 1.0);
    p.trackingMultiTemplateAgreementSamples = std::clamp(
        positiveDouble("trackingMultiTemplateAgreementSamples", 2.0), 0.25, 10.0);
    p.trackingBackupTemplateMaxResidualSamples = std::clamp(
        positiveDouble("trackingBackupTemplateMaxResidualSamples", 5.0), 0.25,
        p.trackingResidualAcceptSamples);
    p.trackingBackupRecentBlendAlpha = std::clamp(
        positiveDouble("trackingBackupRecentBlendAlpha", 0.25), 0.01, 1.0);
    p.lowSnrSoftMinTrackingCorrelation = std::clamp(
        positiveDouble("lowSnrSoftMinTrackingCorrelation", 0.84),
        p.trackingMinCorrelation, 1.0);
    p.lowSnrSoftMaxResidualSamples = std::clamp(
        positiveDouble("lowSnrSoftMaxResidualSamples", 2.0), 0.1, p.trackingResidualAcceptSamples);
    p.lowSnrSoftMinFitR2 = std::clamp(
        positiveDouble("lowSnrSoftMinFitR2", 0.95), 0.1, 0.9999);
    p.lockedBroadMinTrackingCorrelation = std::clamp(
        positiveDouble("lockedBroadMinTrackingCorrelation", 0.80),
        p.trackingMinCorrelation, 1.0);
    p.trackingSoftCorrelation = std::clamp(
        positiveDouble("trackingSoftCorrelation", 0.53), 0.1, p.trackingMinCorrelation);
    p.trackingSoftFitPredictionToleranceSamples = std::clamp(
        positiveDouble("trackingSoftFitPredictionToleranceSamples", 3.0), 0.1, 20.0);
    p.trackingSoftMinFitR2 = std::clamp(
        positiveDouble("trackingSoftMinFitR2", 0.80), 0.1, 0.9999);
    p.trackingSoftMinMaxSnrDb = std::clamp(
        positiveDouble("trackingSoftMinMaxSnrDb", 15.0), 0.1, 60.0);

    if (p.sampleRateHz <= 0.0) p.sampleRateHz = 160000000.0;
    if (p.filterOrder < 1) p.filterOrder = 30;
    if (p.structLen < 1) p.structLen = 9;
    if (p.minWidth < 1) p.minWidth = 1;
    if (p.delayCount < 0) p.delayCount = 0;
    if (p.assertToleranceMm < 0.0) p.assertToleranceMm = 0.05;
    if (p.cutoffWavelengthM <= 0.0) p.cutoffWavelengthM = 1.0;
    p.dominanceThreshold = std::clamp(p.dominanceThreshold, 0.5, 0.95);
    p.minVariationMm = std::max(0.0, p.minVariationMm);

    return p;
}

// -----------------------------------------------------------------------------
// 100 Hz实时算法显式工作区
// -----------------------------------------------------------------------------
//
// 由一次批处理/一个处理线程持有并跨帧复用。避免 non-trivial thread_local 容器
// 在 QtConcurrent 工作线程退出阶段参与TLS析构，同时保留FIR/Raw MAD/Noise MAD/拟合
// 的内存复用收益。这里仅保存临时工作数组与滤波缓存，不保存任何测量历史状态。
struct AlgorithmWorkspace
{
    // FIR工作区
    std::vector<double> filterPadded;
    std::vector<double> filterForward;
    std::vector<double> filterBackward;

    // Raw差分/MAD工作区
    std::vector<double> rawDifferences;
    std::vector<double> rawDeviations;
    std::vector<double> rawMedianScratch;
    std::vector<double> rawLocalDeviations;
    std::vector<bool> rawContaminated;

    // Noise Floor工作区
    std::vector<double> noiseBackgroundDb;
    std::vector<double> noiseMedianScratch;
    std::vector<double> noiseDeviations;
    std::vector<double> noiseAcceptedDb;

    // FIR系数和输出缓存
    double filterLowFreqMHz = std::numeric_limits<double>::quiet_NaN();
    double filterHighFreqMHz = std::numeric_limits<double>::quiet_NaN();
    double filterSampleRateHz = std::numeric_limits<double>::quiet_NaN();
    int filterOrder = -1;
    std::vector<double> filterCoeffs;
    std::vector<double> filtered;

    // 等效单脉冲拟合工作区
    PacketPeakFit::FitWorkspace fit;
};

// -----------------------------------------------------------------------------
// CSV读取
// -----------------------------------------------------------------------------

struct CsvData
{
    QStringList headers;

    std::vector<double> original;
    std::vector<double> filteredColumn;

    QVector<double> thickness;
    QVector<double> position;
    QVector<double> snr;
    QVector<int> valid;

    double recordedThicknessMm =
        std::numeric_limits<double>::quiet_NaN();

    QString detectedType;
    QString error;
};

QString normalizeHeader(QString text)
{
    text = text.trimmed().toLower();
    text.remove(' ');
    text.remove('\t');
    return text;
}

bool parseNumber(const QString &text, double &value)
{
    const QString s = text.trimmed();

    if (s.compare("nan", Qt::CaseInsensitive) == 0) {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }

    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok)
        return false;

    value = v;
    return true;
}

QChar detectDelimiter(const QString &line)
{
    if (line.count(',') >= line.count(';') &&
        line.count(',') >= line.count('\t'))
        return ',';

    if (line.count(';') >= line.count('\t'))
        return ';';

    return '\t';
}

int findHeader(const QStringList &headers,
               std::initializer_list<const char *> names)
{
    for (int i = 0; i < headers.size(); ++i) {
        const QString h = normalizeHeader(headers[i]);
        for (const char *name : names) {
            if (h == QString::fromLatin1(name))
                return i;
        }
    }
    return -1;
}

CsvData loadCsv(const QString &filePath)
{
    CsvData data;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        data.error = QStringLiteral("无法打开文件");
        return data;
    }

    QTextStream in(&file);

    QString firstNonEmpty;
    while (!in.atEnd() && firstNonEmpty.isEmpty())
        firstNonEmpty = in.readLine().trimmed();

    if (firstNonEmpty.isEmpty()) {
        data.error = QStringLiteral("文件为空");
        return data;
    }

    const QChar delimiter = detectDelimiter(firstNonEmpty);
    const QStringList firstParts = firstNonEmpty.split(delimiter);

    bool firstLooksLikeHeader = false;
    for (const QString &part : firstParts) {
        double dummy = 0.0;
        if (!parseNumber(part, dummy)) {
            firstLooksLikeHeader = true;
            break;
        }
    }

    QStringList lines;
    if (firstLooksLikeHeader)
        data.headers = firstParts;
    else
        lines.append(firstNonEmpty);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty())
            lines.append(line);
    }

    const int originalCol =
        findHeader(data.headers, {"original", "raw", "waveform", "signal"});

    const int filteredCol =
        findHeader(data.headers, {"filtered", "filteredsignal"});

    const int thicknessCol =
        findHeader(data.headers, {"thickness(mm)", "thickness", "thicknessmm"});

    const int positionCol =
        findHeader(data.headers, {"position(m)", "position", "positionm"});

    const int snrCol =
        findHeader(data.headers, {"snr(db)", "snr", "snrdb"});

    const int validCol =
        findHeader(data.headers, {"valid", "isvalid"});

    if (originalCol >= 0)
        data.detectedType = QStringLiteral("waveform");
    else if (thicknessCol >= 0)
        data.detectedType = QStringLiteral("spectrum");
    else
        data.detectedType = QStringLiteral("waveform");

    bool recordedThicknessCaptured = false;

    for (const QString &line : lines) {
        const QStringList parts = line.split(delimiter);

        auto valueAt = [&parts](int col, double &value) -> bool {
            if (col < 0 || col >= parts.size())
                return false;
            return parseNumber(parts[col], value);
        };

        // 原始波形：
        // 1) 有Original列时优先使用Original
        // 2) 无表头时优先使用第2列（Index,Value）
        // 3) 单列文件使用第1列
        double waveformValue = 0.0;
        bool waveformOk = false;

        if (originalCol >= 0) {
            waveformOk = valueAt(originalCol, waveformValue);
        } else if (data.headers.isEmpty()) {
            const int fallbackCol = parts.size() >= 2 ? 1 : 0;
            waveformOk = valueAt(fallbackCol, waveformValue);
        }

        // 不可跳过坏采样点再压缩时间轴，否则后续峰位置和厚度都会发生偏移。
        if ((originalCol >= 0 || data.headers.isEmpty())
            && (!waveformOk || !std::isfinite(waveformValue))) {
            data.error = QStringLiteral("原始波形包含缺失/非有限采样点，禁止压缩时间轴后测厚");
            return data;
        }
        if (waveformOk)
            data.original.push_back(waveformValue);

        double filteredValue = 0.0;
        if (filteredCol >= 0 &&
            valueAt(filteredCol, filteredValue) &&
            std::isfinite(filteredValue)) {
            data.filteredColumn.push_back(filteredValue);
        }

        double thicknessValue =
            std::numeric_limits<double>::quiet_NaN();

        if (thicknessCol >= 0 &&
            valueAt(thicknessCol, thicknessValue)) {

            data.thickness.append(thicknessValue);

            if (!recordedThicknessCaptured &&
                std::isfinite(thicknessValue)) {
                data.recordedThicknessMm = thicknessValue;
                recordedThicknessCaptured = true;
            }
        }

        double positionValue =
            std::numeric_limits<double>::quiet_NaN();

        if (positionCol >= 0)
            valueAt(positionCol, positionValue);

        if (thicknessCol >= 0)
            data.position.append(positionValue);

        double snrValue =
            std::numeric_limits<double>::quiet_NaN();

        if (snrCol >= 0)
            valueAt(snrCol, snrValue);

        if (thicknessCol >= 0)
            data.snr.append(snrValue);

        int validValue = std::isfinite(thicknessValue) ? 1 : 0;

        if (validCol >= 0 && validCol < parts.size()) {
            const QString v = parts[validCol].trimmed();
            validValue =
                (v == "1" ||
                 v.compare("true", Qt::CaseInsensitive) == 0)
                ? 1 : 0;
        }

        if (thicknessCol >= 0)
            data.valid.append(validValue);
    }

    // 当前测试程序要求波形回放必须使用原始波形。
    // 只有 Filtered 列时不自动当作 Original，避免重复执行带通滤波，
    // 从而保证“离线结果 = 当前上位机完整算法链”这一测试前提。
    return data;
}

// -----------------------------------------------------------------------------
// 当前DataController算法的离线复制
// -----------------------------------------------------------------------------

void designBandpassFilter(double lowFreq,
                          double highFreq,
                          double sampleRate,
                          int order,
                          std::vector<double> &coeffs)
{
    const int taps = order + 1;
    const double fc1 = lowFreq / sampleRate;
    const double fc2 = highFreq / sampleRate;
    const int M = order / 2;

    coeffs.resize(taps);

    for (int n = 0; n < taps; ++n) {
        const double window =
            0.54 - 0.46 * std::cos(2.0 * PI * n / order);

        const int k = n - M;

        if (k == 0) {
            coeffs[n] =
                2.0 * (fc2 - fc1) * window;
        } else {
            const double sinc =
                (std::sin(2.0 * PI * fc2 * k) -
                 std::sin(2.0 * PI * fc1 * k)) /
                (PI * k);

            coeffs[n] = sinc * window;
        }
    }

    // 理论上线性相位窗函数FIR严格关于中心对称。libm在 cos(2π-x) 的末位
    // 可能产生约1e-17的不对称，这里一次性取成对均值并镜像，后续主卷积即可
    // 用一个乘法处理一对采样点。
    for (int n = 0; n < taps / 2; ++n) {
        const double symmetric = 0.5 * (coeffs[n] + coeffs[order - n]);
        coeffs[n] = symmetric;
        coeffs[order - n] = symmetric;
    }
}

void applyBandpassFilter(const std::vector<double> &input,
                         std::vector<double> &output,
                         const std::vector<double> &coeffs,
                         AlgorithmWorkspace &workspace)
{
    const int len = static_cast<int>(input.size());

    output.resize(len);

    if (len <= 0 || coeffs.empty()) {
        output = input;
        return;
    }

    const int order =
        static_cast<int>(coeffs.size()) - 1;

    if (order < 1) {
        output = input;
        return;
    }

    const int padLen =
        std::min(order * 2, len);

    const int nPadded =
        2 * padLen + len;

    // 100 Hz实时路径：复用显式FIR工作区，避免每帧为三块约1600点缓冲反复malloc/free。
    // resize只改变逻辑长度；达到最大帧长后后续帧不会再次分配，滤波乘加顺序完全不变。
    auto &padded = workspace.filterPadded;
    auto &forward = workspace.filterForward;
    auto &backward = workspace.filterBackward;
    padded.resize(nPadded);
    forward.resize(nPadded);
    backward.resize(nPadded);

    for (int i = 0; i < padLen; ++i) {
        int srcIdx = i;
        if (srcIdx >= len)
            srcIdx = len - 1;

        padded[padLen - 1 - i] =
            input[srcIdx];
    }

    for (int i = 0; i < len; ++i)
        padded[padLen + i] = input[i];

    for (int i = 0; i < padLen; ++i) {
        int srcIdx = len - 1 - i;
        if (srcIdx < 0)
            srcIdx = 0;

        padded[padLen + len + i] =
            input[srcIdx];
    }

    const int warmupEnd =
        std::min(order, nPadded);

    for (int i = 0; i < warmupEnd; ++i) {
        double sum = 0.0;

        for (int j = 0; j <= i; ++j)
            sum += coeffs[j] *
                   padded[i - j];

        forward[i] = sum;
    }

    for (int i = warmupEnd;
         i < nPadded;
         ++i) {

        double sum = 0.0;
        const double *p = padded.data() + i;
        int j = 0;
        int k = order;
        for (; j < k; ++j, --k)
            sum += coeffs[j] * (p[-j] + p[-k]);
        if (j == k)
            sum += coeffs[j] * p[-j];

        forward[i] = sum;
    }

    const int mainEnd =
        nPadded - order - 1;

    for (int i = mainEnd; i >= 0; --i) {
        double sum = 0.0;
        const double *p = forward.data() + i;
        int j = 0;
        int k = order;
        for (; j < k; ++j, --k)
            sum += coeffs[j] * (p[j] + p[k]);
        if (j == k)
            sum += coeffs[j] * p[j];

        backward[i] = sum;
    }

    const int tailEnd =
        std::max(0, nPadded - order);

    for (int i = nPadded - 1;
         i >= tailEnd;
         --i) {

        double sum = 0.0;

        int jMax =
            nPadded - 1 - i;

        if (jMax > order)
            jMax = order;

        const double *p =
            forward.data() + i;

        for (int j = 0; j <= jMax; ++j)
            sum += coeffs[j] * p[j];

        backward[i] = sum;
    }

    for (int i = 0; i < len; ++i)
        output[i] =
            backward[padLen + i];
}

void detrendSignal(std::vector<double> &signal)
{
    const int n =
        static_cast<int>(signal.size());

    if (n < 2)
        return;

    const double meanX =
        (n - 1) / 2.0;

    double meanY = 0.0;

    for (double v : signal)
        meanY += v;

    meanY /= n;

    double numerator = 0.0;
    double denominator = 0.0;

    for (int i = 0; i < n; ++i) {
        const double dx = i - meanX;
        const double dy =
            signal[i] - meanY;

        numerator += dx * dy;
        denominator += dx * dx;
    }

    const double slope =
        denominator != 0.0
        ? numerator / denominator
        : 0.0;

    const double intercept =
        meanY - slope * meanX;

    for (int i = 0; i < n; ++i)
        signal[i] -=
            slope * i + intercept;
}

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

struct WaveformResult
{
    double thicknessMm = NaN;
    double peakTimeSec = NaN;
    double peakVoltage = NaN;
    double snrDb = NaN;
    double subsampleOffset = 0.0;
    double interpolatedPeakIndex = -1.0;
    // 脉冲拟合后的亚采样峰位置，用于PRE_LOST/恢复搜索阶段的身份判断。
    // 拟合失败时保持NaN，由调用端回退到peakIndex。
    double fitPeakIndex = NaN;
    int peakIndex = -1;
    int searchLeftIndex = -1;
    int searchRightIndex = -1;

    // 状态与拒绝原因分开保存；INVALID/NO_SIGNAL 均不得输出厚度。
    QString measurementStatus = QStringLiteral("INVALID");
    QString rejectionReason;
    QString error;

    // 统一质量输出：qualityLevel用于程序/导出，qualityLabel用于界面中文显示。
    // confidenceScore 是0~100的规则质量分，用于比较证据完整度，不代表统计概率。
    QString qualityLevel = QStringLiteral("UNASSESSED");
    QString qualityLabel = QStringLiteral("未评估");
    double confidenceScore = NaN;
    QString confidenceBasis;
    QString rejectionStage;
    QString rejectionThresholdDetail;
    double rawDiffSigma = NaN;
    double impulseThresholdV = NaN;
    double maxImpulseScore = 0.0;
    double maxImpulseIsolationScore = 0.0;
    int coherentRawJumpCount = 0;
    int impulseCount = 0;
    int firstContaminatedIndex = -1;
    double noiseEnergy = NaN;
    int backgroundNoiseWindowCount = 0;
    int backgroundNoiseRejectedCount = 0;
    double backgroundNoiseMedianDb = NaN;
    double backgroundNoiseMadDb = NaN;
    double frameNoiseFloorDb = NaN;
    double temporalNoiseFloorDb = NaN;
    double noiseEwmaAlphaUsed = NaN;
    double localSignalEnergy = NaN;
    double maxLocalSnrDb = NaN;
    // 低SNR软恢复诊断：预门只允许稳定LOCK的弱信号继续检测；最终是否恢复由
    // 模板相关、位置残差、候选唯一性和拟合质量共同决定。
    bool lowSnrSoftCandidate = false;
    bool lowSnrRecovered = false;
    double lowSnrTrackingResidualSamples = NaN;
    // 预测门内的最大局部SNR单独记录，避免整个理论窗外的强旁瓣/其它回波
    // 抬高相对分割阈值时误导诊断。
    double predictionGateMaxLocalSnrDb = NaN;
    int predictionGateMaxIndex = -1;
    double predictedCenterLocalSnrDb = NaN;
    double segmentationReferenceSnrDb = NaN;
    int segmentationReferenceIndex = -1;
    // 预测门内最大能量点所属的原始分割核心（尚未向两侧扩展half点）。
    // 用于区分“门内确有弱波包但核心过窄”和“门内根本没有形成能量片段”。
    int predictionGateCoreLeftIndex = -1;
    int predictionGateCoreRightIndex = -1;
    int predictionGateCoreWidthSamples = 0;
    double firstSignificance = NaN;
    double secondSignificance = NaN;
    double candidateRatio = NaN;
    int firstCandidateIndex = -1;
    int secondCandidateIndex = -1;
    int candidateCount = 0;
    bool predictionApplied = false;
    double predictedPeakIndex = NaN;
    int predictionHistoryCount = 0;
    int predictionLeftIndex = -1;
    int predictionRightIndex = -1;
    QString searchWindowStage = QStringLiteral("GLOBAL_SEARCH");
    QString longitudinalStatus = QStringLiteral("NOT_EVALUATED");
    bool longitudinalCandidate = false;
    QString longitudinalEvidence;
    bool echoConfirmed = false;
    double echoCorrelation = NaN;
    double echoSnrDb = NaN;
    double echoEnergyRatio = NaN;
    double echoDelaySamples = NaN;
    double expectedEchoDelaySamples = NaN;
    int packetLeftIndex = -1;
    int packetRightIndex = -1;
    bool fitSucceeded = false;
    double fitR2 = NaN;
    double fitResidualRms = NaN;
    double fitSinglePeakIndex = NaN;
    double packetCenterIndex = NaN;
    double fitPeakWidthSamples = NaN;
    // 自适应拟合脉宽模型诊断。WIDTH_NORMAL / WIDTH_TRANSITION / WIDTH_OUTLIER
    // 只影响quality/confidence，不直接判废当前厚度。
    QString adaptiveWidthState = QStringLiteral("WIDTH_UNAVAILABLE");
    int adaptiveWidthHistoryCount = 0;
    double adaptiveWidthMedianSamples = NaN;
    double adaptiveWidthMadSamples = NaN;
    double adaptiveWidthToleranceSamples = NaN;
    double adaptiveWidthDeviationSamples = NaN;
    double adaptiveWidthNormalizedDeviation = NaN;
    // 理论窗截断时使用传统单峰回退，单独导出触发原因便于追溯。
    bool fallbackPeakUsed = false;
    QString fallbackReason;
    double trackingCorrelation = NaN;
    double trackingCorrelationMargin = NaN;
    // LOCK_GRACE身份/形态解耦评分：身份连续性负责判断是否仍为同一回波，
    // 形态评分只评价波包变化，不再直接决定身份丢失。
    double identityScore = NaN;
    double shapeScore = NaN;
    double lockGraceScore = NaN;
    bool lockGraceRecovered = false;
    // LOCK_GRACE候选旁路：仅表示标准candidate链失败后，在稳定LOCK身份保护窗口内
    // 重建出的候选，不改变正常candidate判定，也不绕过SNR主阈值。
    bool lockGraceCandidate = false;
    QString lockGraceEvidence;
    double trackedPeakPrediction = NaN;
    double trackingResidualSamples = NaN;
    QString trackingState = QStringLiteral("SEARCH");
    int reacquirePendingCount = 0;
    // 新增恢复诊断：启动弱锁恢复与LOCK局部恢复不改变原始检测门，
    // 仅记录恢复候选和两帧确认状态。
    bool startupRecovered = false;
    bool lockLocalRecoveryCandidate = false;
    bool lockLocalRecoveryConfirmed = false;
    int localRecoveryPendingCount = 0;
    double localRecoveryResidualSamples = NaN;
    QString recoveryEvidence;
    bool templateTrackingUsed = false;
    bool trackingSoftRecovered = false;
    double trackingFitPredictionResidualSamples = NaN;
    // 多模板跟踪诊断：RECENT / CONFIRMED / EMA / SHAPE / FIT_SOFT。
    QString trackingTemplateSource = QStringLiteral("NONE");
    double recentTemplateCorrelation = NaN;
    double confirmedTemplateCorrelation = NaN;
    double emaTemplateCorrelation = NaN;
    double shapeTemplateCorrelation = NaN;
    double recentTemplatePeak = NaN;
    double confirmedTemplatePeak = NaN;
    double emaTemplatePeak = NaN;
    double shapeTemplatePeak = NaN;
    int multiTemplateHardMatchCount = 0;
    double multiTemplateAgreementSpreadSamples = NaN;
    bool multiTemplateUsed = false;
    int multiTemplateBackupPendingCount = 0;
    bool multiTemplateBackupConfirmed = false;
    bool lockedBroadPacketCandidate = false;
    bool lockedBroadPacketRecovered = false;
    double lockedBroadEnergyPeakDistanceSamples = NaN;
    // LOCK碎片化宽波包恢复诊断。最终厚度仍由RECENT整包模板相关ToF计算，
    // high-core只是“信号存在”的seed，不允许其局部最大峰直接成为厚度ToF。
    bool lockedFragmentedPacketCandidate = false;
    bool lockedFragmentedPacketRecovered = false;
    bool lockedStraddlingPacketCandidate = false;
    bool lockedStraddlingPacketRecovered = false;
    // 能量歧义但仅一个主峰位于预测门内时，模板认证后的结果仅只读输出。
    bool lockedAmbiguousReadOnlyRecovered = false;
    // 低相关/低SNR边界仅在拟合、预测与模板同时一致时只读输出，不更新历史。
    bool guardedReadOnlyRecovered = false;
    bool lockedTemplateOnlyCandidate = false;
    bool lockedTemplateOnlyRecovered = false;
    bool lockRecoveryNeedsConfirmation = false;
    int lockedFragmentedCoreWidthSamples = 0;
    int lockedFragmentedSupportWidthSamples = 0;
    double lockedFragmentedSupportThresholdSnrDb = NaN;
    double lockedFragmentedPredictionResidualSamples = NaN;
    // SEARCH启动阶段：整包拟合中心可能落在双峰/多瓣内部谷值。仅允许在已通过
    // 单候选+高质量拟合的同一波包内、拟合中心附近小支持区重新评价信号存在性；
    // 此路径必须再经相邻两帧ToF一致性确认后才真正输出并建立初始LOCK。
    bool startupSupportRecovered = false;
    // 预锁定阶段的低SNR强主候选可输出，但必须只读，不能提前改写模板/位置历史。
    bool prelockReadOnlyRecovered = false;
    double startupSupportSnrDb = NaN;
    int startupPendingCount = 0;
    // 延迟确认回填：首帧先作为PENDING缓存，下一帧确认同一新分支后恢复为VALID。
    bool delayedConfirmationBackfilled = false;
    int delayedConfirmationDelayFrames = 0;
    // 保留该诊断字段兼容旧导出；新LOCK逻辑优先使用整包模板相关ToF。
    bool trackingFitFallback = false;
    QVariantList fittedPoints;
    // 下一帧相关使用真实滤波采样，不以拟合曲线替代测量模板。
    std::vector<double> trackingTemplate;
    int trackingAnchor = -1;
};

double medianValueInPlace(std::vector<double> &values)
{
    if (values.empty())
        return NaN;
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    const double upper = *middle;
    return values.size() % 2 ? upper
        : 0.5 * (upper + *std::max_element(values.begin(), middle));
}

double medianValue(std::vector<double> values)
{
    // 兼容原有调用：仍按值复制，保证调用方数据不被nth_element重排。
    return medianValueInPlace(values);
}

enum class SearchWindowStage
{
    GlobalSearch,
    Warmup,
    Prediction,
    GlobalReacquire
};

struct PredictionGate
{
    double center = NaN;
    double lastAcceptedPeak = NaN;
    int historyCount = 0;
    std::vector<double> templateValues;
    double anchorFraction = 0.0;
    std::vector<double> confirmedTemplateValues;
    double confirmedAnchorFraction = 0.0;
    std::vector<double> emaTemplateValues;
    double emaAnchorFraction = 0.0;
    std::vector<double> shapeTemplateValues;
    double shapeAnchorFraction = 0.0;
    bool lockActive = false;
    bool reacquireRequired = false;
    bool startupConfirmationEnabled = false;
    bool warmupActive = false;
    int warmupFrames = 0;
    int missCount = 0;
    double searchWindowAnchor = NaN;
    SearchWindowStage windowStage = SearchWindowStage::GlobalSearch;
    // LOST后保留的冻结长期身份。它只用于重捕获认证，不直接作为厚度输出。
    bool longTermIdentityAvailable = false;
    double longTermIdentityCenter = NaN;
    std::vector<double> frozenIdentityTemplate;
    double frozenIdentityFraction = 0.0;

    // 由PeakHistory预先计算的自适应拟合脉宽基线，当前帧只读取，不在检测函数中改历史。
    int adaptiveWidthHistoryCount = 0;
    bool adaptiveWidthReady = false;
    double adaptiveWidthMedianSamples = NaN;
    double adaptiveWidthMadSamples = NaN;
    double adaptiveWidthToleranceSamples = NaN;

    // 上一帧已经平滑后的噪声底[dB]，供当前帧快升慢降EWMA使用。
    double temporalNoiseFloorDb = NaN;
};

void rejectWaveform(WaveformResult &r, const QString &reason,
                    const QString &message, bool noSignal);

struct PeakHistory
{
    // LOST仍按原predictionMaxMisses及时进入；冻结身份搜索单独延迟，
    // 先给普通候选+连续两帧重捕获链恢复机会，避免身份模板提前接管。
    static constexpr int PredictionRecoveryGraceFrames = 6;
    static constexpr int FrozenIdentitySearchDelayFrames = 6;

    std::deque<double> validPeaks;
    std::deque<int> validTimes;
    std::vector<double> lastTemplate;
    // CONFIRMED与RECENT物理身份分离：由连续两帧EMA/SHAPE后备救援确认建立，
    // 用于持续形态变化阶段，但永不覆盖RECENT。
    std::vector<double> confirmedBackupTemplate;
    double confirmedBackupFraction = 0.0;
    std::vector<double> emaTemplate;
    std::vector<double> shapeTemplate;
    double emaFraction = 0.0;
    double shapeFraction = 0.0;
    QString directory;
    int sampleCount = 0;
    int misses = 0;
    // 预测窗外强候选需连续两帧位置一致才允许提前结束恢复宽限期。
    int strongAlternativeCount = 0;
    double strongAlternativePeak = NaN;
    int clock = 0;
    double lastFraction = 0.0;
    bool lockActive = false;
    bool everLocked = false;
    bool reacquireRequired = false;
    // 文件夹/连续帧回放启用两帧启动确认；单文件离线分析没有下一帧，直接保留候选结果。
    bool startupConfirmationEnabled = true;
    // 启动学习阶段：初始LOCK建立后，不立即进入完全稳定跟踪；
    // 保留若干帧用于模板、脉宽和噪声模型收敛，避免冷启动阶段过早使用严格LOCK约束。
    bool warmupLearningActive = false;
    int warmupLearningFrames = 0;
    // Session Search Window锚点：由实际首波身份学习得到，不再由reference thickness换算。
    // LOST时保留；只有新目录/新采集会话resetAll()才清空。
    double searchWindowAnchor = NaN;
    // 启动/重锁后的前若干有效帧不使用窄位置门，只在全局首波物理ROI内学习身份和速度。
    // 连续8个有效帧后才进入PREDICTION，避免刚锁定就因位置跳动重新失锁。
    static constexpr int WarmupLearningTargetFrames = 8;
    double pendingPeak = NaN;
    int pendingTime = 0;
    int pendingCount = 0;
    bool hasPendingWaveform = false;
    bool pendingFrozenIdentityAuthenticated = false;
    WaveformResult pendingWaveform;
    bool backfillReady = false;
    int backfillTargetClock = 0;
    WaveformResult backfillWaveform;
    std::deque<double> adaptiveWidthHistory;
    double temporalNoiseFloorDb = NaN;
    // EMA/SHAPE后备模板只在连续两帧给出相近ToF后才允许推动RECENT适应。
    int backupTemplatePendingCount = 0;
    int backupTemplatePendingTime = 0;
    double backupTemplatePendingPeak = NaN;
    double backupTemplatePendingFraction = 0.0;
    std::vector<double> backupTemplatePendingValues;
    // LOST时冻结最近一次可靠纵波身份：优先CONFIRMED，其次EMA，最后RECENT。
    // 短期位置/速度历史清空后，该模板只用于后续候选身份认证。
    std::vector<double> frozenIdentityTemplate;
    double frozenIdentityFraction = 0.0;
    double frozenIdentityCenter = NaN;
    int frozenIdentityTime = 0;
    int frozenIdentityHoldFrames = 0;

    void clearFrozenIdentity()
    {
        frozenIdentityTemplate.clear();
        frozenIdentityFraction = 0.0;
        frozenIdentityCenter = NaN;
        frozenIdentityTime = 0;
        frozenIdentityHoldFrames = 0;
    }

    void freezeLongTermIdentity()
    {
        if (!validPeaks.empty()) {
            frozenIdentityCenter = validPeaks.back();
            frozenIdentityTime = validTimes.back();
        }
        if (!confirmedBackupTemplate.empty()) {
            frozenIdentityTemplate = confirmedBackupTemplate;
            frozenIdentityFraction = confirmedBackupFraction;
        } else if (!emaTemplate.empty()) {
            frozenIdentityTemplate = emaTemplate;
            frozenIdentityFraction = emaFraction;
        } else if (!lastTemplate.empty()) {
            frozenIdentityTemplate = lastTemplate;
            frozenIdentityFraction = lastFraction;
        }
    }

    void clearBackupTemplatePending()
    {
        backupTemplatePendingCount = 0;
        backupTemplatePendingTime = 0;
        backupTemplatePendingPeak = NaN;
        backupTemplatePendingFraction = 0.0;
        backupTemplatePendingValues.clear();
    }

    void clearPending()
    {
        pendingPeak = NaN;
        pendingTime = 0;
        pendingCount = 0;
        hasPendingWaveform = false;
        pendingFrozenIdentityAuthenticated = false;
        pendingWaveform = WaveformResult{};
    }

    bool takeBackfill(WaveformResult &out, int &targetClock)
    {
        if (!backfillReady)
            return false;
        out = backfillWaveform;
        targetClock = backfillTargetClock;
        backfillReady = false;
        backfillTargetClock = 0;
        backfillWaveform = WaveformResult{};
        return true;
    }

    void clearTracking()
    {
        validPeaks.clear();
        validTimes.clear();
        lastTemplate.clear();
        confirmedBackupTemplate.clear();
        emaTemplate.clear();
        shapeTemplate.clear();
        lastFraction = 0.0;
        confirmedBackupFraction = 0.0;
        emaFraction = 0.0;
        shapeFraction = 0.0;
        clearBackupTemplatePending();
        lockActive = false;
    }

    void resetAll()
    {
        clearTracking();
        clearFrozenIdentity();
        clearPending();
        misses = 0;
        strongAlternativeCount = 0;
        strongAlternativePeak = NaN;
        clock = 0;
        everLocked = false;
        reacquireRequired = false;
        warmupLearningActive = false;
        warmupLearningFrames = 0;
        searchWindowAnchor = NaN;
        backfillReady = false;
        backfillTargetClock = 0;
        backfillWaveform = WaveformResult{};
        adaptiveWidthHistory.clear();
        temporalNoiseFloorDb = NaN;
    }

    void prepare(const QString &path, int count)
    {
        if (directory != path || sampleCount != count)
            resetAll();
        directory = path;
        sampleCount = count;
        ++clock;
    }

    PredictionGate predict(const TestParameters &p) const
    {
        PredictionGate gate;
        gate.historyCount = static_cast<int>(validPeaks.size());
        if (!validPeaks.empty())
            gate.lastAcceptedPeak = validPeaks.back();
        gate.lockActive = lockActive;
        gate.reacquireRequired = reacquireRequired;
        gate.startupConfirmationEnabled = startupConfirmationEnabled;
        gate.warmupActive = warmupLearningActive;
        gate.warmupFrames = warmupLearningFrames;
        gate.missCount = misses;
        gate.searchWindowAnchor = searchWindowAnchor;
        gate.windowStage = reacquireRequired
            ? SearchWindowStage::GlobalReacquire
            : (!lockActive ? SearchWindowStage::GlobalSearch
                           : (warmupLearningActive ? SearchWindowStage::Warmup
                                                   : SearchWindowStage::Prediction));
        if (lockActive) {
            gate.templateValues = lastTemplate;
            gate.anchorFraction = lastFraction;
            gate.confirmedTemplateValues = confirmedBackupTemplate;
            gate.confirmedAnchorFraction = confirmedBackupFraction;
            gate.emaTemplateValues = emaTemplate;
            gate.emaAnchorFraction = emaFraction;
            gate.shapeTemplateValues = shapeTemplate;
            gate.shapeAnchorFraction = shapeFraction;
        } else if (reacquireRequired && std::isfinite(frozenIdentityCenter)) {
            // LOST后立即保留最后可靠位置，用于渐进扩窗；冻结模板仍按原延时策略
            // 在普通两帧重捕获继续失败后才参与身份认证。
            gate.longTermIdentityCenter = frozenIdentityCenter;
            if (!frozenIdentityTemplate.empty()
                && misses >= p.predictionMaxMisses
                             + PredictionRecoveryGraceFrames
                             + FrozenIdentitySearchDelayFrames) {
                gate.frozenIdentityTemplate = frozenIdentityTemplate;
                gate.frozenIdentityFraction = frozenIdentityFraction;
                gate.longTermIdentityAvailable = true;
            }
        }
        if (lockActive && !validPeaks.empty()) {
            // 位置身份确认不等于速度已经稳定。冷启动/重锁后只有2~3个位置点时，
            // 两点速度极易被多瓣内部切换放大，导致预测窗自己跑离真实packet。
            // 至少4个有效位置点（3个速度样本）后才启用median velocity；此前只用
            // 最后可靠位置作为中心，由GLOBAL/WARMUP或正常±16门继续收集稳定历史。
            constexpr int minimumVelocityHistoryCount = 4;
            double velocity = 0.0;
            if (gate.historyCount >= minimumVelocityHistoryCount) {
                std::vector<double> speeds;
                speeds.reserve(static_cast<size_t>(gate.historyCount - 1));
                for (int i = 1; i < gate.historyCount; ++i)
                    speeds.push_back((validPeaks[i] - validPeaks[i - 1])
                                    / std::max(1, validTimes[i] - validTimes[i - 1]));
                if (!speeds.empty())
                    velocity = std::clamp(medianValue(speeds),
                                          -double(p.predictionHalfWidth),
                                           double(p.predictionHalfWidth));
            }
            gate.center = validPeaks.back() + velocity * (clock - validTimes.back());
        }

        gate.temporalNoiseFloorDb = temporalNoiseFloorDb;

        gate.adaptiveWidthHistoryCount = static_cast<int>(adaptiveWidthHistory.size());
        if (gate.adaptiveWidthHistoryCount >= p.adaptiveWidthMinHistory) {
            std::vector<double> widths(adaptiveWidthHistory.begin(), adaptiveWidthHistory.end());
            gate.adaptiveWidthMedianSamples = medianValue(widths);
            std::vector<double> deviations;
            deviations.reserve(widths.size());
            for (double width : widths)
                deviations.push_back(std::abs(width - gate.adaptiveWidthMedianSamples));
            gate.adaptiveWidthMadSamples = 1.4826 * medianValue(deviations);
            gate.adaptiveWidthToleranceSamples = std::max(
                p.adaptiveWidthMinToleranceSamples,
                p.adaptiveWidthMadMultiplier * gate.adaptiveWidthMadSamples);
            gate.adaptiveWidthReady = std::isfinite(gate.adaptiveWidthToleranceSamples);
        }
        return gate;
    }

    void miss(const TestParameters &p, bool forceLost = false)
    {
        ++misses;
        // 无论LOST重捕获还是LOCK跳变确认，都必须由紧邻下一帧连续确认；
        // 中间出现任何INVALID/NO_SIGNAL都立即取消pending，禁止隔帧回填。
        if (pendingCount > 0)
            clearPending();
        clearBackupTemplatePending();
        // 普通弱包/形态失败先保留原预测链；若检测到预测窗外强候选，
        // forceLost会提前结束宽限期并恢复原两帧重捕获节奏。
        const int lostTransitionMisses =
            p.predictionMaxMisses + PredictionRecoveryGraceFrames;
        if (lockActive && (misses >= lostTransitionMisses || forceLost)) {
            freezeLongTermIdentity();
            clearTracking();
            reacquireRequired = everLocked;
            clearPending();
        }
    }

    void appendAccepted(WaveformResult &r, const TestParameters &p)
    {
        misses = 0;
        if (warmupLearningActive) {
            ++warmupLearningFrames;
            if (warmupLearningFrames >= WarmupLearningTargetFrames)
                warmupLearningActive = false;
        }
        strongAlternativeCount = 0;
        strongAlternativePeak = NaN;

        // 自适应脉宽只学习高质量、完整拟合的真实测量。软恢复、宽包恢复和单峰回退
        // 不写入模型；已建立模型后，极端WIDTH_OUTLIER也不写入，避免异常宽包拖动基线。
        const bool widthTrainingSample = r.fitSucceeded
            && std::isfinite(r.fitPeakWidthSamples)
            && r.fitPeakWidthSamples > 0.0
            && !r.fallbackPeakUsed
            && !r.trackingSoftRecovered
            && !r.lockedBroadPacketRecovered
            && !r.lockedFragmentedPacketRecovered
            && r.candidateCount == 1
            && std::isfinite(r.fitR2)
            && r.fitR2 >= std::max(0.80, p.fitMinR2)
            && std::isfinite(r.snrDb)
            && r.snrDb >= p.minLocalSnrDb + 2.0
            && r.adaptiveWidthState != QStringLiteral("WIDTH_OUTLIER");
        if (widthTrainingSample) {
            adaptiveWidthHistory.push_back(r.fitPeakWidthSamples);
            while (static_cast<int>(adaptiveWidthHistory.size()) > p.adaptiveWidthHistorySize)
                adaptiveWidthHistory.pop_front();
        }

        const double currentFraction = r.interpolatedPeakIndex - r.trackingAnchor;
        auto appendHistoryPoint = [this](double peak, int time) {
            validPeaks.push_back(peak);
            validTimes.push_back(time);
        };
        auto blendIdentityTemplate = [](std::vector<double> &target, double &targetFraction,
                                        const std::vector<double> &sample, double sampleFraction,
                                        double alpha) {
            if (target.size() != sample.size() || target.empty()) {
                target = sample;
                targetFraction = sampleFraction;
                return;
            }
            for (size_t i = 0; i < target.size(); ++i)
                target[i] = (1.0 - alpha) * target[i] + alpha * sample[i];
            targetFraction = (1.0 - alpha) * targetFraction + alpha * sampleFraction;
        };

        // LOST冻结身份重锁后的保持期：只更新真实位置历史，不覆盖RECENT相位模板。
        // 只有连续高质量正常LOCK帧才消耗保持计数；低SNR/特殊恢复帧不会解除身份锚定。
        if (frozenIdentityHoldFrames > 0) {
            const bool stableIdentityConfirmation = r.trackingTemplateSource == QStringLiteral("RECENT")
                && !r.lowSnrRecovered && !r.trackingSoftRecovered
                && !r.lockedBroadPacketRecovered && !r.lockedFragmentedPacketRecovered
                && !r.lockedStraddlingPacketRecovered && !r.lockedTemplateOnlyRecovered
                && r.fitSucceeded && std::isfinite(r.fitR2) && r.fitR2 >= std::max(0.95, p.fitMinR2)
                && std::isfinite(r.snrDb) && r.snrDb >= p.minLocalSnrDb + 2.0
                && std::isfinite(r.recentTemplateCorrelation) && r.recentTemplateCorrelation >= 0.90
                && std::isfinite(r.trackingResidualSamples) && std::abs(r.trackingResidualSamples) <= 2.0;
            if (stableIdentityConfirmation)
                --frozenIdentityHoldFrames;
            if (frozenIdentityHoldFrames == 0 && !r.trackingTemplate.empty()) {
                // 身份重新确认后，从当前“正常强信号”帧重新开始短期位置/速度历史，
                // 不让之前LOST恢复帧的内部瓣位置长期影响prediction velocity。
                validPeaks.clear();
                validTimes.clear();
                appendHistoryPoint(r.interpolatedPeakIndex, clock);
                lastTemplate = r.trackingTemplate;
                lastFraction = currentFraction;
                clearFrozenIdentity();
            }
            clearBackupTemplatePending();
        // RECENT与CONFIRMED身份完全分离。EMA/SHAPE单帧救援由observeBackupRescue()
        // 单独管理，不会进入本函数；因此普通有效帧不会被“未确认后备救援”污染状态机。
        } else if (r.lowSnrRecovered || r.lockedBroadPacketRecovered
                   || r.lockedFragmentedPacketRecovered || r.lockedStraddlingPacketRecovered
                   || r.lockedAmbiguousReadOnlyRecovered || r.lockedTemplateOnlyRecovered) {
            // 已通过RECENT/历史预测严格认证的特殊恢复帧继续推进真实位置链，
            // 但绝不覆盖RECENT身份模板。这样宽包连续出现时不会被误累计为miss并提前进入LOST。
            appendHistoryPoint(r.interpolatedPeakIndex, clock);
            clearBackupTemplatePending();
        } else if (r.trackingTemplateSource == QStringLiteral("RECENT") || !r.multiTemplateUsed) {
            appendHistoryPoint(r.interpolatedPeakIndex, clock);
            lastTemplate = r.trackingTemplate;
            lastFraction = currentFraction;
            clearBackupTemplatePending();
        } else if (r.trackingTemplateSource == QStringLiteral("CONFIRMED")) {
            appendHistoryPoint(r.interpolatedPeakIndex, clock);
            if (!r.trackingTemplate.empty())
                blendIdentityTemplate(confirmedBackupTemplate, confirmedBackupFraction,
                                      r.trackingTemplate, currentFraction,
                                      p.trackingBackupRecentBlendAlpha);
            clearBackupTemplatePending();
        }

        // EMA/形态模板只学习高质量、单候选、完整拟合的强信号帧。软恢复、低SNR、
        // 宽包恢复、fallback和脉宽极端异常都不允许污染慢模板。
        const bool initialTemplateSeed = !lockActive && emaTemplate.empty();
        const bool stableLockedTemplateSample = lockActive
            && r.trackingTemplateSource == QStringLiteral("RECENT")
            && std::isfinite(r.recentTemplateCorrelation)
            && r.recentTemplateCorrelation >= 0.85
            && std::isfinite(r.trackingResidualSamples)
            && std::abs(r.trackingResidualSamples) <= 2.0;
        const bool templateTrainingSample = frozenIdentityHoldFrames == 0
            && !r.trackingTemplate.empty()
            && (initialTemplateSeed || stableLockedTemplateSample)
            && r.fitSucceeded
            && !r.fallbackPeakUsed
            && !r.trackingSoftRecovered
            && !r.lowSnrRecovered
            && !r.multiTemplateUsed
            && !r.lockedBroadPacketRecovered
            && !r.lockedFragmentedPacketRecovered
            && !r.lockedStraddlingPacketRecovered
            && !r.lockedAmbiguousReadOnlyRecovered
            && !r.lockedTemplateOnlyRecovered
            && r.candidateCount == 1
            && std::isfinite(r.fitR2)
            && r.fitR2 >= std::max(0.95, p.fitMinR2)
            && std::isfinite(r.snrDb)
            && r.snrDb >= p.minLocalSnrDb + 2.0
            && r.adaptiveWidthState != QStringLiteral("WIDTH_OUTLIER");
        auto blendTemplate = [](std::vector<double> &target, double &targetFraction,
                                const std::vector<double> &sample, double sampleFraction,
                                double alpha) {
            if (target.size() != sample.size() || target.empty()) {
                target = sample;
                targetFraction = sampleFraction;
                return;
            }
            for (size_t i = 0; i < target.size(); ++i)
                target[i] = (1.0 - alpha) * target[i] + alpha * sample[i];
            targetFraction = (1.0 - alpha) * targetFraction + alpha * sampleFraction;
        };
        if (templateTrainingSample) {
            blendTemplate(emaTemplate, emaFraction, r.trackingTemplate,
                          currentFraction, p.trackingEmaTemplateAlpha);
            if (r.adaptiveWidthState == QStringLiteral("WIDTH_TRANSITION"))
                blendTemplate(shapeTemplate, shapeFraction, r.trackingTemplate,
                              currentFraction, p.trackingShapeTemplateAlpha);
        }
        while (static_cast<int>(validPeaks.size()) > p.predictionHistorySize) {
            validPeaks.pop_front();
            validTimes.pop_front();
        }

        // Session Search Window只从已经被核心链接受的真实TOF学习。首次至少3个点后
        // 用中位数建立锚点；以后只有最近3个有效点整体偏离锚点超过24点时才重心迁移，
        // 避免单帧跳变让noise floor / packet分割窗口跟着抖动。
        if (!std::isfinite(searchWindowAnchor) && validPeaks.size() >= 3) {
            std::vector<double> recent{validPeaks[validPeaks.size() - 3],
                                       validPeaks[validPeaks.size() - 2],
                                       validPeaks.back()};
            searchWindowAnchor = medianValue(recent);
        } else if (std::isfinite(searchWindowAnchor) && validPeaks.size() >= 3) {
            std::vector<double> recent{validPeaks[validPeaks.size() - 3],
                                       validPeaks[validPeaks.size() - 2],
                                       validPeaks.back()};
            const double recentCenter = medianValue(recent);
            const double recenterThreshold = std::max(24.0, 1.5 * p.predictionHalfWidth);
            if (std::abs(recentCenter - searchWindowAnchor) > recenterThreshold)
                searchWindowAnchor = recentCenter;
        }

        if (!lockActive && static_cast<int>(validPeaks.size()) >= p.predictionMinValid) {
            lockActive = true;
            everLocked = true;
            warmupLearningActive = true;
            warmupLearningFrames = 1;
            r.trackingState = QStringLiteral("WARMUP_LEARNING");
        }
    }

    bool observeBackupRescue(WaveformResult &r, const TestParameters &p)
    {
        const double candidatePeak = r.interpolatedPeakIndex;
        const double currentFraction = candidatePeak - r.trackingAnchor;
        const bool consecutiveBackup = backupTemplatePendingCount == 1
            && backupTemplatePendingTime == clock - 1
            && std::isfinite(backupTemplatePendingPeak)
            && std::abs(candidatePeak - backupTemplatePendingPeak) <= 2.0
            && backupTemplatePendingValues.size() == r.trackingTemplate.size();

        if (!consecutiveBackup) {
            backupTemplatePendingCount = 1;
            backupTemplatePendingTime = clock;
            backupTemplatePendingPeak = candidatePeak;
            backupTemplatePendingFraction = currentFraction;
            backupTemplatePendingValues = r.trackingTemplate;
            r.multiTemplateBackupPendingCount = 1;
            r.trackingState = QStringLiteral("BACKUP_TENTATIVE");
            // 未确认救援不改变RECENT/位置历史，也不把身份miss清零。连续多帧无法确认时，
            // 按原predictionMaxMisses进入LOST，避免“看起来VALID”却长期拖住错误身份。
            ++misses;
            if (lockActive && misses >= p.predictionMaxMisses) {
                freezeLongTermIdentity();
                clearTracking();
                reacquireRequired = everLocked;
            }
            return false;
        }

        r.multiTemplateBackupPendingCount = 2;
        r.multiTemplateBackupConfirmed = true;
        r.trackingState = QStringLiteral("BACKUP_CONFIRMED");

        std::vector<double> confirmedTemplate(r.trackingTemplate.size());
        for (size_t i = 0; i < confirmedTemplate.size(); ++i)
            confirmedTemplate[i] = 0.5 * (backupTemplatePendingValues[i] + r.trackingTemplate[i]);
        confirmedBackupTemplate = std::move(confirmedTemplate);
        confirmedBackupFraction = 0.5 * (backupTemplatePendingFraction + currentFraction);

        // 第一帧救援之前没有进入身份历史；确认后一次性补入两帧，恢复连续速度估计。
        validPeaks.push_back(backupTemplatePendingPeak);
        validTimes.push_back(backupTemplatePendingTime);
        validPeaks.push_back(candidatePeak);
        validTimes.push_back(clock);
        while (static_cast<int>(validPeaks.size()) > p.predictionHistorySize) {
            validPeaks.pop_front();
            validTimes.pop_front();
        }
        misses = 0;
        lockActive = true;
        everLocked = true;
        reacquireRequired = false;
        clearBackupTemplatePending();
        return true;
    }

    void startStartupPending(WaveformResult &r, double candidatePeak)
    {
        pendingPeak = candidatePeak;
        pendingTime = clock;
        pendingCount = 1;
        pendingWaveform = r;
        hasPendingWaveform = true;
        r.startupPendingCount = 1;
        r.trackingState = QStringLiteral("STARTUP_PENDING");
        rejectWaveform(r, QStringLiteral("STARTUP_PENDING"),
            r.startupSupportRecovered
                ? QStringLiteral("SEARCH启动候选由同一波包邻域SNR恢复；需下一帧ToF在±3点内确认后建立初始LOCK")
                : QStringLiteral("SEARCH启动候选需下一帧ToF在±3点内确认后建立初始LOCK，禁止单帧偶然峰直接建立纵波身份"),
            false);
    }

    bool confirmStartupPending(WaveformResult &r, double candidatePeak)
    {
        constexpr double STARTUP_CONSISTENCY_SAMPLES = 3.0;
        constexpr int STARTUP_CONFIRM_FRAMES = 2;
        if (pendingCount <= 0 || !std::isfinite(pendingPeak)
            || pendingTime != clock - 1
            || std::abs(candidatePeak - pendingPeak) > STARTUP_CONSISTENCY_SAMPLES)
            return false;

        ++pendingCount;
        r.startupPendingCount = pendingCount;
        if (pendingCount < STARTUP_CONFIRM_FRAMES)
            return false;

        const double firstPeak = pendingPeak;
        const int firstTime = pendingTime;
        if (hasPendingWaveform && clock - pendingTime == 1) {
            backfillWaveform = pendingWaveform;
            backfillWaveform.measurementStatus = QStringLiteral("VALID");
            backfillWaveform.rejectionReason.clear();
            backfillWaveform.delayedConfirmationBackfilled = true;
            backfillWaveform.delayedConfirmationDelayFrames = 1;
            backfillWaveform.startupPendingCount = STARTUP_CONFIRM_FRAMES;
            backfillWaveform.trackingState = QStringLiteral("BACKFILLED_STARTUP_LOCK");
            backfillWaveform.error += QStringLiteral("；下一帧ToF一致，启动候选已延迟确认回填");
            backfillTargetClock = pendingTime;
            backfillReady = true;
        }

        // 启动确认后只用这两个连续真实候选建立短期身份，避免早期单帧偶然峰污染。
        clearTracking();
        validPeaks.push_back(firstPeak);
        validTimes.push_back(firstTime);
        validPeaks.push_back(candidatePeak);
        validTimes.push_back(clock);
        lastTemplate = r.trackingTemplate;
        lastFraction = r.interpolatedPeakIndex - r.trackingAnchor;
        if (!r.trackingTemplate.empty()) {
            emaTemplate = r.trackingTemplate;
            emaFraction = lastFraction;
        }
        lockActive = true;
        everLocked = true;
        reacquireRequired = false;
        misses = 0;
        // 首次锁定后进入短暂WARMUP，只学习身份模型，不改变已有输出逻辑。
        // 这样避免启动阶段下一帧立即受到完整LOCK约束导致冷启动漏检。
        warmupLearningActive = true;
        warmupLearningFrames = 2;
        clearPending();
        r.trackingState = QStringLiteral("WARMUP_LEARNING");
        return true;
    }

    void startPending(WaveformResult &r, const TestParameters &p,
                      double candidatePeak, bool severeJump)
    {
        pendingPeak = candidatePeak;
        pendingTime = clock;
        pendingCount = 1;
        // rejectWaveform() 会清空当前厚度/峰位，因此必须先保存完整候选结果。
        // 若下一帧确认同一新分支，则用该原始测量结果回填首帧，而不是插值或预测造值。
        pendingWaveform = r;
        hasPendingWaveform = true;
        pendingFrozenIdentityAuthenticated =
            r.trackingState == QStringLiteral("LOST_IDENTITY_CANDIDATE");
        r.reacquirePendingCount = 1;
        r.trackingState = reacquireRequired ? QStringLiteral("REACQUIRE_PENDING")
                                            : QStringLiteral("JUMP_PENDING");
        const QString reason = severeJump ? QStringLiteral("TRACKING_JUMP")
                                          : QStringLiteral("REACQUIRE_PENDING");
        rejectWaveform(r, reason,
            QStringLiteral("候选ToF相对历史趋势发生跳变，需连续%1帧在相近位置确认后才恢复输出")
                .arg(p.reacquireConfirmFrames), false);
    }

    bool confirmPending(WaveformResult &r, const TestParameters &p,
                        double candidatePeak)
    {
        if (pendingCount <= 0 || !std::isfinite(pendingPeak)
            || std::abs(candidatePeak - pendingPeak) > p.reacquireConsistencySamples)
            return false;

        ++pendingCount;
        r.reacquirePendingCount = pendingCount;
        if (pendingCount < p.reacquireConfirmFrames)
            return false;

        // 第一帧只作为内部重捕获证据，不对外输出；确认成功后用“第一候选+当前帧”
        // 重建短期趋势，并以当前真实波形模板重新LOCK。
        const double firstPeak = pendingPeak;
        const int firstTime = pendingTime;

        // 当前帧已经证明第一帧候选是同一连续新分支：将第一帧原始测量结果
        // 标记为延迟确认回填。这里只恢复当时真实寻到的峰和厚度，不做插值。
        if (hasPendingWaveform && clock - pendingTime == 1) {
            backfillWaveform = pendingWaveform;
            backfillWaveform.measurementStatus = QStringLiteral("VALID");
            backfillWaveform.rejectionReason.clear();
            backfillWaveform.delayedConfirmationBackfilled = true;
            backfillWaveform.delayedConfirmationDelayFrames = std::max(1, clock - pendingTime);
            backfillWaveform.reacquirePendingCount = p.reacquireConfirmFrames;
            backfillWaveform.trackingState = QStringLiteral("BACKFILLED_RELOCKED");
            backfillWaveform.error += QStringLiteral("；下一帧确认同一新分支，首帧已延迟回填") ;
            backfillTargetClock = pendingTime;
            backfillReady = true;
        }

        // 若两帧都通过冻结长期身份认证，重锁后先进入短暂“身份保持期”：位置历史使用
        // 两帧真实ToF，但RECENT模板继续使用冻结身份，避免刚恢复的多瓣波形立即把相位锚点
        // 换到另一内部瓣。连续5帧高质量正常LOCK确认后才解除保持并重新学习慢模板。
        const bool frozenAuthenticated = pendingFrozenIdentityAuthenticated
            && r.trackingState == QStringLiteral("LOST_IDENTITY_CANDIDATE")
            && !frozenIdentityTemplate.empty();
        clearTracking();
        validPeaks.push_back(firstPeak);
        validTimes.push_back(firstTime);
        validPeaks.push_back(candidatePeak);
        validTimes.push_back(clock);
        if (frozenAuthenticated) {
            lastTemplate = frozenIdentityTemplate;
            lastFraction = frozenIdentityFraction;
            emaTemplate.clear();
            shapeTemplate.clear();
            frozenIdentityHoldFrames = 5;
        } else {
            lastTemplate = r.trackingTemplate;
            lastFraction = r.interpolatedPeakIndex - r.trackingAnchor;
            if (!r.trackingTemplate.empty()) {
                emaTemplate = r.trackingTemplate;
                emaFraction = lastFraction;
            }
            clearFrozenIdentity();
        }
        while (static_cast<int>(validPeaks.size()) > p.predictionHistorySize) {
            validPeaks.pop_front();
            validTimes.pop_front();
        }
        lockActive = true;
        everLocked = true;
        reacquireRequired = false;
        misses = 0;
        // LOST后的重锁已经有冻结身份/历史模板，不重新进入冷启动WARMUP。
        // 直接恢复PREDICTION，避免成熟运行阶段反复“重新学习”而降低检出率。
        warmupLearningActive = false;
        warmupLearningFrames = 0;
        clearPending();
        r.trackingState = QStringLiteral("RELOCKED");
        return true;
    }

    void observe(WaveformResult &r, const TestParameters &p)
    {
        // 噪声底历史与厚度有效性解耦：只要本帧成功完成背景估计，就更新快升慢降EWMA。
        // 因此NO_SIGNAL帧也能帮助跟踪现场噪声变化；Raw污染/背景不足帧则不会更新。
        if (std::isfinite(r.temporalNoiseFloorDb))
            temporalNoiseFloorDb = r.temporalNoiseFloorDb;

        if (r.measurementStatus != "VALID" || !std::isfinite(r.thicknessMm)
            || (!r.fitSucceeded && !r.fallbackPeakUsed
                && !r.lockedFragmentedPacketRecovered && !r.lockedTemplateOnlyRecovered)
            || !r.longitudinalCandidate) {
            // 弱包/形态波动继续使用预测链宽限期。只有预测窗外的高SNR候选连续
            // 两帧位置一致，才判定为真实ToF分支跳移并提前交回原LOST两帧重捕获；
            // 单帧毛刺或形态相关失配不能终止宽限期，且这里不放宽任何有效性门限。
            const PredictionGate currentGate = predict(p);
            const double alternativePeak = std::isfinite(r.fitPeakIndex)
                ? r.fitPeakIndex
                : (r.firstCandidateIndex >= 0 ? double(r.firstCandidateIndex) : NaN);
            const bool strongAlternative = lockActive
                && !warmupLearningActive
                && misses + 1 >= p.predictionMaxMisses
                && (r.rejectionReason == QStringLiteral("PREDICTION_GATE")
                    || r.rejectionReason == QStringLiteral("TRACKING_MISMATCH"))
                && std::isfinite(r.maxLocalSnrDb)
                && r.maxLocalSnrDb >= p.minLocalSnrDb + 3.0
                && std::isfinite(currentGate.center)
                && std::isfinite(alternativePeak)
                && std::abs(alternativePeak - currentGate.center)
                    > p.trackingResidualAcceptSamples;
            if (strongAlternative) {
                if (strongAlternativeCount > 0
                    && std::abs(alternativePeak - strongAlternativePeak)
                        <= p.reacquireConsistencySamples)
                    ++strongAlternativeCount;
                else
                    strongAlternativeCount = 1;
                strongAlternativePeak = alternativePeak;
            } else {
                strongAlternativeCount = 0;
                strongAlternativePeak = NaN;
            }
            const bool confirmedAlternativeBranch = strongAlternativeCount >= 2;
            miss(p, confirmedAlternativeBranch);
            if (confirmedAlternativeBranch) {
                strongAlternativeCount = 0;
                strongAlternativePeak = NaN;
            }
            r.trackingState = reacquireRequired ? QStringLiteral("LOST")
                                                : (lockActive
                                                    ? (warmupLearningActive
                                                        ? QStringLiteral("WARMUP")
                                                        : QStringLiteral("LOCK"))
                                                    : QStringLiteral("SEARCH"));
            return;
        }

        const double candidatePeak = r.interpolatedPeakIndex;
        const PredictionGate gate = predict(p);

        // 初始SEARCH阶段仅对“整包中心落谷、靠同一波包邻域SNR恢复”的候选做两帧确认。
        // 普通中心SNR已过门的SEARCH帧保持原有逻辑，避免改变既有强信号启动行为。
        if (startupConfirmationEnabled && !lockActive && !everLocked) {
            if (pendingCount > 0) {
                if (confirmStartupPending(r, candidatePeak))
                    return;
                clearPending();
            }
            if (r.startupSupportRecovered) {
                startStartupPending(r, candidatePeak);
                return;
            }
        }

        // 受控低相关/低SNR边界恢复只补当前测量；状态机仍按原拒绝帧执行miss，
        // 因而既不覆盖模板，也不改变后续预测轨迹。
        if (r.guardedReadOnlyRecovered) {
            // 只读恢复不学习峰位或模板，但它已输出受严格门控的真实厚度，不能计作丢失。
            // 保留原预测链并清零连续miss，避免边界信号把稳定LOCK提前推入LOST。
            misses = 0;
            clearPending();
            clearBackupTemplatePending();
            r.trackingState = lockActive ? QStringLiteral("LOCK_GUARDED_READ_ONLY")
                                         : QStringLiteral("GUARDED_READ_ONLY");
            return;
        }

        // 预锁定低SNR恢复只补当前测量，不提前建立LOCK或覆盖模板。
        // 对状态机执行与原NO_SIGNAL完全相同的一次miss，保证后续正常帧结果不变。
        if (r.prelockReadOnlyRecovered) {
            miss(p);
            r.trackingState = QStringLiteral("SEARCH_PRELOCK_RECOVERED");
            return;
        }

        // LOCK特殊恢复的厚度已经由RECENT模板、预测残差和波包质量联合确认。
        // 它们只推进真实峰位/速度链，不学习异常波形模板；因此既保持原预测恢复节奏，
        // 又避免宽包、碎片包连续出现时污染RECENT身份或提前触发LOST冻结搜索。
        if (r.lockedBroadPacketRecovered || r.lockedFragmentedPacketRecovered
            || r.lockedStraddlingPacketRecovered || r.lockedAmbiguousReadOnlyRecovered
            || r.lockedTemplateOnlyRecovered) {
            appendAccepted(r, p);
            if (r.lockedBroadPacketRecovered)
                r.trackingState = lockActive ? QStringLiteral("LOCK_BROAD_RECOVERED")
                                             : QStringLiteral("BROAD_RECOVERED_LOST");
            else if (r.lockedFragmentedPacketRecovered)
                r.trackingState = lockActive ? QStringLiteral("LOCK_FRAGMENTED_RECOVERED")
                                             : QStringLiteral("FRAGMENTED_RECOVERED_LOST");
            else if (r.lockedStraddlingPacketRecovered)
                r.trackingState = lockActive ? QStringLiteral("LOCK_STRADDLING_RECOVERED")
                                             : QStringLiteral("STRADDLING_RECOVERED_LOST");
            else if (r.lockedAmbiguousReadOnlyRecovered)
                r.trackingState = lockActive ? QStringLiteral("LOCK_AMBIGUOUS_RECOVERED")
                                             : QStringLiteral("AMBIGUOUS_RECOVERED_LOST");
            else
                r.trackingState = lockActive ? QStringLiteral("LOCK_TEMPLATE_ONLY_RECOVERED")
                                             : QStringLiteral("TEMPLATE_ONLY_RECOVERED_LOST");
            return;
        }

        // EMA/SHAPE属于“未确认后备身份”，不能按普通VALID帧走状态机，否则即使不写历史，
        // 也会通过misses/pending时序间接改变RECENT身份。先在独立两帧确认通道处理。
        if (r.multiTemplateUsed
            && (r.trackingTemplateSource == QStringLiteral("EMA")
                || r.trackingTemplateSource == QStringLiteral("SHAPE"))) {
            clearPending();
            observeBackupRescue(r, p);
            return;
        }

        if (lockActive && std::isfinite(gate.center)) {
            r.trackingResidualSamples = candidatePeak - gate.center;
            const double residual = std::abs(r.trackingResidualSamples);
            // WARMUP扩大“发现范围”，但不取消位置身份确认。超过正常残差的候选仍走
            // 原两帧pending确认：连续真实位移可在下一帧确认并回填，单帧大跳变不能
            // 因为处于启动学习阶段就直接污染位置/速度历史。
            if (residual > p.trackingResidualAcceptSamples) {
                if (confirmPending(r, p, candidatePeak))
                    return;
                // 当前候选没有延续上一帧待确认分支：先把本帧作为新的pending。
                miss(p);
                startPending(r, p, candidatePeak,
                    residual > p.trackingResidualRejectSamples);
                return;
            }
        }

        if (reacquireRequired) {
            if (confirmPending(r, p, candidatePeak))
                return;

            // LOST冻结身份认证通过的高质量候选允许“只读输出”：
            // 当前帧保持VALID，但不更新模板、速度或有效峰历史；同时把它作为
            // REACQUIRE_PENDING的第一帧证据。下一帧位置一致时仍走原confirmPending
            // 正式重锁；不一致或中断则仅丢弃pending，不污染身份历史。
            const int frozenIdentityAgeFrames = frozenIdentityTime > 0
                ? clock - frozenIdentityTime : std::numeric_limits<int>::max();
            const bool identityReadOnlyCandidate =
                frozenIdentityAgeFrames <= 10
                && r.trackingState == QStringLiteral("LOST_IDENTITY_CANDIDATE")
                && r.candidateCount == 1
                && r.fitSucceeded && !r.fallbackPeakUsed
                && std::isfinite(r.fitR2)
                && r.fitR2 >= std::max(0.95, p.fitMinR2)
                && std::isfinite(r.snrDb)
                && r.snrDb >= p.minLocalSnrDb
                && std::isfinite(r.trackingCorrelation)
                && r.trackingCorrelation >= 0.92;
            if (identityReadOnlyCandidate) {
                pendingPeak = candidatePeak;
                pendingTime = clock;
                pendingCount = 1;
                pendingWaveform = r;
                hasPendingWaveform = true;
                pendingFrozenIdentityAuthenticated = true;
                r.reacquirePendingCount = 1;
                r.trackingState = QStringLiteral("REACQUIRE_READ_ONLY");
                r.error += QStringLiteral("；冻结长期身份认证通过，当前帧只读输出并等待下一帧重锁确认");
                return;
            }

            startPending(r, p, candidatePeak, false);
            return;
        }

        clearPending();
        appendAccepted(r, p);
        if (r.lowSnrRecovered)
            r.trackingState = QStringLiteral("LOW_SNR_RECOVERED");
        else if (r.trackingSoftRecovered)
            r.trackingState = QStringLiteral("SOFT_TRACK_RECOVERED");
        else
            r.trackingState = lockActive
                ? (warmupLearningActive ? QStringLiteral("WARMUP") : QStringLiteral("LOCK"))
                : QStringLiteral("SEARCH");
    }
};

void rejectWaveform(WaveformResult &r, const QString &reason,
                    const QString &message, bool noSignal = false)
{
    r.thicknessMm = NaN;
    r.peakIndex = -1;
    r.interpolatedPeakIndex = -1.0;
    r.peakTimeSec = NaN;
    r.peakVoltage = NaN;
    r.subsampleOffset = 0.0;
    r.measurementStatus = noSignal ? QStringLiteral("NO_SIGNAL")
                                  : QStringLiteral("INVALID");
    r.rejectionReason = reason;
    r.error = message;
}

// 以原始数据的一阶差分估计稳健噪声尺度。必须在带通处理前执行，
// 否则孤立尖峰的滤波振铃可能被误认成纵波。仅标记污染，不修改 raw。
const std::vector<bool> &detectRawImpulses(const std::vector<double> &raw,
                                           const TestParameters &p,
                                           WaveformResult &r,
                                           AlgorithmWorkspace &workspace)
{
    const int n = static_cast<int>(raw.size());

    // 100 Hz实时路径：Raw差分/MAD使用显式批次工作区。达到最大帧长后不再进行堆分配。
    // medianScratch只用于求全局差分中位数，避免原medianValue按值传参复制differences。
    auto &differences = workspace.rawDifferences;
    auto &deviations = workspace.rawDeviations;
    auto &medianScratch = workspace.rawMedianScratch;
    auto &localDeviations = workspace.rawLocalDeviations;
    auto &contaminated = workspace.rawContaminated;

    differences.clear();
    differences.reserve(std::max(0, n - 1));
    for (int i = 1; i < n; ++i)
        differences.push_back(raw[i] - raw[i - 1]);

    medianScratch.assign(differences.begin(), differences.end());
    const double center = medianValueInPlace(medianScratch);

    deviations.clear();
    deviations.reserve(differences.size());
    for (double value : differences)
        deviations.push_back(std::abs(value - center));

    // 全局MAD门只负责发现相对整帧背景很大的跳变。真实超声波包本身也可能
    // 达到几十倍MAD，因此不能再把“超过倍数”直接等同于电尖峰。
    r.rawDiffSigma = 1.4826 * medianValueInPlace(deviations);
    r.impulseThresholdV = std::max(p.impulseMinJumpV,
                                   p.impulseMadMultiplier * r.rawDiffSigma);
    const double scoreScale = std::max(r.rawDiffSigma,
                                      p.impulseMinJumpV / p.impulseMadMultiplier);
    const int supportRadius = std::clamp(
        std::max(4, p.energyWindowSamples / 2), 4, 32);
    const double supportThreshold = std::max(
        3.0 * r.rawDiffSigma, 0.25 * p.impulseMinJumpV);
    contaminated.assign(n, false);

    // 双向 FIR 会把一个Raw尖峰扩散成一段振铃，因此必须扩展污染范围。
    const int guard = p.filterOrder
        + std::max(p.structLen, p.energyWindowSamples / 2)
        + p.impulseGuardSamples;
    for (int i = 1; i < n; ++i) {
        const int differenceIndex = i - 1;
        const double jump = std::abs(differences[differenceIndex] - center);
        const int left = std::max(0, i - 1 - guard);
        const int right = std::min(n - 1, i + guard);
        const bool touchesSearch = right >= r.searchLeftIndex
            && left <= r.searchRightIndex;

        // impulseThreshold始终 >= impulseMinJumpV；因此 jump<=最小跳变时绝不可能
        // 成为孤立尖峰或超过全局门。搜索窗外也不需要其isolation诊断，直接跳过
        // 每点局部median，可消除绝大多数1599点的8样本nth_element。
        if (jump <= p.impulseMinJumpV && !touchesSearch)
            continue;

        const int supportLeft = std::max(0, differenceIndex - supportRadius);
        const int supportRight = std::min(n - 2, differenceIndex + supportRadius);

        // 邻域稳健尺度排除当前差分。单点尖峰即使在全局倍数设得很高时，
        // 相对邻域仍会非常突出；连续波包则有多个相邻差分共同支撑。
        localDeviations.clear();
        localDeviations.reserve(std::max(0, supportRight - supportLeft));
        int supportCount = 0;
        int firstSupport = -1;
        int lastSupport = -1;
        for (int j = supportLeft; j <= supportRight; ++j) {
            const double localJump = std::abs(differences[j] - center);
            if (j != differenceIndex)
                localDeviations.push_back(localJump);
            if (localJump > supportThreshold) {
                ++supportCount;
                if (firstSupport < 0)
                    firstSupport = j;
                lastSupport = j;
            }
        }
        const double localScale = std::max(
            r.rawDiffSigma, medianValueInPlace(localDeviations));
        const double isolationScore = jump / std::max(localScale, 1e-24);
        const bool exceedsGlobalGate = jump > r.impulseThresholdV;
        const bool locallyIsolated = jump > p.impulseMinJumpV
            && isolationScore > p.impulseIsolationRatio;
        // 至少三个显著差分并跨越四个采样间隔，才视为带限波包的连续边沿。
        const bool coherentWaveSupport = supportCount >= 3
            && firstSupport >= 0 && lastSupport - firstSupport >= 4;

        if (touchesSearch) {
            r.maxImpulseScore = std::max(r.maxImpulseScore, jump / scoreScale);
            r.maxImpulseIsolationScore = std::max(
                r.maxImpulseIsolationScore, isolationScore);
        }

        // 局部孤立尖峰始终拒绝；超过全局MAD门但具有连续波包支撑的边沿放行。
        if (!locallyIsolated && (!exceedsGlobalGate || coherentWaveSupport)) {
            if (exceedsGlobalGate && coherentWaveSupport)
                ++r.coherentRawJumpCount;
            continue;
        }
        ++r.impulseCount;
        std::fill(contaminated.begin() + left, contaminated.begin() + right + 1, true);
        if (touchesSearch && r.firstContaminatedIndex < 0)
            r.firstContaminatedIndex = i;
    }
    if (r.firstContaminatedIndex >= 0)
        rejectWaveform(r, QStringLiteral("IMPULSE_CONTAMINATED"),
            QStringLiteral("Raw孤立跳变污染搜索窗：差分点 #%1，全局 %2σ，局部孤立度 %3，禁止输出厚度")
                .arg(r.firstContaminatedIndex)
                .arg(r.maxImpulseScore, 0, 'f', 1)
                .arg(r.maxImpulseIsolationScore, 0, 'f', 1));
    return contaminated;
}

// 带通后的局部交流能量：减去窗口均值，避免残余直流/缓慢基线漂移抬高 SNR。
double localEnergy(const std::vector<double> &signal, int center, int halfWidth)
{
    if (center - halfWidth < 0
        || center + halfWidth >= static_cast<int>(signal.size()))
        return NaN;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = center - halfWidth; i <= center + halfWidth; ++i) {
        const double value = signal[i];
        sum += value;
        sumSquares += value * value;
    }
    const double count = static_cast<double>(2 * halfWidth + 1);
    // Var(x)=E[x²]-E[x]²，与原“先求均值再第二遍平方差”数学等价，
    // 把每次局部能量从两遍窗口扫描压成一遍。负的浮点舍入残差钳为0。
    const double mean = sum / count;
    return std::max(0.0, sumSquares / count - mean * mean);
}

double energySnrDb(double energy, double noiseEnergy)
{
    return 10.0 * std::log10(std::max(energy, 1e-24)
                            / std::max(noiseEnergy, 1e-24));
}

struct OriginalPeakResult
{
    bool found = false;
    int integerIndex = -1;
    double position = NaN;
    double voltage = NaN;
};

// 恢复最初的单峰寻峰方式：在当前有效窗内按 peak/valley/auto 选择滤波波形
// 的最强极值，再用相邻三点抛物线得到亚采样位置。只有调用方明确允许回退时使用。
OriginalPeakResult findOriginalSinglePeak(const std::vector<double> &signal,
                                          int left, int right,
                                          const QString &detectMode)
{
    OriginalPeakResult result;
    left = std::max(0, left);
    right = std::min(right, static_cast<int>(signal.size()) - 1);
    if (left > right)
        return result;

    double maximum = -std::numeric_limits<double>::infinity();
    double minimum = std::numeric_limits<double>::infinity();
    for (int i = left; i <= right; ++i) {
        maximum = std::max(maximum, signal[i]);
        minimum = std::min(minimum, signal[i]);
    }
    const double sign = detectMode == "valley" ? -1.0
        : detectMode == "peak" ? 1.0 : (maximum >= -minimum ? 1.0 : -1.0);

    int best = left;
    double bestMagnitude = sign * signal[left];
    for (int i = left + 1; i <= right; ++i) {
        const double magnitude = sign * signal[i];
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            best = i;
        }
    }
    if (!std::isfinite(bestMagnitude))
        return result;

    double delta = 0.0;
    if (best > left && best < right) {
        const double yLeft = sign * signal[best - 1];
        const double yCenter = sign * signal[best];
        const double yRight = sign * signal[best + 1];
        const double curvature = yLeft - 2.0 * yCenter + yRight;
        if (std::isfinite(curvature) && curvature < -1e-15)
            delta = std::clamp(0.5 * (yLeft - yRight) / curvature, -0.5, 0.5);
    }

    result.found = true;
    result.integerIndex = best;
    result.position = best + delta;
    if (best > left && best < right) {
        const double yLeft = signal[best - 1];
        const double yCenter = signal[best];
        const double yRight = signal[best + 1];
        result.voltage = yCenter + 0.5 * (yRight - yLeft) * delta
            + 0.5 * (yLeft - 2.0 * yCenter + yRight) * delta * delta;
    } else {
        result.voltage = signal[best];
    }
    return result;
}

bool estimateNoiseEnergy(const std::vector<double> &signal,
                         const std::vector<bool> &contaminated,
                         const TestParameters &p, WaveformResult &r,
                         const PredictionGate &history,
                         AlgorithmWorkspace &workspace)
{
    const int half = p.energyWindowSamples / 2;
    const int edge = p.filterOrder + half;

    // 100 Hz实时路径：Noise Floor统计使用显式批次工作区，避免每帧反复申请背景/MAD数组。
    auto &backgroundDb = workspace.noiseBackgroundDb;
    auto &medianScratch = workspace.noiseMedianScratch;
    auto &deviations = workspace.noiseDeviations;
    auto &acceptedDb = workspace.noiseAcceptedDb;
    backgroundDb.clear();

    // 背景噪声模型：
    // 1) 与信号使用同长度局部交流能量窗口；
    // 2) 排除理论搜索窗、Raw污染区和FIR边界；
    // 3) 能量转dB后用median + MAD只剔除“异常高能量”背景窗，避免后续回波/局部干扰抬高Noise Floor；
    // 4) 单帧鲁棒Noise Floor再用快升慢降EWMA做时间平滑。
    for (int center = edge; center + edge < static_cast<int>(signal.size());
         center += p.energyWindowSamples) {
        if (center + half >= r.searchLeftIndex - p.filterOrder
            && center - half <= r.searchRightIndex + p.filterOrder)
            continue;
        bool clean = true;
        for (int i = center - half; i <= center + half; ++i)
            clean = clean && !contaminated[i];
        if (!clean)
            continue;
        const double energy = localEnergy(signal, center, half);
        if (std::isfinite(energy))
            backgroundDb.push_back(10.0 * std::log10(std::max(energy, 1e-24)));
    }

    r.backgroundNoiseWindowCount = static_cast<int>(backgroundDb.size());
    if (backgroundDb.size() < 4) {
        rejectWaveform(r, QStringLiteral("INSUFFICIENT_NOISE"),
            QStringLiteral("干净背景能量窗口不足4个，无法可靠估计SNR"));
        return false;
    }

    medianScratch.assign(backgroundDb.begin(), backgroundDb.end());
    const double medianDb = medianValueInPlace(medianScratch);
    deviations.clear();
    deviations.reserve(backgroundDb.size());
    for (double value : backgroundDb)
        deviations.push_back(std::abs(value - medianDb));
    const double robustSigmaDb = 1.4826 * medianValueInPlace(deviations);
    // MAD极小时给0.25 dB数值保护，避免几乎相同的背景窗口因浮点微差被大量误删。
    const double rejectScaleDb = std::max(0.25, robustSigmaDb);
    const double highRejectDb = medianDb + p.noiseMadRejectMultiplier * rejectScaleDb;

    acceptedDb.clear();
    acceptedDb.reserve(backgroundDb.size());
    for (double value : backgroundDb) {
        if (value <= highRejectDb)
            acceptedDb.push_back(value);
        else
            ++r.backgroundNoiseRejectedCount;
    }
    if (acceptedDb.size() < 4) {
        rejectWaveform(r, QStringLiteral("INSUFFICIENT_NOISE"),
            QStringLiteral("dB-MAD剔除异常高能量背景后，有效噪声窗口不足4个"));
        return false;
    }

    r.backgroundNoiseMedianDb = medianDb;
    r.backgroundNoiseMadDb = robustSigmaDb;
    // acceptedDb之后不再需要保持原顺序，可直接原地nth_element，避免再次复制。
    r.frameNoiseFloorDb = medianValueInPlace(acceptedDb);

    if (std::isfinite(history.temporalNoiseFloorDb)) {
        const bool rising = r.frameNoiseFloorDb > history.temporalNoiseFloorDb;
        r.noiseEwmaAlphaUsed = rising ? p.noiseEwmaAlphaUp : p.noiseEwmaAlphaDown;
        r.temporalNoiseFloorDb = (1.0 - r.noiseEwmaAlphaUsed) * history.temporalNoiseFloorDb
                               + r.noiseEwmaAlphaUsed * r.frameNoiseFloorDb;
    } else {
        r.noiseEwmaAlphaUsed = 1.0;
        r.temporalNoiseFloorDb = r.frameNoiseFloorDb;
    }

    r.noiseEnergy = std::pow(10.0, r.temporalNoiseFloorDb / 10.0);
    return std::isfinite(r.noiseEnergy) && r.noiseEnergy > 0.0;
}

QVariant finiteVariant(double value)
{
    return std::isfinite(value) ? QVariant(value) : QVariant();
}

double clampQuality01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

QString qualityNumber(double value, int precision = 2)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision)
                                : QStringLiteral("--");
}

// 将算法路径和各阶段证据统一映射为质量等级、0~100规则质量分，以及拒绝阶段/阈值说明。
// confidenceScore只是工程质量分，不表示统计学概率；它用于在上位机中快速区分直接锁定、
// 软恢复、宽包恢复、延迟回填等不同来源的数据点。
void evaluateWaveformQuality(WaveformResult &r, const TestParameters &p)
{
    r.rejectionStage.clear();
    r.rejectionThresholdDetail.clear();

    if (r.measurementStatus == QStringLiteral("VALID") && std::isfinite(r.thicknessMm)) {
        double confidenceCap = 100.0;
        if (r.delayedConfirmationBackfilled) {
            r.qualityLevel = QStringLiteral("VALID_BACKFILLED");
            r.qualityLabel = QStringLiteral("延迟确认回填");
            confidenceCap = 90.0;
        } else if (r.lowSnrRecovered) {
            r.qualityLevel = QStringLiteral("VALID_LOW_SNR_RECOVERED");
            r.qualityLabel = QStringLiteral("低SNR软恢复");
            confidenceCap = 82.0;
        } else if (r.lockedBroadPacketRecovered) {
            r.qualityLevel = QStringLiteral("VALID_BROAD_RECOVERED");
            r.qualityLabel = QStringLiteral("LOCK宽包恢复");
            confidenceCap = 90.0;
        } else if (r.lockedFragmentedPacketRecovered) {
            r.qualityLevel = QStringLiteral("VALID_FRAGMENTED_RECOVERED");
            r.qualityLabel = QStringLiteral("LOCK碎片波包恢复");
            confidenceCap = 90.0;
        } else if (r.lockedStraddlingPacketRecovered) {
            r.qualityLevel = QStringLiteral("VALID_STRADDLING_RECOVERED");
            r.qualityLabel = QStringLiteral("LOCK双瓣合并恢复");
            confidenceCap = 92.0;
        } else if (r.lockedTemplateOnlyRecovered) {
            r.qualityLevel = QStringLiteral("VALID_TEMPLATE_ONLY_RECOVERED");
            r.qualityLabel = QStringLiteral("LOCK模板直接恢复");
            confidenceCap = 90.0;
        } else if (r.trackingSoftRecovered) {
            r.qualityLevel = QStringLiteral("VALID_SOFT_RECOVERED");
            r.qualityLabel = QStringLiteral("低相关软恢复");
            confidenceCap = 85.0;
        } else if (r.trackingState == QStringLiteral("RELOCKED")) {
            r.qualityLevel = QStringLiteral("VALID_RELOCKED");
            r.qualityLabel = QStringLiteral("重捕获确认");
            confidenceCap = 92.0;
        } else if (r.multiTemplateUsed && r.trackingTemplateSource == QStringLiteral("CONFIRMED")) {
            r.qualityLevel = QStringLiteral("VALID_CONFIRMED_TEMPLATE");
            r.qualityLabel = QStringLiteral("确认形态模板");
            confidenceCap = 95.0;
        } else if (r.multiTemplateUsed && r.trackingState == QStringLiteral("BACKUP_CONFIRMED")) {
            r.qualityLevel = QStringLiteral("VALID_BACKUP_CONFIRMED");
            r.qualityLabel = QStringLiteral("后备模板两帧确认");
            confidenceCap = 92.0;
        } else if (r.multiTemplateUsed && r.trackingTemplateSource == QStringLiteral("EMA")) {
            r.qualityLevel = QStringLiteral("VALID_EMA_TEMPLATE");
            r.qualityLabel = r.trackingState == QStringLiteral("BACKUP_TENTATIVE")
                ? QStringLiteral("EMA后备候选") : QStringLiteral("EMA模板跟踪");
            confidenceCap = r.trackingState == QStringLiteral("BACKUP_TENTATIVE") ? 86.0 : 95.0;
        } else if (r.multiTemplateUsed && r.trackingTemplateSource == QStringLiteral("SHAPE")) {
            r.qualityLevel = QStringLiteral("VALID_SHAPE_TEMPLATE");
            r.qualityLabel = r.trackingState == QStringLiteral("BACKUP_TENTATIVE")
                ? QStringLiteral("形态后备候选") : QStringLiteral("形态模板跟踪");
            confidenceCap = r.trackingState == QStringLiteral("BACKUP_TENTATIVE") ? 86.0 : 93.0;
        } else if (r.fallbackPeakUsed) {
            r.qualityLevel = QStringLiteral("VALID_FALLBACK");
            r.qualityLabel = QStringLiteral("单峰回退");
            confidenceCap = 80.0;
        } else if (r.searchWindowStage == QStringLiteral("WARMUP")) {
            r.qualityLevel = QStringLiteral("VALID_WARMUP");
            r.qualityLabel = QStringLiteral("启动宽窗学习");
            confidenceCap = 90.0;
        } else if (r.trackingState == QStringLiteral("LOCK")) {
            r.qualityLevel = QStringLiteral("VALID_LOCKED");
            r.qualityLabel = QStringLiteral("直接锁定");
        } else {
            r.qualityLevel = QStringLiteral("VALID_SEARCH");
            r.qualityLabel = QStringLiteral("初始捕获");
            confidenceCap = 75.0;
        }

        const double snrEvidence = std::isfinite(r.snrDb)
            ? clampQuality01((r.snrDb - p.minLocalSnrDb) / 10.0) : 0.5;
        const double fitEvidence = r.fallbackPeakUsed ? 0.55
            : ((r.lockedFragmentedPacketRecovered || r.lockedTemplateOnlyRecovered)
                    && !r.fitSucceeded ? 0.80
                : (std::isfinite(r.fitR2)
                    ? clampQuality01((r.fitR2 - p.fitMinR2)
                                     / std::max(1e-6, 1.0 - p.fitMinR2))
                    : 0.55));
        const double trackingEvidence = std::isfinite(r.trackingCorrelation)
            ? clampQuality01((r.trackingCorrelation - p.trackingSoftCorrelation)
                             / std::max(1e-6, 1.0 - p.trackingSoftCorrelation))
            : (r.trackingState == QStringLiteral("SEARCH") ? 0.60 : 0.50);
        const double residualEvidence = std::isfinite(r.trackingResidualSamples)
            ? clampQuality01(1.0 - std::abs(r.trackingResidualSamples)
                             / std::max(1.0, p.trackingResidualRejectSamples))
            : 0.75;

        const double rawScore = 100.0 * (0.30 * snrEvidence
                                        + 0.25 * fitEvidence
                                        + 0.30 * trackingEvidence
                                        + 0.15 * residualEvidence);
        double widthPenalty = 0.0;
        if (r.adaptiveWidthState == QStringLiteral("WIDTH_TRANSITION"))
            widthPenalty = 8.0;
        else if (r.adaptiveWidthState == QStringLiteral("WIDTH_OUTLIER"))
            widthPenalty = 18.0;
        r.confidenceScore = std::clamp(rawScore - widthPenalty, 0.0, confidenceCap);
        r.confidenceBasis = QStringLiteral("路径=%1；模板=%2；SNR=%3 dB；R²=%4；相关=%5；残差=%6 点；脉宽=%7（中值%8，容差±%9，状态%10）")
            .arg(r.qualityLabel)
            .arg(r.trackingTemplateSource)
            .arg(qualityNumber(r.snrDb, 1))
            .arg(qualityNumber(r.fitR2, 4))
            .arg(qualityNumber(r.trackingCorrelation, 3))
            .arg(qualityNumber(r.trackingResidualSamples, 2))
            .arg(qualityNumber(r.fitPeakWidthSamples, 2))
            .arg(qualityNumber(r.adaptiveWidthMedianSamples, 2))
            .arg(qualityNumber(r.adaptiveWidthToleranceSamples, 2))
            .arg(r.adaptiveWidthState);
        return;
    }

    r.qualityLevel = QStringLiteral("REJECTED");
    r.qualityLabel = r.measurementStatus == QStringLiteral("NO_SIGNAL")
        ? QStringLiteral("无信号") : QStringLiteral("已剔除");
    r.confidenceScore = 0.0;

    const QString reason = r.rejectionReason;
    if (reason == QStringLiteral("INVALID_DATA")) {
        r.rejectionStage = QStringLiteral("输入数据");
        r.rejectionThresholdDetail = QStringLiteral("原始数据为空、过短或含非有限值，未进入寻峰");
    } else if (reason == QStringLiteral("INVALID_PARAMETERS")) {
        r.rejectionStage = QStringLiteral("参数检查");
        r.rejectionThresholdDetail = QStringLiteral("采样率/带通/参考厚度/窗口参数未通过合法性检查");
    } else if (reason == QStringLiteral("INVALID_WINDOW")) {
        r.rejectionStage = QStringLiteral("理论搜索窗");
        r.rejectionThresholdDetail = QStringLiteral("理论搜索窗超出当前波形有效范围");
    } else if (reason == QStringLiteral("IMPULSE_CONTAMINATED")) {
        r.rejectionStage = QStringLiteral("Raw尖峰污染");
        r.rejectionThresholdDetail = QStringLiteral("最大Raw差分=%1σ，孤立度=%2；门限：MAD倍数%3、最小跳变%4 V、孤立度%5")
            .arg(qualityNumber(r.maxImpulseScore, 1))
            .arg(qualityNumber(r.maxImpulseIsolationScore, 1))
            .arg(qualityNumber(p.impulseMadMultiplier, 1))
            .arg(qualityNumber(p.impulseMinJumpV, 3))
            .arg(qualityNumber(p.impulseIsolationRatio, 1));
    } else if (reason == QStringLiteral("INSUFFICIENT_NOISE")) {
        r.rejectionStage = QStringLiteral("背景噪声估计");
        r.rejectionThresholdDetail = QStringLiteral("干净背景能量窗口不足4个，无法建立可靠SNR基线");
    } else if (reason == QStringLiteral("NO_SIGNAL")) {
        r.rejectionStage = QStringLiteral("信号存在性/SNR");
        const double observed = std::isfinite(r.snrDb) ? r.snrDb : r.maxLocalSnrDb;
        if (r.lowSnrSoftCandidate) {
            r.rejectionThresholdDetail = QStringLiteral("局部SNR=%1 dB处于软恢复区[%2,%3)；稳定LOCK下还需相关≥%4、残差≤%5点、唯一候选且拟合R²≥%6")
                .arg(qualityNumber(observed, 2))
                .arg(qualityNumber(p.lowSnrSoftMinDb, 2))
                .arg(qualityNumber(p.minLocalSnrDb, 2))
                .arg(qualityNumber(p.lowSnrSoftMinTrackingCorrelation, 2))
                .arg(qualityNumber(p.lowSnrSoftMaxResidualSamples, 1))
                .arg(qualityNumber(p.lowSnrSoftMinFitR2, 2));
        } else {
            r.rejectionThresholdDetail = QStringLiteral("局部SNR=%1 dB；普通路径要求 ≥ %2 dB；稳定LOCK软恢复下限=%3 dB")
                .arg(qualityNumber(observed, 2))
                .arg(qualityNumber(p.minLocalSnrDb, 2))
                .arg(qualityNumber(p.lowSnrSoftMinDb, 2));
        }
    } else if (reason == QStringLiteral("PACKET_SHAPE_INVALID")) {
        r.rejectionStage = QStringLiteral("波包形态/宽度");
        r.rejectionThresholdDetail = QStringLiteral("分割核心宽=%1 点；常规允许 %2~%3 点，LOCK宽包最多%4点且需额外相关验证")
            .arg(r.predictionGateCoreWidthSamples)
            .arg(p.packetMinWidth).arg(p.packetMaxWidth).arg(p.lockedPacketMaxCoreWidth);
    } else if (reason == QStringLiteral("PREDICTION_GATE")) {
        r.rejectionStage = QStringLiteral("历史预测门");
        r.rejectionThresholdDetail = QStringLiteral("预测峰=%1，允许窗=[%2,%3]；门内没有通过波包/峰位条件的候选")
            .arg(qualityNumber(r.predictedPeakIndex, 2))
            .arg(r.predictionLeftIndex).arg(r.predictionRightIndex);
    } else if (reason == QStringLiteral("AMBIGUOUS_PACKETS")) {
        r.rejectionStage = QStringLiteral("候选唯一性");
        r.rejectionThresholdDetail = QStringLiteral("第一/第二候选显著性比=%1；要求 ≥ %2")
            .arg(qualityNumber(r.candidateRatio, 3)).arg(qualityNumber(p.minCandidateRatio, 3));
    } else if (reason == QStringLiteral("PACKET_FIT_FAILED")) {
        r.rejectionStage = QStringLiteral("整包拟合");
        r.rejectionThresholdDetail = QStringLiteral("拟合R²=%1；要求 ≥ %2")
            .arg(qualityNumber(r.fitR2, 4)).arg(qualityNumber(p.fitMinR2, 4));
    } else if (reason == QStringLiteral("PEAK_NOT_FOUND")) {
        r.rejectionStage = QStringLiteral("单峰回退寻峰");
        r.rejectionThresholdDetail = QStringLiteral("理论窗截断后，传统单峰回退仍未找到可用极值");
    } else if (reason == QStringLiteral("TRACKING_MISMATCH")) {
        r.rejectionStage = QStringLiteral("连续模板跟踪");
        r.rejectionThresholdDetail = QStringLiteral("模板相关=%1；硬门 ≥ %2；软恢复下限%3且还需位置/R²/SNR同时通过")
            .arg(qualityNumber(r.trackingCorrelation, 3))
            .arg(qualityNumber(p.trackingMinCorrelation, 3))
            .arg(qualityNumber(p.trackingSoftCorrelation, 3));
    } else if (reason == QStringLiteral("BROAD_PACKET_TRACKING_MISMATCH")) {
        r.rejectionStage = QStringLiteral("LOCK宽包恢复");
        r.rejectionThresholdDetail = QStringLiteral("宽核心=%1点；需相关≥%2、跟踪残差≤%3点、能量峰/ToF距离≤%4点")
            .arg(r.predictionGateCoreWidthSamples)
            .arg(qualityNumber(p.lockedBroadMinTrackingCorrelation, 2))
            .arg(qualityNumber(p.trackingResidualAcceptSamples, 1))
            .arg(qualityNumber(p.lockedBroadEnergyPeakToleranceSamples, 1));
    } else if (reason == QStringLiteral("TRACKING_JUMP")) {
        r.rejectionStage = QStringLiteral("位置连续性");
        r.rejectionThresholdDetail = QStringLiteral("预测残差=%1点；严重跳变门=%2点，需连续%3帧且相邻候选差≤%4点确认")
            .arg(qualityNumber(std::abs(r.trackingResidualSamples), 2))
            .arg(qualityNumber(p.trackingResidualRejectSamples, 1))
            .arg(p.reacquireConfirmFrames)
            .arg(qualityNumber(p.reacquireConsistencySamples, 1));
    } else if (reason == QStringLiteral("REACQUIRE_PENDING")) {
        r.rejectionStage = QStringLiteral("重捕获确认");
        r.rejectionThresholdDetail = QStringLiteral("当前确认计数=%1/%2；连续候选位置差必须 ≤ %3点")
            .arg(r.reacquirePendingCount).arg(p.reacquireConfirmFrames)
            .arg(qualityNumber(p.reacquireConsistencySamples, 1));
    } else if (reason == QStringLiteral("INVALID_THICKNESS")) {
        r.rejectionStage = QStringLiteral("厚度换算");
        r.rejectionThresholdDetail = QStringLiteral("峰位已找到，但ToF无法换算为有限厚度；厚度公差不参与此判定");
    } else {
        r.rejectionStage = QStringLiteral("算法有效性");
        r.rejectionThresholdDetail = r.error.isEmpty() ? reason : r.error;
    }
    r.confidenceBasis = r.rejectionThresholdDetail;
}

QVariantMap waveformDiagnostics(const WaveformResult &r)
{
    return {
        {"measurementStatus", r.measurementStatus}, {"rejectionReason", r.rejectionReason},
        {"qualityLevel", r.qualityLevel}, {"qualityLabel", r.qualityLabel},
        {"confidenceScore", finiteVariant(r.confidenceScore)},
        {"confidenceBasis", r.confidenceBasis},
        {"rejectionStage", r.rejectionStage},
        {"rejectionThresholdDetail", r.rejectionThresholdDetail},
        {"rawDiffSigma", finiteVariant(r.rawDiffSigma)},
        {"impulseThresholdV", finiteVariant(r.impulseThresholdV)},
        {"maxImpulseScore", r.maxImpulseScore},
        {"maxImpulseIsolationScore", r.maxImpulseIsolationScore},
        {"coherentRawJumpCount", r.coherentRawJumpCount},
        {"impulseCount", r.impulseCount},
        {"firstContaminatedIndex", r.firstContaminatedIndex},
        {"noiseEnergy", finiteVariant(r.noiseEnergy)},
        {"backgroundNoiseWindowCount", r.backgroundNoiseWindowCount},
        {"backgroundNoiseRejectedCount", r.backgroundNoiseRejectedCount},
        {"backgroundNoiseMedianDb", finiteVariant(r.backgroundNoiseMedianDb)},
        {"backgroundNoiseMadDb", finiteVariant(r.backgroundNoiseMadDb)},
        {"frameNoiseFloorDb", finiteVariant(r.frameNoiseFloorDb)},
        {"temporalNoiseFloorDb", finiteVariant(r.temporalNoiseFloorDb)},
        {"noiseEwmaAlphaUsed", finiteVariant(r.noiseEwmaAlphaUsed)},
        {"localSignalEnergy", finiteVariant(r.localSignalEnergy)},
        {"maxLocalSnrDb", finiteVariant(r.maxLocalSnrDb)},
        {"lowSnrSoftCandidate", r.lowSnrSoftCandidate},
        {"lowSnrRecovered", r.lowSnrRecovered},
        {"lowSnrTrackingResidualSamples", finiteVariant(r.lowSnrTrackingResidualSamples)},
        {"predictionGateMaxLocalSnrDb", finiteVariant(r.predictionGateMaxLocalSnrDb)},
        {"predictionGateMaxIndex", r.predictionGateMaxIndex},
        {"predictedCenterLocalSnrDb", finiteVariant(r.predictedCenterLocalSnrDb)},
        {"segmentationReferenceSnrDb", finiteVariant(r.segmentationReferenceSnrDb)},
        {"segmentationReferenceIndex", r.segmentationReferenceIndex},
        {"predictionGateCoreLeftIndex", r.predictionGateCoreLeftIndex},
        {"predictionGateCoreRightIndex", r.predictionGateCoreRightIndex},
        {"predictionGateCoreWidthSamples", r.predictionGateCoreWidthSamples},
        {"snrDb", finiteVariant(r.snrDb)},
        {"firstSignificance", finiteVariant(r.firstSignificance)},
        {"secondSignificance", finiteVariant(r.secondSignificance)},
        {"candidateRatio", finiteVariant(r.candidateRatio)},
        {"firstCandidateIndex", r.firstCandidateIndex},
        {"secondCandidateIndex", r.secondCandidateIndex}, {"candidateCount", r.candidateCount},
        {"predictionApplied", r.predictionApplied},
        {"predictedPeakIndex", finiteVariant(r.predictedPeakIndex)},
        {"predictionHistoryCount", r.predictionHistoryCount},
        {"predictionLeftIndex", r.predictionLeftIndex}, {"predictionRightIndex", r.predictionRightIndex},
        {"searchWindowStage", r.searchWindowStage},
        {"longitudinalStatus", r.longitudinalStatus}, {"longitudinalCandidate", r.longitudinalCandidate},
        {"longitudinalEvidence", r.longitudinalEvidence}, {"echoConfirmed", r.echoConfirmed},
        {"echoCorrelation", finiteVariant(r.echoCorrelation)}, {"echoSnrDb", finiteVariant(r.echoSnrDb)},
        {"echoEnergyRatio", finiteVariant(r.echoEnergyRatio)},
        {"echoDelaySamples", finiteVariant(r.echoDelaySamples)},
        {"expectedEchoDelaySamples", finiteVariant(r.expectedEchoDelaySamples)},
        {"packetLeftIndex", r.packetLeftIndex}, {"packetRightIndex", r.packetRightIndex},
        {"fitSucceeded", r.fitSucceeded}, {"fitR2", finiteVariant(r.fitR2)},
        {"fitResidualRms", finiteVariant(r.fitResidualRms)},
        {"fitSinglePeakIndex", finiteVariant(r.fitSinglePeakIndex)},
        {"packetCenterIndex", finiteVariant(r.packetCenterIndex)},
        {"fitPeakWidthSamples", finiteVariant(r.fitPeakWidthSamples)},
        {"adaptiveWidthState", r.adaptiveWidthState},
        {"adaptiveWidthHistoryCount", r.adaptiveWidthHistoryCount},
        {"adaptiveWidthMedianSamples", finiteVariant(r.adaptiveWidthMedianSamples)},
        {"adaptiveWidthMadSamples", finiteVariant(r.adaptiveWidthMadSamples)},
        {"adaptiveWidthToleranceSamples", finiteVariant(r.adaptiveWidthToleranceSamples)},
        {"adaptiveWidthDeviationSamples", finiteVariant(r.adaptiveWidthDeviationSamples)},
        {"adaptiveWidthNormalizedDeviation", finiteVariant(r.adaptiveWidthNormalizedDeviation)},
        {"fallbackPeakUsed", r.fallbackPeakUsed}, {"fallbackReason", r.fallbackReason},
        {"trackingCorrelation", finiteVariant(r.trackingCorrelation)},
        {"trackingCorrelationMargin", finiteVariant(r.trackingCorrelationMargin)},
        {"trackedPeakPrediction", finiteVariant(r.trackedPeakPrediction)},
        {"trackingResidualSamples", finiteVariant(r.trackingResidualSamples)},
        {"trackingState", r.trackingState},
        {"reacquirePendingCount", r.reacquirePendingCount},
        {"templateTrackingUsed", r.templateTrackingUsed},
        {"trackingSoftRecovered", r.trackingSoftRecovered},
        {"trackingFitPredictionResidualSamples", finiteVariant(r.trackingFitPredictionResidualSamples)},
        {"trackingTemplateSource", r.trackingTemplateSource},
        {"recentTemplateCorrelation", finiteVariant(r.recentTemplateCorrelation)},
        {"confirmedTemplateCorrelation", finiteVariant(r.confirmedTemplateCorrelation)},
        {"emaTemplateCorrelation", finiteVariant(r.emaTemplateCorrelation)},
        {"shapeTemplateCorrelation", finiteVariant(r.shapeTemplateCorrelation)},
        {"recentTemplatePeak", finiteVariant(r.recentTemplatePeak)},
        {"confirmedTemplatePeak", finiteVariant(r.confirmedTemplatePeak)},
        {"emaTemplatePeak", finiteVariant(r.emaTemplatePeak)},
        {"shapeTemplatePeak", finiteVariant(r.shapeTemplatePeak)},
        {"multiTemplateHardMatchCount", r.multiTemplateHardMatchCount},
        {"multiTemplateAgreementSpreadSamples", finiteVariant(r.multiTemplateAgreementSpreadSamples)},
        {"multiTemplateUsed", r.multiTemplateUsed},
        {"multiTemplateBackupPendingCount", r.multiTemplateBackupPendingCount},
        {"multiTemplateBackupConfirmed", r.multiTemplateBackupConfirmed},
        {"lockedBroadPacketCandidate", r.lockedBroadPacketCandidate},
        {"lockedBroadPacketRecovered", r.lockedBroadPacketRecovered},
        {"lockedBroadEnergyPeakDistanceSamples", finiteVariant(r.lockedBroadEnergyPeakDistanceSamples)},
        {"lockedFragmentedPacketCandidate", r.lockedFragmentedPacketCandidate},
        {"lockedFragmentedPacketRecovered", r.lockedFragmentedPacketRecovered},
        {"lockedFragmentedCoreWidthSamples", r.lockedFragmentedCoreWidthSamples},
        {"lockedFragmentedSupportWidthSamples", r.lockedFragmentedSupportWidthSamples},
        {"lockedFragmentedSupportThresholdSnrDb", finiteVariant(r.lockedFragmentedSupportThresholdSnrDb)},
        {"lockedFragmentedPredictionResidualSamples", finiteVariant(r.lockedFragmentedPredictionResidualSamples)},
        {"lockedStraddlingPacketCandidate", r.lockedStraddlingPacketCandidate},
        {"lockedStraddlingPacketRecovered", r.lockedStraddlingPacketRecovered},
        {"lockedTemplateOnlyCandidate", r.lockedTemplateOnlyCandidate},
        {"lockedTemplateOnlyRecovered", r.lockedTemplateOnlyRecovered},
        {"lockRecoveryNeedsConfirmation", r.lockRecoveryNeedsConfirmation},
        {"startupSupportRecovered", r.startupSupportRecovered},
        {"startupSupportSnrDb", finiteVariant(r.startupSupportSnrDb)},
        {"startupPendingCount", r.startupPendingCount},
        {"delayedConfirmationBackfilled", r.delayedConfirmationBackfilled},
        {"delayedConfirmationDelayFrames", r.delayedConfirmationDelayFrames},
        {"trackingFitFallback", r.trackingFitFallback},
        {"_fittedPoints", r.fittedPoints}
    };
}

// 与detrendSignal保持完全相同的计算顺序，但直接作用于连续工作区。
void detrendCorrelationBuffer(double *signal, int n)
{
    if (n < 2)
        return;
    const double mean_x = (n - 1) / 2.0;
    double mean_y = 0.0;
    for (int i = 0; i < n; ++i)
        mean_y += signal[i];
    mean_y /= n;

    double numerator = 0.0, denominator = 0.0;
    for (int i = 0; i < n; ++i) {
        const double dx = i - mean_x;
        const double dy = signal[i] - mean_y;
        numerator += dx * dy;
        denominator += dx * dx;
    }
    const double slope = denominator != 0.0 ? numerator / denominator : 0.0;
    const double intercept = mean_y - slope * mean_x;
    for (int i = 0; i < n; ++i)
        signal[i] -= slope * i + intercept;
}

// 对完整波包去均值/线性趋势后归一化相关。正常工业窗口最大远小于512点，
// 因此使用栈工作区消除滑窗每个center/lag的临时vector分配；超长输入保留原vector回退。
double packetCorrelation(const double *reference, const double *target, size_t count)
{
    if (!reference || !target || count < 8)
        return NaN;

    constexpr size_t STACK_CORRELATION_CAPACITY = 512;
    // 下面只读取 copy_n 写入的 [0,count) 区间，无需对整块栈数组做值初始化。
    // Debug 下每次相关调用清零 2×512 doubles 会放大运行时与栈压力。
    std::array<double, STACK_CORRELATION_CAPACITY> referenceStack;
    std::array<double, STACK_CORRELATION_CAPACITY> targetStack;
    std::vector<double> referenceHeap;
    std::vector<double> targetHeap;
    double *referenceWork = nullptr;
    double *targetWork = nullptr;

    if (count <= STACK_CORRELATION_CAPACITY) {
        referenceWork = referenceStack.data();
        targetWork = targetStack.data();
        std::copy_n(reference, count, referenceWork);
        std::copy_n(target, count, targetWork);
    } else {
        referenceHeap.assign(reference, reference + count);
        targetHeap.assign(target, target + count);
        referenceWork = referenceHeap.data();
        targetWork = targetHeap.data();
    }

    detrendCorrelationBuffer(referenceWork, static_cast<int>(count));
    detrendCorrelationBuffer(targetWork, static_cast<int>(count));
    double dot = 0.0, energyA = 0.0, energyB = 0.0;
    for (size_t i = 0; i < count; ++i) {
        dot += referenceWork[i] * targetWork[i];
        energyA += referenceWork[i] * referenceWork[i];
        energyB += targetWork[i] * targetWork[i];
    }
    return energyA > 1e-24 && energyB > 1e-24
        ? dot / std::sqrt(energyA * energyB) : NaN;
}

struct TemplateQuickMatch
{
    QString source;
    double correlation = NaN;
    double peak = NaN;
    double margin = NaN;
};

TemplateQuickMatch matchTrackingTemplate(const std::vector<double> &signal,
                                         const std::vector<double> &templ,
                                         double anchorFraction,
                                         int centerLeft, int centerRight,
                                         const QString &source)
{
    TemplateQuickMatch out;
    out.source = source;
    if (templ.empty())
        return out;
    const int n = static_cast<int>(signal.size());
    const int templateHalf = static_cast<int>(templ.size()) / 2;
    const int left = std::max(centerLeft, templateHalf);
    const int right = std::min(centerRight, n - 1 - templateHalf);
    if (left > right)
        return out;

    // 只读取 correlationCount 范围内已写入的元素，避免Debug下每次初始化2048项。
    std::array<std::pair<int, double>, 2048> correlations;
    int correlationCount = 0;
    for (int center = left; center <= right; ++center) {
        const double rho = packetCorrelation(
            templ.data(), signal.data() + center - templateHalf, templ.size());
        if (correlationCount >= static_cast<int>(correlations.size()))
            return out; // 1600点工业帧不会到达该保护分支。
        correlations[correlationCount++] = {center, std::isfinite(rho) ? rho : -1.0};
    }
    if (correlationCount == 0)
        return out;

    const auto begin = correlations.begin();
    const auto end = begin + correlationCount;
    const auto best = std::max_element(begin, end,
        [](const auto &a, const auto &b) { return a.second < b.second; });
    out.correlation = best->second;

    double correlationDelta = 0.0;
    const int bestIndex = static_cast<int>(std::distance(begin, best));
    if (bestIndex > 0 && bestIndex + 1 < correlationCount) {
        const double yLeft = correlations[bestIndex - 1].second;
        const double yCenter = correlations[bestIndex].second;
        const double yRight = correlations[bestIndex + 1].second;
        const double curvature = yLeft - 2.0 * yCenter + yRight;
        if (std::isfinite(curvature) && curvature < -1e-12)
            correlationDelta = std::clamp(
                0.5 * (yLeft - yRight) / curvature, -0.5, 0.5);
    }
    out.peak = best->first + correlationDelta + anchorFraction;

    double competitor = 0.0;
    for (int i = 1; i + 1 < correlationCount; ++i) {
        if (std::abs(correlations[i].first - best->first) >= 4
            && correlations[i].second >= correlations[i - 1].second
            && correlations[i].second >= correlations[i + 1].second)
            competitor = std::max(competitor, correlations[i].second);
    }
    out.margin = out.correlation - competitor;
    return out;
}

WaveformResult detectLongitudinalPacket(
    const std::vector<double> &signal, const std::vector<bool> &contaminated,
    const TestParameters &p, WaveformResult result, const PredictionGate &history,
    AlgorithmWorkspace &workspace)
{
    result.adaptiveWidthHistoryCount = history.adaptiveWidthHistoryCount;
    result.adaptiveWidthMedianSamples = history.adaptiveWidthMedianSamples;
    result.adaptiveWidthMadSamples = history.adaptiveWidthMadSamples;
    result.adaptiveWidthToleranceSamples = history.adaptiveWidthToleranceSamples;
    if (!history.adaptiveWidthReady && history.adaptiveWidthHistoryCount > 0)
        result.adaptiveWidthState = QStringLiteral("WIDTH_LEARNING");

    struct Packet {
        int left = 0, right = 0, strongest = 0;
        bool clippedBySearchWindow = false;
        bool lockedBroadContinuation = false;
        bool lockedFragmentedRecoveryCandidate = false;
        int fragmentedCoreWidthSamples = 0;
        int fragmentedSupportWidthSamples = 0;
        double fragmentedSupportThresholdSnrDb = NaN;
        double score = 0.0, primaryEnergy = 0.0, echoRho = 0.0, echoSnrDb = NaN;
        double echoEnergyRatio = NaN, echoLag = NaN, expectedLag = NaN;
    };
    const int n = static_cast<int>(signal.size());
    const int half = p.energyWindowSamples / 2;
    const int left = std::max(result.searchLeftIndex, half);
    const int right = std::min(result.searchRightIndex, n - 1 - half);

    std::vector<double> energies(n, 0.0);
    std::vector<Packet> packets;
    std::vector<Packet> eligible;
    // 碎片恢复绝不参与普通候选竞争。只有正常eligible为空时，才把预测门内
    // 4~7点高阈值seed作为后置救援候选，避免新增AMBIGUOUS_PACKETS副作用。
    std::vector<Packet> fragmentedEligible;
    for (int i = left; i <= right; ++i)
        energies[i] = localEnergy(signal, i, half);

    // 用比接受门限低3dB的能量门分割波包，以保留弱侧峰；最终峰仍必须过原SNR门。
    // LOCK状态下，相对分割阈值必须以“预测门内”的最大能量为参考，不能再让理论搜索窗
    // 其它位置的强旁瓣/其它回波抬高阈值，把沿历史轨迹的真实弱首波包压掉。
    double referencePeakEnergy = -1.0;
    int referencePeakIndex = -1;
    const int referenceLeft = result.predictionApplied
        ? std::max(left, result.predictionLeftIndex) : left;
    const int referenceRight = result.predictionApplied
        ? std::min(right, result.predictionRightIndex) : right;
    for (int i = referenceLeft; i <= referenceRight; ++i) {
        if (energies[i] > referencePeakEnergy) {
            referencePeakEnergy = energies[i];
            referencePeakIndex = i;
        }
    }
    if (!(referencePeakEnergy > 0.0)) {
        referencePeakEnergy = *std::max_element(
            energies.begin() + left, energies.begin() + right + 1);
        referencePeakIndex = static_cast<int>(std::distance(
            energies.begin(), std::max_element(energies.begin() + left, energies.begin() + right + 1)));
    }
    result.segmentationReferenceIndex = referencePeakIndex;
    result.segmentationReferenceSnrDb = energySnrDb(referencePeakEnergy, result.noiseEnergy);

    // 相对边界仅用于分割，不取代绝对SNR质量门。真实数据中的缓慢漂移可能
    // 高于背景7dB，却远弱于纵波包；若不加相对边界，会把整窗误拼成一个超宽波包。
    const double segmentationThreshold = std::max(
        result.noiseEnergy * std::pow(10.0, std::max(3.0, p.minLocalSnrDb - 3.0) / 10.0),
        p.packetEnergyFraction * referencePeakEnergy);
    // LOCK碎片恢复的低阈值只用于“从已存在的高阈值seed向两侧重建完整波包”，
    // 绝不用于全窗发现新候选。这样可恢复多瓣波包的弱侧翼，同时不会把远处底噪当成纵波。
    const double fragmentedSupportThreshold = std::max(
        result.noiseEnergy * std::pow(10.0, p.lockedFragmentedSupportSnrDb / 10.0),
        p.lockedFragmentedSupportEnergyFraction * referencePeakEnergy);
    const double fragmentedSupportThresholdSnrDb =
        energySnrDb(fragmentedSupportThreshold, result.noiseEnergy);
    for (int i = left; i <= right; ++i) {
        if (energies[i] < segmentationThreshold)
            continue;
        Packet next;
        next.left = i;
        while (i + 1 <= right && energies[i + 1] >= segmentationThreshold)
            ++i;
        next.right = i;
        if (!packets.empty()
            && (next.left - packets.back().right - 1 <= p.packetMergeGap
                || (next.left + next.right - packets.back().left - packets.back().right) / 2
                     < p.candidateSeparationSamples)) {
            packets.back().right = next.right;
        } else {
            packets.push_back(next);
        }
    }

    if (result.predictionGateMaxIndex >= 0) {
        for (const Packet &packet : packets) {
            if (result.predictionGateMaxIndex >= packet.left
                && result.predictionGateMaxIndex <= packet.right) {
                result.predictionGateCoreLeftIndex = packet.left;
                result.predictionGateCoreRightIndex = packet.right;
                result.predictionGateCoreWidthSamples = packet.right - packet.left + 1;
                break;
            }
        }
    }

    bool sawShape = false;
    result.longitudinalStatus = QStringLiteral("UNCONFIRMED");
    for (Packet packet : packets) {
        const int originalCoreLeft = packet.left;
        const int originalCoreRight = packet.right;
        const int width = packet.right - packet.left + 1;
        packet.clippedBySearchWindow = packet.left <= left || packet.right >= right;
        const bool overlapsPrediction = !(packet.right < result.predictionLeftIndex
                                          || packet.left > result.predictionRightIndex);
        // LOCK宽包仅在常规宽度门失败后进入恢复通道。热态双尖峰强弱交换会让
        // 能量核心临时展宽，且历史ToF可能位于两峰之间的能量谷，因此不能再要求
        // “预测中心本点SNR”过门；改用预测门内最大SNR证明信号存在。宽度只放宽到
        // 常规上限的2倍，后续仍必须通过RECENT模板高相关、预测残差和能量峰距离复核。
        const int guardedBroadMaxWidth = std::max(
            p.lockedPacketMaxCoreWidth, std::min(250, 2 * p.packetMaxWidth));
        packet.lockedBroadContinuation = history.lockActive
            && overlapsPrediction
            && !packet.clippedBySearchWindow
            && width > p.packetMaxWidth
            && width <= guardedBroadMaxWidth
            && std::isfinite(result.predictionGateMaxLocalSnrDb)
            && result.predictionGateMaxLocalSnrDb >= p.minLocalSnrDb;

        // 对4~7点高阈值碎片，只有稳定LOCK且该seed就在预测门内时才尝试低阈值重建。
        // 重建允许跨越最多2个连续低阈值点，以连接双峰/多瓣之间的内部谷；边界仍被预测门严格限制。
        if (history.lockActive && overlapsPrediction && !packet.clippedBySearchWindow
            && width >= p.lockedFragmentedCoreMinWidth && width < p.packetMinWidth) {
            double coreMaximumEnergy = 0.0;
            for (int i = packet.left; i <= packet.right; ++i)
                coreMaximumEnergy = std::max(coreMaximumEnergy, energies[i]);
            const double coreMaxSnrDb = energySnrDb(coreMaximumEnergy, result.noiseEnergy);
            if (std::isfinite(coreMaxSnrDb) && coreMaxSnrDb >= p.lockedFragmentedMinMaxSnrDb) {
                const int supportBoundLeft = std::max(left, result.predictionLeftIndex);
                const int supportBoundRight = std::min(right, result.predictionRightIndex);
                int supportLeft = packet.left;
                int supportRight = packet.right;
                int lowRun = 0;
                for (int i = packet.left - 1; i >= supportBoundLeft; --i) {
                    if (energies[i] >= fragmentedSupportThreshold) {
                        supportLeft = i;
                        lowRun = 0;
                    } else if (++lowRun > 2) {
                        break;
                    }
                }
                lowRun = 0;
                for (int i = packet.right + 1; i <= supportBoundRight; ++i) {
                    if (energies[i] >= fragmentedSupportThreshold) {
                        supportRight = i;
                        lowRun = 0;
                    } else if (++lowRun > 2) {
                        break;
                    }
                }
                const int supportWidth = supportRight - supportLeft + 1;
                if (result.predictionGateMaxIndex >= originalCoreLeft
                    && result.predictionGateMaxIndex <= originalCoreRight) {
                    result.lockedFragmentedCoreWidthSamples = width;
                    result.lockedFragmentedSupportWidthSamples = supportWidth;
                    result.lockedFragmentedSupportThresholdSnrDb = fragmentedSupportThresholdSnrDb;
                }
                // 后置碎片恢复不再要求低阈值支持区一定扩展到常规packetMinWidth。
                // 只要存在4~7点高阈值seed，就允许进入严格RECENT模板身份复核；
                // 能否输出由后面的高相关/小残差决定，而不是靠缩短全局脉宽门。
                const int minimumSupportWidth = p.lockedFragmentedCoreMinWidth;
                if (supportWidth >= minimumSupportWidth
                    && supportWidth <= p.packetMaxWidth) {
                    packet.lockedFragmentedRecoveryCandidate = true;
                    packet.fragmentedCoreWidthSamples = width;
                    packet.fragmentedSupportWidthSamples = supportWidth;
                    packet.fragmentedSupportThresholdSnrDb = fragmentedSupportThresholdSnrDb;
                    packet.left = supportLeft;
                    packet.right = supportRight;
                    packet.clippedBySearchWindow = false;
                }
            }
        }

        // 普通完整波包仍执行packetMin/MaxWidth。宽核心和碎片核心都只能走各自的LOCK严格恢复通道。
        if (!packet.clippedBySearchWindow
            && ((width < p.packetMinWidth && !packet.lockedFragmentedRecoveryCandidate)
                || (width > p.packetMaxWidth && !packet.lockedBroadContinuation)))
            continue;
        sawShape = true;
        if (!overlapsPrediction)
            continue;
        packet.left = std::max(0, packet.left - half);
        packet.right = std::min(n - 1, packet.right + half);
        double maximum = -1.0;
        packet.strongest = packet.left;
        for (int i = packet.left; i <= packet.right; ++i) {
            packet.score += std::max(0.0, energies[i] - result.noiseEnergy);
            if (energies[i] > maximum) {
                maximum = energies[i];
                packet.strongest = i;
            }
        }
        packet.primaryEnergy = maximum;

        // 后续回波只作辅助佐证。第二回波受温度、衰减和耦合影响可能很弱，
        // 因此低相关不能单独否决首波包；首波包到时、SNR及可用的定位证据才是硬门。
        // 由粗到时扣除固定厚度偏置估计一次往返间隔，不使用CSV旧Thickness列。
        const double biasSamples = 2.0 * CURRENT_FIXED_BIAS_MM * 1e-3
            / p.longitudinalVelocityMps * p.sampleRateHz;
        packet.expectedLag = packet.strongest + p.delayCount - biasSamples;
        if (packet.expectedLag > packet.right - packet.left + half) {
            const int lagLeft = std::max(packet.right - packet.left + half,
                static_cast<int>(std::ceil(packet.expectedLag * (1.0 - p.echoPeriodTolerance))));
            const int lagRight = std::min(n - 1 - packet.right,
                static_cast<int>(std::floor(packet.expectedLag * (1.0 + p.echoPeriodTolerance))));
            const size_t packetSampleCount = static_cast<size_t>(packet.right - packet.left + 1);
            for (int lag = lagLeft; lag <= lagRight; ++lag) {
                const bool dirty = std::any_of(contaminated.begin() + packet.left + lag,
                    contaminated.begin() + packet.right + lag + 1, [](bool value) { return value; });
                const double targetEnergy = localEnergy(signal, packet.strongest + lag, half);
                const double targetSnrDb = energySnrDb(targetEnergy, result.noiseEnergy);
                const double targetEnergyRatio = targetEnergy / std::max(packet.primaryEnergy, 1e-24);
                if (dirty || !std::isfinite(targetSnrDb) || targetSnrDb < p.echoMinSnrDb
                    || targetEnergyRatio < p.echoMinRelativeEnergy)
                    continue;
                const double rho = packetCorrelation(
                    signal.data() + packet.left,
                    signal.data() + packet.left + lag,
                    packetSampleCount);
                // 反射可能反相，因此辅助回波看绝对相关；连续帧跟踪仍保留相位符号。
                if (std::isfinite(rho) && std::abs(rho) > packet.echoRho) {
                    packet.echoRho = std::abs(rho);
                    packet.echoSnrDb = targetSnrDb;
                    packet.echoEnergyRatio = targetEnergyRatio;
                    packet.echoLag = lag;
                }
            }
        }
        if (packet.lockedFragmentedRecoveryCandidate)
            fragmentedEligible.push_back(packet);
        else
            eligible.push_back(packet);
        if (!std::isfinite(result.echoCorrelation) || packet.echoRho > result.echoCorrelation) {
            result.echoCorrelation = packet.echoRho;
            result.echoSnrDb = packet.echoSnrDb;
            result.echoEnergyRatio = packet.echoEnergyRatio;
            result.echoDelaySamples = packet.echoLag;
            result.expectedEchoDelaySamples = packet.expectedLag;
        }
    }
    if (eligible.empty() && !fragmentedEligible.empty()) {
        // 多瓣宽波包可能被高阈值切成2~3个4~7点碎片。只有正常候选完全为空时，
        // 才在LOCK预测位置附近把“低阈值支持区相邻、峰间距有限、整体围绕历史ToF”
        // 的碎片合成一个physical packet cluster。cluster内部不再互相做candidateRatio；
        // 最终ToF仍必须通过后续RECENT模板高相关/小预测残差门。
        const bool lostFrozenSearch = history.reacquireRequired
            && history.longTermIdentityAvailable
            && std::isfinite(history.longTermIdentityCenter);
        const double identitySearchCenter = lostFrozenSearch
            ? history.longTermIdentityCenter : history.center;
        if ((history.lockActive || lostFrozenSearch)
            && std::isfinite(identitySearchCenter)
            && fragmentedEligible.size() > 1) {
            std::stable_sort(fragmentedEligible.begin(), fragmentedEligible.end(),
                [](const Packet &a, const Packet &b) { return a.left < b.left; });
            std::vector<Packet> clusters;
            clusters.reserve(fragmentedEligible.size());
            for (const Packet &fragment : fragmentedEligible) {
                if (clusters.empty()) {
                    clusters.push_back(fragment);
                    continue;
                }
                Packet &cluster = clusters.back();
                const int gap = fragment.left - cluster.right - 1;
                const int strongestSpan = std::abs(fragment.strongest - cluster.strongest);
                const int combinedLeft = std::min(cluster.left, fragment.left);
                const int combinedRight = std::max(cluster.right, fragment.right);
                const double combinedMid = 0.5 * (combinedLeft + combinedRight);
                const bool straddlesHistory =
                    (cluster.strongest <= identitySearchCenter && fragment.strongest >= identitySearchCenter)
                    || (fragment.strongest <= identitySearchCenter && cluster.strongest >= identitySearchCenter);
                const bool centeredCluster = std::abs(combinedMid - identitySearchCenter) <= 4.0;
                const bool supportClose = gap <= p.packetMergeGap + 2 * half;
                const bool compactCluster = strongestSpan <= p.candidateSeparationSamples + 4;
                if (supportClose && compactCluster && (straddlesHistory || centeredCluster)) {
                    cluster.left = combinedLeft;
                    cluster.right = combinedRight;
                    cluster.score += fragment.score;
                    cluster.fragmentedCoreWidthSamples += fragment.fragmentedCoreWidthSamples;
                    cluster.fragmentedSupportWidthSamples = cluster.right - cluster.left + 1;
                    cluster.fragmentedSupportThresholdSnrDb = std::max(
                        cluster.fragmentedSupportThresholdSnrDb,
                        fragment.fragmentedSupportThresholdSnrDb);
                    if (fragment.primaryEnergy > cluster.primaryEnergy) {
                        cluster.primaryEnergy = fragment.primaryEnergy;
                        cluster.strongest = fragment.strongest;
                    }
                    if (fragment.echoRho > cluster.echoRho) {
                        cluster.echoRho = fragment.echoRho;
                        cluster.echoSnrDb = fragment.echoSnrDb;
                        cluster.echoEnergyRatio = fragment.echoEnergyRatio;
                        cluster.echoLag = fragment.echoLag;
                        cluster.expectedLag = fragment.expectedLag;
                    }
                } else {
                    clusters.push_back(fragment);
                }
            }
            fragmentedEligible = std::move(clusters);
        }
        eligible = std::move(fragmentedEligible);
    }
    // LOCK_GRACE_SEARCH候选重建：稳定LOCK下标准候选生成失败时，
    // 使用冻结身份中心扩大搜索窗口重新生成候选，避免直接进入NO_SIGNAL/LOST。
    if (eligible.empty() && history.lockActive && std::isfinite(history.center)) {
        result.trackingState = QStringLiteral("LOCK_GRACE_SEARCH");
        const int graceLeft = std::max(left, static_cast<int>(std::floor(history.center - 40.0)));
        const int graceRight = std::min(right, static_cast<int>(std::ceil(history.center + 40.0)));
        // 多候选重构：LOCK_GRACE_SEARCH不再只保留最高能量点。
        // 保留Top-N候选，交由后续Identity/Shape评分选择，避免双峰强弱交换时身份漂移。
        std::vector<Packet> graceCandidates;
        for (int i = graceLeft; i <= graceRight; ++i) {
            const double snr = energySnrDb(energies[i], result.noiseEnergy);
            if (!std::isfinite(snr) || snr < p.minLocalSnrDb - 2.0)
                continue;
            Packet candidate;
            candidate.left = std::max(graceLeft, i - half);
            candidate.right = std::min(graceRight, i + half);
            candidate.strongest = i;
            candidate.primaryEnergy = energies[i];
            candidate.score = energies[i];
            graceCandidates.push_back(candidate);
        }
        std::stable_sort(graceCandidates.begin(), graceCandidates.end(),
            [](const Packet &a, const Packet &b) { return a.score > b.score; });
        const size_t keepCount = std::min<size_t>(3, graceCandidates.size());
        for (size_t k = 0; k < keepCount; ++k)
            eligible.push_back(graceCandidates[k]);

        // 宽波包/双峰融合候选：当两个相邻候选共同覆盖历史身份中心时，
        // 保留一个融合Packet作为额外竞争项。后续Identity/Shape排序负责选择，
        // 不改变正常LOCK路径。
        if (history.lockActive && std::isfinite(history.center)
            && graceCandidates.size() >= 2) {
            for (size_t a = 0; a < keepCount; ++a) {
                for (size_t b = a + 1; b < keepCount; ++b) {
                    const Packet &pa = graceCandidates[a];
                    const Packet &pb = graceCandidates[b];
                    const int separation = std::abs(pa.strongest - pb.strongest);
                    const bool coversIdentity =
                        std::min(pa.strongest, pb.strongest) <= history.center
                        && history.center <= std::max(pa.strongest, pb.strongest);
                    if (coversIdentity && separation <= p.lockStraddlingMaxSeparationSamples) {
                        Packet merged = pa;
                        merged.left = std::min(pa.left, pb.left);
                        merged.right = std::max(pa.right, pb.right);
                        merged.score = pa.score + pb.score;
                        merged.primaryEnergy = std::max(pa.primaryEnergy, pb.primaryEnergy);
                        merged.strongest = std::abs(pa.strongest - history.center)
                            <= std::abs(pb.strongest - history.center)
                            ? pa.strongest : pb.strongest;
                        eligible.push_back(merged);
                        result.lockGraceEvidence = QStringLiteral("LOCK_GRACE_SEARCH_MERGED_CANDIDATE");
                        a = keepCount;
                        break;
                    }
                }
            }
        }
        if (!eligible.empty()) {
            // 标记为LOCK_GRACE候选：该候选只来自标准candidate失败后的身份保护搜索。
            // 后续仍必须经过packet拟合、模板相关、预测残差和SNR质量链，不能直接输出。
            result.lockGraceCandidate = true;
            result.lockGraceEvidence = QStringLiteral("LOCK_GRACE_SEARCH_MULTI_CANDIDATE_REBUILD");
        } else {
            result.lockGraceEvidence = QStringLiteral("LOCK_GRACE_SEARCH_NO_CANDIDATE");
        }
    }
    if (eligible.empty()) {
        // LOST状态下，冻结身份附近的宽波包可能没有形成标准单峰Packet。
        // 不立即判PACKET_SHAPE_INVALID，而尝试一次弱信号身份重建：后续仍要求
        // 模板相关、拟合质量和重捕获确认，不直接解除LOST。
        if (history.reacquireRequired && history.longTermIdentityAvailable) {
            const bool weakIdentityWindow = std::isfinite(result.maxLocalSnrDb)
                && result.maxLocalSnrDb >= p.minLocalSnrDb - 2.0;
            if (weakIdentityWindow) {
                result.trackingState = QStringLiteral("LOST_WEAK_PACKET_SEARCH");
            }
        }
        const QString reason = !sawShape ? QStringLiteral("PACKET_SHAPE_INVALID")
                                        : QStringLiteral("PREDICTION_GATE");
        rejectWaveform(result, reason, !sawShape
            ? QStringLiteral("没有满足纵波首波包条件的能量片段")
            : QStringLiteral("纵波首波包不在历史预测门内"));
        return result;
    }
    std::stable_sort(eligible.begin(), eligible.end(),
        [](const Packet &a, const Packet &b) { return a.score > b.score; });
    const int originalCandidateCount = static_cast<int>(eligible.size());
    result.candidateCount = originalCandidateCount;
    result.firstSignificance = eligible.front().score;
    result.firstCandidateIndex = eligible.front().strongest;
    result.secondSignificance = 0.0;
    if (eligible.size() > 1) {
        result.secondSignificance = eligible[1].score;
        result.secondCandidateIndex = eligible[1].strongest;
        result.candidateRatio = result.firstSignificance / result.secondSignificance;
        if (result.candidateRatio < p.minCandidateRatio) {
            bool straddlingRecovered = false;
            if (history.lockActive && std::isfinite(history.center)
                && !history.templateValues.empty() && eligible.size() == 2) {
                const Packet &a = eligible[0];
                const Packet &b = eligible[1];
                const int lowStrong = std::min(a.strongest, b.strongest);
                const int highStrong = std::max(a.strongest, b.strongest);
                // 亚采样历史中心可能比整数主峰仅多出不足0.5点；把这一量化误差
                // 纳入“夹持”判断，仍要求后续RECENT模板高相关与≤1点残差。
                const bool straddlesPrediction = lowStrong - 0.5 <= history.center
                    && history.center <= highStrong + 0.5;
                const bool closeEnough = highStrong - lowStrong
                    <= p.lockStraddlingMaxSeparationSamples;
                const int matchLeft = std::max(result.predictionLeftIndex,
                    static_cast<int>(std::ceil(history.center
                        - p.lockRecoveryPendingResidualSamples)));
                const int matchRight = std::min(result.predictionRightIndex,
                    static_cast<int>(std::floor(history.center
                        + p.lockRecoveryPendingResidualSamples)));
                const TemplateQuickMatch recent = matchTrackingTemplate(
                    signal, history.templateValues, history.anchorFraction,
                    matchLeft, matchRight, QStringLiteral("RECENT"));
                const double residual = std::isfinite(recent.peak)
                    ? std::abs(recent.peak - history.center) : NaN;
                // 先记录快速匹配诊断；即便恢复失败，报告也能直接给出相关与位置证据。
                result.recentTemplateCorrelation = recent.correlation;
                result.recentTemplatePeak = recent.peak;
                const bool directEvidence = std::isfinite(recent.correlation)
                    && recent.correlation >= p.lockRecoveryDirectCorrelation
                    && std::isfinite(residual)
                    && residual <= p.lockRecoveryDirectResidualSamples;
                const bool aPeakInGate = a.strongest >= result.predictionLeftIndex
                    && a.strongest <= result.predictionRightIndex;
                const bool bPeakInGate = b.strongest >= result.predictionLeftIndex
                    && b.strongest <= result.predictionRightIndex;
                const bool predictedBranchEvidence = directEvidence
                    || (std::isfinite(recent.correlation) && recent.correlation >= 0.90
                        && std::isfinite(residual) && residual <= 1.5);
                if (predictedBranchEvidence && aPeakInGate != bPeakInGate) {
                    // 两个候选能量接近时，若只有一个候选的主峰真正位于预测门内，
                    // 另一个只是宽支持区边缘擦到门限，则用RECENT模板高相关/小残差
                    // 认证门内分支。该路径不降低显著性比，只处理原本必然判废的歧义帧。
                    const Packet predictedBranch = aPeakInGate ? a : b;
                    eligible.clear();
                    eligible.push_back(predictedBranch);
                    result.lockRecoveryNeedsConfirmation = false;
                    result.recentTemplateCorrelation = recent.correlation;
                    result.recentTemplatePeak = recent.peak;
                    result.lockedAmbiguousReadOnlyRecovered = true;
                    result.recoveryEvidence = QStringLiteral(
                        "LOCK歧义只读恢复：仅门内候选通过RECENT模板身份认证");
                    straddlingRecovered = true;
                } else if (straddlesPrediction && closeEnough && directEvidence) {
                    Packet merged = a;
                    merged.left = std::min(a.left, b.left);
                    merged.right = std::max(a.right, b.right);
                    merged.score = a.score + b.score;
                    if (b.primaryEnergy > a.primaryEnergy) {
                        merged.strongest = b.strongest;
                        merged.primaryEnergy = b.primaryEnergy;
                    }
                    eligible.clear();
                    eligible.push_back(merged);
                    result.lockedStraddlingPacketCandidate = true;
                    result.lockRecoveryNeedsConfirmation = false;
                    result.recentTemplateCorrelation = recent.correlation;
                    result.recentTemplatePeak = recent.peak;
                    straddlingRecovered = true;
                }
            }
            if (!straddlingRecovered) {
                rejectWaveform(result, QStringLiteral("AMBIGUOUS_PACKETS"),
                    QStringLiteral("两个独立纵波候选波包能量接近，且未满足LOCK双瓣夹持模板恢复条件；比值 %1")
                        .arg(result.candidateRatio, 0, 'f', 3));
                return result;
            }
        }
    }

    const Packet &packet = eligible.front();
    result.lockedBroadPacketCandidate = packet.lockedBroadContinuation;
    result.lockedFragmentedPacketCandidate = packet.lockedFragmentedRecoveryCandidate;
    result.lockedFragmentedCoreWidthSamples = packet.fragmentedCoreWidthSamples;
    result.lockedFragmentedSupportWidthSamples = packet.fragmentedSupportWidthSamples;
    result.lockedFragmentedSupportThresholdSnrDb = packet.fragmentedSupportThresholdSnrDb;
    result.packetLeftIndex = packet.left;
    result.packetRightIndex = packet.right;
    result.echoCorrelation = packet.echoRho;
    result.echoSnrDb = packet.echoSnrDb;
    result.echoEnergyRatio = packet.echoEnergyRatio;
    result.echoDelaySamples = packet.echoLag;
    result.expectedEchoDelaySamples = packet.expectedLag;
    result.echoConfirmed = packet.echoRho >= p.echoMinCorrelation;

    double selected = NaN;
    double selectedVoltage = NaN;
    auto useOriginalPeak = [&](const QString &reason) {
        const OriginalPeakResult original = findOriginalSinglePeak(
            signal, std::max(packet.left, result.predictionLeftIndex),
            std::min(packet.right, result.predictionRightIndex), p.detectMode);
        if (!original.found)
            return false;
        result.fallbackPeakUsed = true;
        result.fallbackReason = reason;
        result.fitSinglePeakIndex = original.position;
        result.packetCenterIndex = original.position;
        selected = original.position;
        selectedVoltage = original.voltage;
        return true;
    };

    PacketPeakFit::Fit fit;
    // 消融实验开关必须保持关闭；完整波包正常执行整包等效单脉冲拟合。
    constexpr bool EXPERIMENT_DISABLE_PACKET_FIT = false;
    if (packet.clippedBySearchWindow || EXPERIMENT_DISABLE_PACKET_FIT) {
        // 理论窗截断或消融实验时使用原始单峰寻峰输出。
        const QString fallbackReason = packet.clippedBySearchWindow
            ? QStringLiteral("THEORETICAL_WINDOW_CLIPPED")
            : QStringLiteral("EXPERIMENT_NO_PACKET_FIT");
        if (!useOriginalPeak(fallbackReason)) {
            rejectWaveform(result, QStringLiteral("PEAK_NOT_FOUND"),
                QStringLiteral("理论窗截断波包，传统单峰回退仍未找到可用峰"));
            return result;
        }
    } else {
        // 完整波包先形成短窗RMS包络，再从包络峰向两侧寻找10%峰高的“脉冲底部”。
        // 拟合窗口只保留左右底部之间的脉冲；高斯sigma由该10%脉宽唯一确定，
        // 不再允许sigma自由变宽去覆盖双尖峰/旁瓣。拟合仅优化中心、幅值和背景。
        PacketPeakFit::FitOptions fitOptions;
        fitOptions.footFraction = p.fitFootFraction;
        fitOptions.footConfirmSamples = p.fitFootConfirmSamples;
        fitOptions.footSmoothRadius = p.fitFootSmoothRadius;
        fitOptions.centerFineHalfRange = p.fitCenterHalfRange;
        fitOptions.centerSearchCoarseStep = p.fitCenterSearchCoarseStep;
        fitOptions.amplitudeMinRatio = p.fitAmplitudeMinRatio;
        fitOptions.amplitudeMaxRatio = p.fitAmplitudeMaxRatio;
        fit = PacketPeakFit::fit(signal, packet.left, packet.right,
                                 p.detectMode.toUtf8().constData(), fitOptions,
                                 &workspace.fit);
        result.fitR2 = fit.rSquared;
        result.fitResidualRms = fit.residualRms;
        result.fitSucceeded = fit.solved && fit.rSquared >= p.fitMinR2;
        if (fit.solved) {
            const int fittedPointCount = std::max(
                0, static_cast<int>(std::floor((fit.right - fit.left) / 0.25)) + 1);
            result.fittedPoints.reserve(fittedPointCount);
            for (double x = fit.left; x <= fit.right; x += 0.25)
                result.fittedPoints.append(QVariantMap{{"x", x}, {"y", fit.value(x)}});
        }
        if (!result.fitSucceeded && !result.lockedFragmentedPacketCandidate) {
            // 稳定LOCK下，拟合失败只说明当前形态不适合等效高斯，不等价于纵波身份消失。
            // 只有当前帧能量足够、RECENT模板存在且历史中心有限时，才允许继续进入
            // “模板直接定位”严格复核；最终仍必须通过高相关/小残差门，否则保持FIT_FAILED。
            const bool templateOnlyEligible = history.lockActive
                && std::isfinite(history.center)
                && !history.templateValues.empty()
                && std::isfinite(result.maxLocalSnrDb)
                && result.maxLocalSnrDb >= p.lockTemplateOnlyMinMaxSnrDb;
            if (templateOnlyEligible) {
                result.lockedTemplateOnlyCandidate = true;
            } else {
                rejectWaveform(result, QStringLiteral("PACKET_FIT_FAILED"),
                    QStringLiteral("底部窗口固定脉宽高斯拟合不可信：R²=%1，要求≥%2；且未满足LOCK模板直接恢复前提")
                        .arg(result.fitR2, 0, 'f', 4).arg(p.fitMinR2, 0, 'f', 4));
                return result;
            }
        }
        // 碎片/模板直接恢复允许高斯拟合失败，但只把历史预测中心作为临时占位；
        // 后续必须由RECENT模板在严格预测残差门内重新定位，绝不会用该占位值直接计算厚度。
        selected = result.fitSucceeded ? fit.center : history.center;
        result.fitSinglePeakIndex = result.fitSucceeded ? fit.center : NaN;
        result.packetCenterIndex = selected;
        // 对外显示“脉宽”时使用10%底部宽度，而不是高斯sigma，便于直接和波形脚点对应。
        result.fitPeakWidthSamples = result.fitSucceeded ? fit.pulseWidthSamples : NaN;
        if (history.adaptiveWidthReady
            && std::isfinite(result.fitPeakWidthSamples)
            && std::isfinite(history.adaptiveWidthMedianSamples)
            && std::isfinite(history.adaptiveWidthToleranceSamples)
            && history.adaptiveWidthToleranceSamples > 0.0) {
            result.adaptiveWidthDeviationSamples = std::abs(
                result.fitPeakWidthSamples - history.adaptiveWidthMedianSamples);
            result.adaptiveWidthNormalizedDeviation = result.adaptiveWidthDeviationSamples
                / history.adaptiveWidthToleranceSamples;
            if (result.adaptiveWidthNormalizedDeviation <= 1.0) {
                result.adaptiveWidthState = QStringLiteral("WIDTH_NORMAL");
            } else if (result.adaptiveWidthNormalizedDeviation <= p.adaptiveWidthOutlierMultiplier) {
                result.adaptiveWidthState = QStringLiteral("WIDTH_TRANSITION");
            } else {
                result.adaptiveWidthState = QStringLiteral("WIDTH_OUTLIER");
            }
        } else if (std::isfinite(result.fitPeakWidthSamples)) {
            result.adaptiveWidthState = QStringLiteral("WIDTH_LEARNING");
        }
    }

    // 信号存在性/SNR质量仍以当前帧已经通过波包拟合或受控fallback得到的物理候选位置评价；
    // LOCK后模板相关只改变ToF定义，不应因为相关锚点落在同一波包的零交叉/低能量点而把真实信号改判NO_SIGNAL。
    double qualityCenter = selected;

    if (!history.templateValues.empty() || !history.confirmedTemplateValues.empty()
        || !history.emaTemplateValues.empty() || !history.shapeTemplateValues.empty()
        || !history.frozenIdentityTemplate.empty()) {
        const bool precisionLockRecoveryCandidate = result.lockedFragmentedPacketCandidate
            || result.lockedStraddlingPacketCandidate || result.lockedTemplateOnlyCandidate;
        const double precisionMatchResidual = result.lockedFragmentedPacketCandidate
            ? p.lockedFragmentedMaxPredictionResidualSamples
            : p.lockRecoveryPendingResidualSamples;
        const int matchLeft = precisionLockRecoveryCandidate
            && history.lockActive && std::isfinite(history.center)
            ? std::max(result.predictionLeftIndex,
                       static_cast<int>(std::ceil(history.center - precisionMatchResidual)))
            : std::max(packet.left, result.predictionLeftIndex);
        const int matchRight = precisionLockRecoveryCandidate
            && history.lockActive && std::isfinite(history.center)
            ? std::min(result.predictionRightIndex,
                       static_cast<int>(std::floor(history.center + precisionMatchResidual)))
            : std::min(packet.right, result.predictionRightIndex);
        std::vector<TemplateQuickMatch> templateMatches;
        std::vector<const TemplateQuickMatch *> hardMatches;
        templateMatches.reserve(4);
        hardMatches.reserve(4);
        if (!history.templateValues.empty())
            templateMatches.push_back(matchTrackingTemplate(signal, history.templateValues,
                history.anchorFraction, matchLeft, matchRight, QStringLiteral("RECENT")));
        if (!history.confirmedTemplateValues.empty())
            templateMatches.push_back(matchTrackingTemplate(signal, history.confirmedTemplateValues,
                history.confirmedAnchorFraction, matchLeft, matchRight, QStringLiteral("CONFIRMED")));
        if (!history.emaTemplateValues.empty())
            templateMatches.push_back(matchTrackingTemplate(signal, history.emaTemplateValues,
                history.emaAnchorFraction, matchLeft, matchRight, QStringLiteral("EMA")));
        // 形态模板只在当前帧已被自适应脉宽判为形态过渡时参与，避免久远形态在稳定区干扰。
        if (!history.shapeTemplateValues.empty()
            && result.adaptiveWidthState == QStringLiteral("WIDTH_TRANSITION"))
            templateMatches.push_back(matchTrackingTemplate(signal, history.shapeTemplateValues,
                history.shapeAnchorFraction, matchLeft, matchRight, QStringLiteral("SHAPE")));

        // LOST身份漂移搜索：正常LOCK保持原预测门，只有LOST且存在长期身份时，
        // 使用冻结CONFIRMED/EMA双模板在扩展窗口内竞争。该路径只提供身份证据，
        // 不直接覆盖prediction历史，避免错误峰改变速度模型。
        if (history.reacquireRequired && history.longTermIdentityAvailable
            && !history.frozenIdentityTemplate.empty()
            && std::isfinite(history.longTermIdentityCenter)) {
            const int lostLeft = std::max(0, static_cast<int>(std::floor(
                history.longTermIdentityCenter - p.predictionHalfWidth - 8)));
            const int lostRight = std::min(n - 1, static_cast<int>(std::ceil(
                history.longTermIdentityCenter + p.predictionHalfWidth + 8)));
            const TemplateQuickMatch lostFrozen = matchTrackingTemplate(
                signal, history.frozenIdentityTemplate,
                history.frozenIdentityFraction, lostLeft, lostRight,
                QStringLiteral("LOST_CONFIRMED"));
            if (std::isfinite(lostFrozen.correlation) && std::isfinite(lostFrozen.peak)
                && lostFrozen.correlation >= p.trackingMinCorrelation) {
                templateMatches.push_back(lostFrozen);
            }
            if (!history.emaTemplateValues.empty()) {
                const TemplateQuickMatch lostEma = matchTrackingTemplate(
                    signal, history.emaTemplateValues,
                    history.emaAnchorFraction, lostLeft, lostRight,
                    QStringLiteral("LOST_EMA"));
                if (std::isfinite(lostEma.correlation) && std::isfinite(lostEma.peak)
                    && lostEma.correlation >= p.trackingMinCorrelation) {
                    templateMatches.push_back(lostEma);
                }
            }
        }

        for (const TemplateQuickMatch &m : templateMatches) {
            if (m.source == QStringLiteral("RECENT")) {
                result.recentTemplateCorrelation = m.correlation;
                result.recentTemplatePeak = m.peak;
            } else if (m.source == QStringLiteral("CONFIRMED")) {
                result.confirmedTemplateCorrelation = m.correlation;
                result.confirmedTemplatePeak = m.peak;
            } else if (m.source == QStringLiteral("EMA")) {
                result.emaTemplateCorrelation = m.correlation;
                result.emaTemplatePeak = m.peak;
            } else if (m.source == QStringLiteral("SHAPE")) {
                result.shapeTemplateCorrelation = m.correlation;
                result.shapeTemplatePeak = m.peak;
            }
        }

        for (const TemplateQuickMatch &m : templateMatches)
            if (std::isfinite(m.correlation) && std::isfinite(m.peak)
                && m.correlation >= p.trackingMinCorrelation)
                hardMatches.push_back(&m);
        result.multiTemplateHardMatchCount = static_cast<int>(hardMatches.size());
        if (hardMatches.size() >= 2) {
            double minPeak = hardMatches.front()->peak;
            double maxPeak = hardMatches.front()->peak;
            for (const TemplateQuickMatch *m : hardMatches) {
                minPeak = std::min(minPeak, m->peak);
                maxPeak = std::max(maxPeak, m->peak);
            }
            // 仅作诊断，不因模板之间形态差异直接判废；真正ToF仍受历史预测残差门约束。
            result.multiTemplateAgreementSpreadSamples = maxPeak - minPeak;
        }

        // RECENT仍是主模板：只要它通过原0.65硬相关门，就完全交给原6/9点状态机判断
        // 位置连续性，不允许EMA/SHAPE替代一次“高相关但位置跳变”的真实状态变化。
        // 只有RECENT本身相关失配(<0.65)时，才允许后备模板救回形态变化导致的TRACKING_MISMATCH。
        const TemplateQuickMatch *recentMatch = nullptr;
        for (const TemplateQuickMatch &m : templateMatches)
            if (m.source == QStringLiteral("RECENT"))
                recentMatch = &m;
        auto predictionResidual = [&history](const TemplateQuickMatch &m) {
            return std::isfinite(history.center) && std::isfinite(m.peak)
                ? std::abs(m.peak - history.center) : 0.0;
        };
        // 常规LOCK继续优先RECENT；但宽包恢复本身要求更高相关门。若RECENT只过
        // 常规0.65而未过宽包门，不应提前阻断CONFIRMED/EMA模板接管，否则形态突变
        // 时会把已有更强身份模板忽略掉。该改动只作用于原本即将被宽包门拒绝的帧。
        const double recentRequiredCorrelation = result.lockedBroadPacketCandidate
            ? p.lockedBroadMinTrackingCorrelation
            : (result.lockedFragmentedPacketCandidate ? 0.82 : p.trackingMinCorrelation);
        const bool recentStable = recentMatch
            && std::isfinite(recentMatch->correlation)
            && recentMatch->correlation >= recentRequiredCorrelation;
        // 已有RECENT低相关软恢复路径同样优先于后备模板：多模板不能覆盖一个已经由
        // 拟合中心+预测位置+R²+SNR共同证明可靠的旧路径，否则会改变原ToF身份。
        const bool recentSoftEligible = recentMatch
            && history.lockActive
            && !result.lockedBroadPacketCandidate
            && std::isfinite(recentMatch->correlation)
            && recentMatch->correlation >= p.trackingSoftCorrelation
            && recentMatch->correlation < p.trackingMinCorrelation
            && result.candidateCount == 1
            && result.fitSucceeded
            && !result.fallbackPeakUsed
            && std::isfinite(result.fitR2)
            && result.fitR2 >= p.trackingSoftMinFitR2
            && std::isfinite(result.maxLocalSnrDb)
            && result.maxLocalSnrDb >= p.trackingSoftMinMaxSnrDb
            && std::isfinite(history.center)
            && std::abs(selected - history.center) <= p.trackingSoftFitPredictionToleranceSamples;

        const TemplateQuickMatch *chosenMatch = (recentStable || recentSoftEligible) ? recentMatch : nullptr;
        if (!chosenMatch) {
            for (const TemplateQuickMatch &m : templateMatches) {
                if (m.source == QStringLiteral("RECENT")
                    || !std::isfinite(m.correlation) || !std::isfinite(m.peak)
                    || m.correlation < p.trackingMinCorrelation
                    || predictionResidual(m) > p.trackingBackupTemplateMaxResidualSamples)
                    continue;
                if (!chosenMatch
                    || predictionResidual(m) < predictionResidual(*chosenMatch) - 1e-9
                    || (std::abs(predictionResidual(m) - predictionResidual(*chosenMatch)) <= 1e-9
                        && m.correlation > chosenMatch->correlation))
                    chosenMatch = &m;
            }
        }
        // 碎片包有可靠拟合时可接受0.75以上的RECENT。若后备模板虽然过常规门，
        // 但相关反而更低，不能让它覆盖更可信的RECENT（典型为多瓣碎片形态突变）。
        const double fragmentedSelectionMin = result.fitSucceeded ? 0.75 : 0.82;
        if (result.lockedFragmentedPacketCandidate && recentMatch
            && std::isfinite(recentMatch->correlation)
            && recentMatch->correlation >= fragmentedSelectionMin
            && (!chosenMatch || recentMatch->correlation > chosenMatch->correlation))
            chosenMatch = recentMatch;

        // 若后备模板也没有可靠接管条件，则回到原RECENT决策；RECENT不存在时才取全模板最高相关。
        if (!chosenMatch && recentMatch)
            chosenMatch = recentMatch;
        if (!chosenMatch && !templateMatches.empty())
            chosenMatch = &*std::max_element(templateMatches.begin(), templateMatches.end(),
                [](const TemplateQuickMatch &a, const TemplateQuickMatch &b) {
                    const double ca = std::isfinite(a.correlation) ? a.correlation : -2.0;
                    const double cb = std::isfinite(b.correlation) ? b.correlation : -2.0;
                    return ca < cb;
                });

        if (!chosenMatch || !std::isfinite(chosenMatch->correlation)) {
            if (history.lockActive) {
                rejectWaveform(result, QStringLiteral("TRACKING_MISMATCH"),
                    QStringLiteral("LOCK状态下最近/EMA/形态模板均无法形成有效相关"));
                return result;
            }
        } else {
            result.trackingTemplateSource = chosenMatch->source;
            result.multiTemplateUsed = chosenMatch->source != QStringLiteral("RECENT");
            const std::vector<double> *activeTemplate = &history.templateValues;
            double activeAnchorFraction = history.anchorFraction;
            if (chosenMatch->source == QStringLiteral("CONFIRMED")) {
                activeTemplate = &history.confirmedTemplateValues;
                activeAnchorFraction = history.confirmedAnchorFraction;
            } else if (chosenMatch->source == QStringLiteral("LOST_CONFIRMED")) {
                // LOST漂移搜索命中冻结身份时，相关复核必须继续使用同一冻结模板。
                activeTemplate = &history.frozenIdentityTemplate;
                activeAnchorFraction = history.frozenIdentityFraction;
            } else if (chosenMatch->source == QStringLiteral("EMA")) {
                activeTemplate = &history.emaTemplateValues;
                activeAnchorFraction = history.emaAnchorFraction;
            } else if (chosenMatch->source == QStringLiteral("SHAPE")) {
                activeTemplate = &history.shapeTemplateValues;
                activeAnchorFraction = history.shapeAnchorFraction;
            }

            const int templateHalf = static_cast<int>(activeTemplate->size()) / 2;
            // 只读取 correlationCount 范围内已写入的元素，避免Debug下全容量初始化。
            std::array<std::pair<int, double>, 2048> correlations;
            int correlationCount = 0;
            const int correlationLeft = precisionLockRecoveryCandidate
                ? std::max({matchLeft, result.predictionLeftIndex, templateHalf})
                : std::max({packet.left, result.predictionLeftIndex, templateHalf});
            const int correlationRight = precisionLockRecoveryCandidate
                ? std::min({matchRight, result.predictionRightIndex, n - 1 - templateHalf})
                : std::min({packet.right, result.predictionRightIndex, n - 1 - templateHalf});
            for (int center = correlationLeft; center <= correlationRight; ++center) {
                const double rho = packetCorrelation(
                    activeTemplate->data(), signal.data() + center - templateHalf,
                    activeTemplate->size());
                if (correlationCount >= static_cast<int>(correlations.size()))
                    break;
                correlations[correlationCount++] = {center, std::isfinite(rho) ? rho : -1.0};
            }

            if (correlationCount > 0) {
            const auto begin = correlations.begin();
            const auto end = begin + correlationCount;
            const auto best = std::max_element(begin, end,
                [](const auto &a, const auto &b) { return a.second < b.second; });
            result.trackingCorrelation = best->second;
            if (history.lockActive && std::isfinite(history.center))
                result.trackingFitPredictionResidualSamples = selected - history.center;

            // 第二步优化：主相关门仍保持0.65。只有“相关略低但拟合中心与历史趋势高度一致”
            // 的单候选完整波包，才允许走软恢复；这样避免全局降低相关门重新放入9-9错包。
            const bool softRecovery = history.lockActive
                && !result.lockedBroadPacketCandidate
                && result.trackingCorrelation >= p.trackingSoftCorrelation
                && result.trackingCorrelation < p.trackingMinCorrelation
                && result.candidateCount == 1
                && result.fitSucceeded
                && !result.fallbackPeakUsed
                && std::isfinite(result.fitR2)
                && result.fitR2 >= p.trackingSoftMinFitR2
                && std::isfinite(result.maxLocalSnrDb)
                && result.maxLocalSnrDb >= p.trackingSoftMinMaxSnrDb
                && std::isfinite(result.trackingFitPredictionResidualSamples)
                && std::abs(result.trackingFitPredictionResidualSamples)
                    <= p.trackingSoftFitPredictionToleranceSamples;
            // 更低SNR的低相关边界不写入历史：只有R²≥0.99、最大SNR≥10 dB且
            // 拟合中心距预测≤2.5点时才只读输出，避免降低全局相关门造成错包。
            const bool guardedReadOnlySoftRecovery = history.lockActive
                && !result.lockedBroadPacketCandidate
                && !result.lockedFragmentedPacketCandidate
                && result.trackingCorrelation >= p.trackingSoftCorrelation
                && result.trackingCorrelation < p.trackingMinCorrelation
                && result.candidateCount == 1
                && result.fitSucceeded && !result.fallbackPeakUsed
                && std::isfinite(result.fitR2) && result.fitR2 >= 0.99
                && std::isfinite(result.maxLocalSnrDb) && result.maxLocalSnrDb >= 10.0
                && std::isfinite(result.trackingFitPredictionResidualSamples)
                && std::abs(result.trackingFitPredictionResidualSamples) <= 2.5;

            // LOCK_GRACE窄范围恢复：仅处理已经LOCK且候选仍处于合理身份邻域的形态突变。
            // correlation硬门仍作为最终保护，不降低全局trackingMinCorrelation。
            const double identityResidual = history.lockActive && std::isfinite(history.center)
                ? std::abs(selected - history.center) : NaN;
            const double identityPositionScore = std::isfinite(identityResidual)
                ? std::exp(-identityResidual / 12.0) : 0.0;
            const double shapeCorrelationScore = std::isfinite(result.trackingCorrelation)
                ? std::clamp((result.trackingCorrelation - 0.4) / 0.6, 0.0, 1.0) : 0.0;
            const double fitShapeScore = std::isfinite(result.fitR2)
                ? std::clamp((result.fitR2 - 0.8) / 0.2, 0.0, 1.0) : 0.0;
            result.identityScore = identityPositionScore;
            result.shapeScore = 0.6 * shapeCorrelationScore + 0.4 * fitShapeScore;
            result.lockGraceScore = 0.7 * result.identityScore + 0.3 * result.shapeScore;

            const bool lockGraceRecovery = history.lockActive
                && identityResidual > 8.0
                && identityResidual <= 32.0
                && result.candidateCount <= 2
                && result.fitSucceeded
                && std::isfinite(result.fitR2)
                && result.fitR2 >= 0.90
                && result.identityScore >= 0.55
                && result.shapeScore >= 0.45
                && result.lockGraceScore >= 0.60;
            if (lockGraceRecovery) {
                result.lockGraceRecovered = true;
                result.lockGraceEvidence = QStringLiteral("IDENTITY_KEEP_SHAPE_CHANGE");
                result.trackingState = QStringLiteral("LOCK_GRACE_RECOVERED");
            }

            if (result.trackingCorrelation < p.trackingMinCorrelation
                && !softRecovery && !guardedReadOnlySoftRecovery
                && !lockGraceRecovery) {
                rejectWaveform(result, QStringLiteral("TRACKING_MISMATCH"),
                    QStringLiteral("连续波包相关 %1，低于硬门 %2；软恢复条件未同时满足")
                        .arg(result.trackingCorrelation, 0, 'f', 3)
                        .arg(p.trackingMinCorrelation, 0, 'f', 3));
                return result;
            }
            if (result.lockedFragmentedPacketCandidate) {
                // 高阈值只留下4~7点碎片时，完整高斯可能无法求解。此时允许RECENT或
                // EMA/CONFIRMED模板提供身份：有可靠拟合时相关≥0.75，无拟合时相关≥0.82。
                // 最终还要在下方通过严格预测残差门，且恢复帧只读、不更新历史模板。
                const double fragmentedIdentityMin = result.fitSucceeded ? 0.75 : 0.82;
                const bool fittedShapeEvidence = result.fitSucceeded
                    && std::isfinite(result.fitR2)
                    && result.fitR2 >= p.lockedFragmentedMinFitR2
                    && result.adaptiveWidthState != QStringLiteral("WIDTH_OUTLIER");
                const bool templateOnlyShapeEvidence = !result.fitSucceeded
                    && std::isfinite(result.trackingCorrelation)
                    && result.trackingCorrelation >= fragmentedIdentityMin;
                // 双瓣夹持恢复会先把两个能量片段合成同一物理波包；此时原始
                // candidateCount仍为2，但lockedStraddlingPacketCandidate已由高相关模板认证。
                const bool fragmentedCandidateUnique = result.candidateCount == 1
                    || result.lockedStraddlingPacketCandidate;
                const bool fragmentedShapeEvidence = fragmentedCandidateUnique
                    && !result.fallbackPeakUsed
                    && (fittedShapeEvidence || templateOnlyShapeEvidence);
                const bool templateIdentityEvidence =
                    (result.trackingTemplateSource == QStringLiteral("RECENT")
                     || result.trackingTemplateSource == QStringLiteral("EMA")
                     || result.trackingTemplateSource == QStringLiteral("CONFIRMED"))
                    && std::isfinite(result.trackingCorrelation)
                    && result.trackingCorrelation >= fragmentedIdentityMin;
                if (!fragmentedShapeEvidence || !templateIdentityEvidence) {
                    rejectWaveform(result, QStringLiteral("FRAGMENTED_PACKET_TRACKING_MISMATCH"),
                        QStringLiteral("LOCK碎片波包重建后未同时满足单候选/形态质量/模板身份约束"));
                    return result;
                }
            }
            if (result.lockedBroadPacketCandidate) {
                // 宽包左右瓣强弱互换会降低整包相关；只在预测中心自身SNR充足且拟合质量高时，
                // 允许0.70~宽包硬门之间的相关继续进入残差复核，绝不单靠低相关直接输出。
                const bool guardedBroadCorrelation = std::isfinite(result.trackingCorrelation)
                    && result.trackingCorrelation >= 0.70
                    && std::isfinite(result.predictedCenterLocalSnrDb)
                    && result.predictedCenterLocalSnrDb >= p.minLocalSnrDb
                    && result.fitSucceeded && std::isfinite(result.fitR2)
                    && result.fitR2 >= std::max(0.95, p.fitMinR2);
                if (result.trackingCorrelation < p.lockedBroadMinTrackingCorrelation
                    && !guardedBroadCorrelation) {
                    rejectWaveform(result, QStringLiteral("BROAD_PACKET_TRACKING_MISMATCH"),
                        QStringLiteral("LOCK宽包相关 %1 低于硬门，且预测中心SNR/拟合质量不足")
                            .arg(result.trackingCorrelation, 0, 'f', 3));
                    return result;
                }
            }

            // 记录相关主次峰差用于诊断，但LOCK时不再因为周期性波包的“相关峰不唯一”
            // 回退到当前帧拟合中心；否则左右瓣强弱交换仍会造成8~11点的ToF跳变。
            double competitor = 0.0;
            for (int i = 1; i + 1 < correlationCount; ++i)
                if (std::abs(correlations[i].first - best->first) >= 4
                    && correlations[i].second >= correlations[i - 1].second
                    && correlations[i].second >= correlations[i + 1].second)
                    competitor = std::max(competitor, correlations[i].second);
            result.trackingCorrelationMargin = best->second - competitor;

            if (softRecovery || guardedReadOnlySoftRecovery) {
                // 低相关时不使用不稳定的相关峰位置；使用已经被历史预测约束在容差内的
                // 当前整包拟合中心作为ToF。边界恢复帧只读，不更新模板或位置历史。
                result.trackingSoftRecovered = true;
                const bool useGuardedReadOnlySoft = !softRecovery && guardedReadOnlySoftRecovery;
                result.guardedReadOnlyRecovered = useGuardedReadOnlySoft;
                result.trackedPeakPrediction = selected;
                result.packetCenterIndex = selected;
                result.trackingState = useGuardedReadOnlySoft
                    ? QStringLiteral("SOFT_TRACK_READ_ONLY")
                    : QStringLiteral("SOFT_TRACK_RECOVERED");
            } else {
                double correlationDelta = 0.0;
                const auto bestIndex = static_cast<int>(std::distance(begin, best));
                if (bestIndex > 0 && bestIndex + 1 < correlationCount) {
                    const double yLeft = correlations[bestIndex - 1].second;
                    const double yCenter = correlations[bestIndex].second;
                    const double yRight = correlations[bestIndex + 1].second;
                    const double curvature = yLeft - 2.0 * yCenter + yRight;
                    if (std::isfinite(curvature) && curvature < -1e-12)
                        correlationDelta = std::clamp(
                            0.5 * (yLeft - yRight) / curvature, -0.5, 0.5);
                }
                result.trackedPeakPrediction = best->first + correlationDelta + activeAnchorFraction;

                // LOST重捕获时只允许“冻结长期身份”认证通过的候选进入两帧确认。
                // 单帧绝不直接重锁：这里仅把当前真实模板相关ToF作为候选测量，后续仍由
                // PeakHistory::REACQUIRE_PENDING要求连续两帧位置一致后才回填和RELOCK。
                if (history.reacquireRequired && history.longTermIdentityAvailable) {
                    const double identityResidual = std::isfinite(history.longTermIdentityCenter)
                        ? std::abs(result.trackedPeakPrediction - history.longTermIdentityCenter) : NaN;
                    const bool identityOk =
                        (result.trackingTemplateSource == QStringLiteral("CONFIRMED")
                         || result.trackingTemplateSource == QStringLiteral("LOST_CONFIRMED"))
                        && std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.90
                        && std::isfinite(identityResidual)
                        && identityResidual <= 3.0
                        && result.fitSucceeded
                        && std::isfinite(result.fitR2)
                        && result.fitR2 >= std::max(0.95, p.fitMinR2);
                    if (identityOk) {
                        selected = result.trackedPeakPrediction;
                        result.templateTrackingUsed = true;
                        result.packetCenterIndex = selected;
                        result.trackingState = QStringLiteral("LOST_IDENTITY_CANDIDATE");
                        qualityCenter = packet.strongest;
                    }
                    // 冻结身份不匹配时不改变原有LOST两帧重捕获：候选仍可用当前真实
                    // 整包拟合ToF进入REACQUIRE_PENDING，但不会获得邻域SNR恢复特权。
                }

                if (result.lockedFragmentedPacketCandidate) {
                    result.lockedFragmentedPredictionResidualSamples = std::isfinite(history.center)
                        ? std::abs(result.trackedPeakPrediction - history.center) : NaN;
                    const double fragmentedIdentityMin = result.fitSucceeded ? 0.75 : 0.82;
                    const double fragmentedResidualLimit =
                        p.lockedFragmentedMaxPredictionResidualSamples
                        + ((std::isfinite(result.trackingCorrelation)
                            && result.trackingCorrelation >= 0.83) ? 0.5 : 0.0);
                    const bool fragmentedIdentityOk =
                        (result.trackingTemplateSource == QStringLiteral("RECENT")
                         || result.trackingTemplateSource == QStringLiteral("EMA")
                         || result.trackingTemplateSource == QStringLiteral("CONFIRMED"))
                        && std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= fragmentedIdentityMin
                        && std::isfinite(result.lockedFragmentedPredictionResidualSamples)
                        && result.lockedFragmentedPredictionResidualSamples <= fragmentedResidualLimit;
                    if (!fragmentedIdentityOk) {
                        rejectWaveform(result, QStringLiteral("FRAGMENTED_PACKET_TRACKING_MISMATCH"),
                            QStringLiteral("LOCK碎片波包RECENT模板身份不足：相关=%1，预测残差=%2点")
                                .arg(result.trackingCorrelation, 0, 'f', 3)
                                .arg(result.lockedFragmentedPredictionResidualSamples, 0, 'f', 2));
                        return result;
                    }
                    result.lockedFragmentedPacketRecovered = true;
                    // 信号存在性在重建后的完整波包内评价，但厚度ToF始终使用RECENT模板相关位置。
                    qualityCenter = packet.strongest;
                }

                if (result.lockedStraddlingPacketCandidate) {
                    const double residual = std::isfinite(history.center)
                        ? std::abs(result.trackedPeakPrediction - history.center) : NaN;
                    const bool identityOk = result.trackingTemplateSource == QStringLiteral("RECENT")
                        && std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= p.lockRecoveryDirectCorrelation
                        && std::isfinite(residual)
                        && residual <= p.lockRecoveryDirectResidualSamples;
                    if (!identityOk) {
                        rejectWaveform(result, QStringLiteral("AMBIGUOUS_PACKETS"),
                            QStringLiteral("LOCK双瓣合并后RECENT模板身份不足：相关=%1，预测残差=%2点")
                                .arg(result.trackingCorrelation, 0, 'f', 3)
                                .arg(residual, 0, 'f', 2));
                        return result;
                    }
                    result.lockedStraddlingPacketRecovered = true;
                    qualityCenter = static_cast<int>(std::round(result.trackedPeakPrediction));
                }

                if (result.lockedTemplateOnlyCandidate) {
                    const double residual = std::isfinite(history.center)
                        ? std::abs(result.trackedPeakPrediction - history.center) : NaN;
                    const bool veryHighCorrelation = std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.95;
                    const bool highCorrelation = std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.90;
                    const bool moderateCorrelation = std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.85;
                    const bool residualOk = std::isfinite(residual)
                        && ((veryHighCorrelation && residual <= 2.5)
                            || (highCorrelation && residual <= 1.5)
                            || (moderateCorrelation && residual <= 1.0));
                    const bool identityOk = result.trackingTemplateSource == QStringLiteral("RECENT")
                        && residualOk;
                    if (!identityOk) {
                        rejectWaveform(result, QStringLiteral("PACKET_FIT_FAILED"),
                            QStringLiteral("整包拟合失败，且RECENT模板直接恢复证据不足：相关=%1，预测残差=%2点")
                                .arg(result.trackingCorrelation, 0, 'f', 3)
                                .arg(residual, 0, 'f', 2));
                        return result;
                    }
                    result.lockedTemplateOnlyRecovered = true;
                    qualityCenter = static_cast<int>(std::round(result.trackedPeakPrediction));
                }

                if (result.lockedBroadPacketCandidate) {
                    const double broadTrackingResidual = std::isfinite(history.center)
                        ? std::abs(result.trackedPeakPrediction - history.center) : NaN;
                    result.lockedBroadEnergyPeakDistanceSamples = result.predictionGateMaxIndex >= 0
                        ? std::abs(result.trackedPeakPrediction - result.predictionGateMaxIndex) : NaN;
                    const bool highConfidenceBroadExtension = std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.93
                        && std::isfinite(broadTrackingResidual)
                        && broadTrackingResidual <= 0.75;
                    const double broadEnergyTolerance = p.lockedBroadEnergyPeakToleranceSamples
                        + (highConfidenceBroadExtension ? 1.0 : 0.0);
                    const bool broadEnergyAligned = result.candidateCount == 1
                        && std::isfinite(broadTrackingResidual)
                        && broadTrackingResidual <= p.trackingResidualAcceptSamples
                        && std::isfinite(result.predictionGateMaxLocalSnrDb)
                        && result.predictionGateMaxLocalSnrDb >= p.minLocalSnrDb
                        && std::isfinite(result.lockedBroadEnergyPeakDistanceSamples)
                        && result.lockedBroadEnergyPeakDistanceSamples <= broadEnergyTolerance;
                    // 对明显宽包，最强局部能量可能落在远侧瓣。若模板ToF仍紧贴历史趋势、
                    // 预测中心本身SNR过门且整包拟合质量高，则不再强迫ToF靠近最强侧瓣。
                    const bool broadCenterAligned = result.candidateCount == 1
                        && std::isfinite(result.trackingCorrelation)
                        && result.trackingCorrelation >= 0.70
                        && std::isfinite(broadTrackingResidual)
                        && broadTrackingResidual <= 3.0
                        && std::isfinite(result.predictedCenterLocalSnrDb)
                        && result.predictedCenterLocalSnrDb >= p.minLocalSnrDb
                        && result.fitSucceeded && std::isfinite(result.fitR2)
                        && result.fitR2 >= std::max(0.95, p.fitMinR2);
                    const bool broadEvidenceConsistent = broadEnergyAligned || broadCenterAligned;
                    if (!broadEvidenceConsistent) {
                        rejectWaveform(result, QStringLiteral("BROAD_PACKET_TRACKING_MISMATCH"),
                            QStringLiteral("LOCK宽包虽可拟合，但跟踪残差/能量峰位置与历史ToF不一致"));
                        return result;
                    }
                    // 宽合并包的高斯中心可能被邻近能量拉偏；只有模板高相关且跟踪位置、
                    // 历史预测和门内能量峰三者一致时，才使用模板ToF，并在门内能量峰处评价SNR。
                    result.lockedBroadPacketRecovered = true;
                    qualityCenter = result.predictionGateMaxIndex;
                }

                if (history.lockActive) {
                    // LOCK后最终ToF由整波包模板相关位置主导；当前帧高斯拟合仍保留作
                    // 波包形态/脉宽质量证据，但不再允许左右瓣强弱交换直接改变ToF定义。
                    selected = result.trackedPeakPrediction;
                    result.templateTrackingUsed = true;
                    result.packetCenterIndex = selected;
                }
            }
        } else if (history.lockActive) {
            rejectWaveform(result, QStringLiteral("TRACKING_MISMATCH"),
                QStringLiteral("LOCK状态下没有可用于整波包模板相关的位置"));
            return result;
        }
    }
    }

    if (selected < result.predictionLeftIndex || selected > result.predictionRightIndex) {
        // 第一阶段LOCK_GRACE前置候选旁路：处理“身份保持但ToF中心迁移”的波包形态突变。
        // 仅在稳定LOCK、预测门失败后触发，不改变正常LOCK路径。
        const double graceResidual = std::isfinite(history.center)
            ? std::abs(selected - history.center) : NaN;
        const double graceIdentityScore = std::isfinite(graceResidual)
            ? std::exp(-graceResidual / 12.0) : 0.0;
        const double graceShapeScore = std::isfinite(result.trackingCorrelation)
            ? std::clamp((result.trackingCorrelation - 0.4) / 0.6, 0.0, 1.0) : 0.0;
        const double graceScore = 0.7 * graceIdentityScore + 0.3 * graceShapeScore;
        result.identityScore = graceIdentityScore;
        result.shapeScore = graceShapeScore;
        result.lockGraceScore = graceScore;

        const bool lockGraceCandidate = history.lockActive
            && result.candidateCount <= 2
            && result.fitSucceeded
            && std::isfinite(result.fitR2)
            && result.fitR2 >= 0.90
            && std::isfinite(graceResidual)
            && graceResidual <= 32.0
            && graceIdentityScore >= 0.55
            && graceShapeScore >= 0.45
            && graceScore >= 0.60;

        if (lockGraceCandidate) {
            result.lockGraceRecovered = true;
            result.lockGraceEvidence = QStringLiteral("PRE_PREDICTION_GATE_IDENTITY_KEEP_SHAPE_CHANGE");
            result.trackingState = QStringLiteral("LOCK_GRACE_PRE_GATE_RECOVERED");
            result.templateTrackingUsed = true;
        } else {
            // PRE_LOST_RECOVERY：仅针对强信号、大ToF漂移场景提前尝试恢复。
            // 不改变正常LOCK预测门，不降低普通数据的检出约束。
            const bool preLostRecoveryCandidate = history.lockActive
                && result.candidateCount == 1
                && result.fitSucceeded
                && std::isfinite(result.fitR2)
                && result.fitR2 >= std::max(0.95, p.fitMinR2)
                && std::isfinite(result.trackingCorrelation)
                && result.trackingCorrelation >= 0.85
                && std::isfinite(result.predictionGateMaxLocalSnrDb)
                && result.predictionGateMaxLocalSnrDb >= 12.0;

            if (preLostRecoveryCandidate) {
                result.trackingState = QStringLiteral("PRE_LOST_RECOVERY");
                result.templateTrackingUsed = true;
            } else {
                rejectWaveform(result, QStringLiteral("PREDICTION_GATE"),
                    QStringLiteral("选中的亚采样峰不在有效预测门内"));
                return result;
            }
        }
    }
    const int integer = static_cast<int>(std::round(selected));
    const int qualityInteger = static_cast<int>(std::round(qualityCenter));
    result.localSignalEnergy = localEnergy(signal, qualityInteger, half);
    const double centerSnrDb = energySnrDb(result.localSignalEnergy, result.noiseEnergy);

    // LOCK后最终ToF由整包模板相关位置定义。对双峰/宽包而言，模板ToF或高斯整包中心
    // 可能恰好落在两个局部尖峰之间的能量谷，此时若只在中心的7点小窗评价SNR，
    // 会把“整包明显存在、模板也稳定”的帧误判成NO_SIGNAL。这里仅在已通过模板相关
    // 与预测门约束的同一个候选波包内，取最终ToF附近一个很小支持区的最大局部能量；
    // 不使用理论搜索窗全局最大值，避免远处旁瓣/其它回波替当前纵波包抬高SNR。
    if ((history.lockActive || (history.reacquireRequired && history.longTermIdentityAvailable))
        && result.templateTrackingUsed
        && !result.lockedBroadPacketRecovered
        && result.packetLeftIndex >= 0 && result.packetRightIndex >= result.packetLeftIndex) {
        const int supportHalf = std::max(half, p.packetMergeGap + 2);
        const int supportLeft = std::max({result.packetLeftIndex, result.predictionLeftIndex,
                                          integer - supportHalf, half});
        const int supportRight = std::min({result.packetRightIndex, result.predictionRightIndex,
                                           integer + supportHalf, n - 1 - half});
        for (int i = supportLeft; i <= supportRight; ++i)
            result.localSignalEnergy = std::max(result.localSignalEnergy, energies[i]);

        // 高质量LOCK的多瓣包可能因亚采样ToF跨过整数边界而恰好多漏掉一侧1个sample。
        // 仅当单候选、高R²、高模板相关且当前支持SNR仍不足时，允许在同一已确认波包内
        // 再扩1点；不扩大prediction gate，也不借用其它候选/远处回波能量。
        const double supportSnrDb = energySnrDb(result.localSignalEnergy, result.noiseEnergy);
        const bool guardedOneSampleExtension = history.lockActive
            && result.candidateCount == 1
            && result.fitSucceeded && std::isfinite(result.fitR2)
            && result.fitR2 >= std::max(0.95, p.fitMinR2)
            && std::isfinite(result.trackingCorrelation) && result.trackingCorrelation >= 0.85
            && std::isfinite(result.maxLocalSnrDb) && result.maxLocalSnrDb >= p.minLocalSnrDb
            && std::isfinite(supportSnrDb) && supportSnrDb < p.minLocalSnrDb;
        if (guardedOneSampleExtension) {
            const int extendedLeft = std::max({result.packetLeftIndex, result.predictionLeftIndex,
                                               integer - supportHalf - 1, half});
            const int extendedRight = std::min({result.packetRightIndex, result.predictionRightIndex,
                                                integer + supportHalf + 1, n - 1 - half});
            for (int i = extendedLeft; i <= extendedRight; ++i)
                result.localSignalEnergy = std::max(result.localSignalEnergy, energies[i]);
        }
    }

    // SEARCH启动专用恢复：整包拟合中心落在双峰/多瓣内部谷值时，中心7点SNR会
    // 虚低。首个候选仍走相邻两帧延迟确认；已有至少两个启动有效点时，还允许
    // “显著性比≥2 + 与上一有效峰≤1.5点”的多候选帧实时恢复。两种情况都只在
    // 当前拟合波包的小支持区重评SNR，绝不借用理论窗远处噪声或其它回波。
    const bool dominantStartupMultiCandidate = history.historyCount >= 2
        && result.candidateCount > 1
        && std::isfinite(result.candidateRatio)
        && result.candidateRatio >= std::max(2.0, p.minCandidateRatio)
        && std::isfinite(history.lastAcceptedPeak)
        && std::isfinite(selected)
        && std::abs(selected - history.lastAcceptedPeak) <= 1.5;
    if (history.startupConfirmationEnabled
        && !history.lockActive && !history.reacquireRequired
        && std::isfinite(centerSnrDb)
        && centerSnrDb >= p.lowSnrSoftMinDb && centerSnrDb < p.minLocalSnrDb
        && (result.candidateCount == 1 || dominantStartupMultiCandidate)
        && result.fitSucceeded && !result.fallbackPeakUsed
        && std::isfinite(result.fitR2) && result.fitR2 >= std::max(0.95, p.fitMinR2)
        && result.packetLeftIndex >= 0 && result.packetRightIndex >= result.packetLeftIndex) {
        const int supportHalf = std::max(half, p.packetMergeGap + 2);
        const int supportLeft = std::max({result.packetLeftIndex, result.predictionLeftIndex,
                                          integer - supportHalf, half});
        const int supportRight = std::min({result.packetRightIndex, result.predictionRightIndex,
                                           integer + supportHalf, n - 1 - half});
        double startupSupportEnergy = result.localSignalEnergy;
        for (int i = supportLeft; i <= supportRight; ++i)
            startupSupportEnergy = std::max(startupSupportEnergy, energies[i]);
        result.startupSupportSnrDb = energySnrDb(startupSupportEnergy, result.noiseEnergy);
        if (std::isfinite(result.startupSupportSnrDb)
            && result.startupSupportSnrDb >= p.minLocalSnrDb) {
            result.localSignalEnergy = startupSupportEnergy;
            result.startupRecovered = true;
            if (dominantStartupMultiCandidate) {
                // 前两帧已给出连续ToF，当前主候选又具有强显著性；作为低SNR位置点
                // 加入历史，但lowSnrRecovered会阻止这帧异常形态覆盖连续跟踪模板。
                result.lowSnrRecovered = true;
                result.prelockReadOnlyRecovered = true;
                result.recoveryEvidence = QStringLiteral(
                    "SEARCH预锁定只读恢复：前序ToF连续、主候选显著且同一波包邻域SNR通过");
            } else {
                result.startupSupportRecovered = true;
                result.recoveryEvidence = QStringLiteral(
                    "SEARCH启动支持区恢复：同一波包拟合和邻域能量证据通过");
            }
        }
    }
    result.snrDb = energySnrDb(result.localSignalEnergy, result.noiseEnergy);

    // LOCK_GRACE_PRE_SNR_RECOVERY：仅用于稳定LOCK状态下的弱SNR形态突变恢复。
    // 位置位于SNR判定之前，避免已经存在真实波包但局部中心能量下降时提前返回NO_SIGNAL。
    // 不改变正常candidate判定，也不降低全局SNR阈值；恢复仍要求历史身份、拟合质量和位置连续性。
    if ((!std::isfinite(result.snrDb) || result.snrDb < p.minLocalSnrDb)
        && history.lockActive
        && !history.reacquireRequired
        && result.candidateCount == 1
        && result.fitSucceeded
        && !result.fallbackPeakUsed
        && std::isfinite(result.fitR2)
        && result.fitR2 >= 0.95
        && std::isfinite(result.trackingCorrelation)
        && result.trackingCorrelation >= 0.80
        && std::isfinite(history.center)
        && std::isfinite(selected)
        && std::abs(selected - history.center) <= 3.0
        && std::isfinite(result.maxLocalSnrDb)
        && result.maxLocalSnrDb >= p.lowSnrSoftMinDb) {
        result.lowSnrRecovered = true;
        result.guardedReadOnlyRecovered = true;
        result.trackingState = QStringLiteral("LOCK_GRACE_PRE_SNR_RECOVERED");
        result.recoveryEvidence = QStringLiteral("LOCK身份保持：SNR前置恢复，使用同一波包拟合/模板证据");
        // 保留原始snrDb用于统计，不修改SNR阈值。
    }

    const bool coldGlobalSearch = history.windowStage == SearchWindowStage::GlobalSearch
        && !history.lockActive && !history.reacquireRequired && history.historyCount == 0;
    // GLOBAL_SEARCH不再抬高最终SNR硬门。7~9.5 dB的弱全局候选仍可通过，
    // 但必须同时具备单候选、高R²和后续Echo高相关三种独立证据；这样抑制
    // “全局ROI里总能挑到一个噪声局部最大值”，同时保留真实弱启动波包。
    const bool coldGlobalWeakBand = coldGlobalSearch
        && std::isfinite(result.snrDb)
        && result.snrDb >= p.minLocalSnrDb
        && result.snrDb < p.minLocalSnrDb + 2.5;
    const bool coldGlobalWeakEvidence = coldGlobalWeakBand
        && result.candidateCount == 1
        && result.fitSucceeded && !result.fallbackPeakUsed
        && std::isfinite(result.fitR2) && result.fitR2 >= 0.95
        && result.echoConfirmed
        && std::isfinite(result.echoCorrelation)
        && result.echoCorrelation >= std::max(p.echoMinCorrelation, 0.85);
    const bool coldGlobalWeakRejected = coldGlobalWeakBand && !coldGlobalWeakEvidence;
    if (!std::isfinite(result.snrDb) || result.snrDb < p.minLocalSnrDb
        || coldGlobalWeakRejected) {
        const bool inLowSnrBand = std::isfinite(result.snrDb)
            && result.snrDb >= p.lowSnrSoftMinDb
            && result.snrDb < p.minLocalSnrDb;
        result.lowSnrSoftCandidate = result.lowSnrSoftCandidate || inLowSnrBand;
        result.lowSnrTrackingResidualSamples = history.lockActive && std::isfinite(history.center)
            ? selected - history.center : NaN;
        const bool stableLock = history.lockActive
            && !history.reacquireRequired
            && !history.templateValues.empty();
        const bool lowSnrEvidence = inLowSnrBand
            && stableLock
            && result.candidateCount == 1
            && result.fitSucceeded
            && !result.fallbackPeakUsed
            && !result.lockedBroadPacketCandidate
            && std::isfinite(result.fitR2)
            && result.fitR2 >= p.lowSnrSoftMinFitR2
            && std::isfinite(result.trackingCorrelation)
            && result.trackingCorrelation >= p.lowSnrSoftMinTrackingCorrelation
            && std::isfinite(result.lowSnrTrackingResidualSamples)
            && std::abs(result.lowSnrTrackingResidualSamples) <= p.lowSnrSoftMaxResidualSamples;
        // 极低SNR不做恢复；仅在同一候选的最大SNR≥8 dB、R²≥0.99、
        // 模板相关/预测残差共同成立时，允许5~7 dB边界帧只读输出。
        const bool guardedLowSnrEvidence = inLowSnrBand
            || (std::isfinite(result.snrDb) && result.snrDb >= 5.0
                && result.snrDb < p.minLocalSnrDb);
        const bool guardedLowSnrConsistent = guardedLowSnrEvidence
            && history.lockActive && !history.reacquireRequired
            && result.candidateCount == 1
            && result.fitSucceeded && !result.fallbackPeakUsed
            && !result.lockedBroadPacketCandidate
            && std::isfinite(result.fitR2) && result.fitR2 >= 0.99
            && std::isfinite(result.maxLocalSnrDb)
            && result.maxLocalSnrDb >= (result.snrDb >= 6.0 ? 7.0 : 8.0)
            && std::isfinite(result.trackingCorrelation)
            && ((result.trackingCorrelation >= 0.85)
                || (result.snrDb >= 6.0 && result.trackingCorrelation >= 0.80))
            && std::isfinite(result.lowSnrTrackingResidualSamples)
            && std::abs(result.lowSnrTrackingResidualSamples) <= 2.1;
        const bool prelockHistoryEvidence = result.startupRecovered
            && !result.startupSupportRecovered
            && result.lowSnrRecovered
            && !history.lockActive
            && history.historyCount >= 2;
        if (lowSnrEvidence || guardedLowSnrConsistent || prelockHistoryEvidence) {
            if (!lowSnrEvidence && guardedLowSnrConsistent)
                result.guardedReadOnlyRecovered = true;
            result.lowSnrRecovered = true;
            result.trackingState = prelockHistoryEvidence
                ? QStringLiteral("PRELOCK_LOW_SNR_RECOVERED")
                : QStringLiteral("LOW_SNR_RECOVERED");
        } else {
            rejectWaveform(result, QStringLiteral("NO_SIGNAL"),
                coldGlobalWeakRejected
                    ? QStringLiteral("GLOBAL_SEARCH弱候选SNR %1 dB虽过基础门，但缺少单候选/高R²/Echo高相关联合证据")
                        .arg(result.snrDb, 0, 'f', 2)
                    : (inLowSnrBand
                        ? QStringLiteral("SNR %1 dB处于%2~%3 dB软恢复区，但LOCK相关/R²/位置一致性条件未同时满足")
                            .arg(result.snrDb, 0, 'f', 2)
                            .arg(p.lowSnrSoftMinDb, 0, 'f', 2)
                            .arg(p.minLocalSnrDb, 0, 'f', 2)
                        : QStringLiteral("选中峰局部SNR %1 dB，低于稳定LOCK软恢复下限 %2 dB")
                            .arg(result.snrDb, 0, 'f', 2)
                            .arg(p.lowSnrSoftMinDb, 0, 'f', 2)), true);
            return result;
        }
    }
    const double thickness = p.longitudinalVelocityMps
        * (selected + p.delayCount) / p.sampleRateHz / 2.0 * 1000.0 - CURRENT_FIXED_BIAS_MM;
    // 厚度公差不再参与 measurementStatus 判废。只要峰位/波包/SNR/跟踪等
    // 算法质量判据通过，就输出实际计算厚度；超出参考厚度仅属于工艺/评价结果。
    if (!std::isfinite(thickness)) {
        rejectWaveform(result, QStringLiteral("INVALID_THICKNESS"),
            QStringLiteral("选中峰无法换算为有限厚度"));
        return result;
    }
    result.peakIndex = integer;
    result.interpolatedPeakIndex = selected;
    result.subsampleOffset = selected - integer;
    result.peakTimeSec = selected / p.sampleRateHz;
    result.peakVoltage = result.fallbackPeakUsed ? selectedVoltage
        : ((result.lockedBroadPacketRecovered || result.lockedFragmentedPacketRecovered
            || result.lockedStraddlingPacketRecovered || result.lockedTemplateOnlyRecovered)
            ? signal[integer] : fit.value(selected));
    result.thicknessMm = thickness;
    result.longitudinalCandidate = true;
    result.longitudinalStatus = QStringLiteral("CONSISTENT");
    result.longitudinalEvidence = result.echoConfirmed
        ? QStringLiteral("FIRST_PACKET_PLUS_ECHO")
        : QStringLiteral("FIRST_PACKET_ONLY");
    result.measurementStatus = QStringLiteral("VALID");
    result.rejectionReason.clear();
    if (result.fallbackPeakUsed) {
        result.error = result.echoConfirmed
            ? QStringLiteral("首波包到时/SNR符合纵波判据；理论窗截断，已用传统单峰三点抛物线定位；后续回波辅助确认")
            : QStringLiteral("首波包到时/SNR符合纵波判据；理论窗截断，已用传统单峰三点抛物线定位；后续回波较弱且不否决");
    } else {
        result.error = result.echoConfirmed
            ? QStringLiteral("首波包到时/SNR/整包单峰拟合符合纵波判据；后续回波辅助确认")
            : QStringLiteral("首波包到时/SNR/整包单峰拟合符合纵波判据；后续回波较弱，仅作辅助且不否决");
    }
    if (result.trackingSoftRecovered)
        result.error += QStringLiteral("；连续相关低于硬门，但拟合中心与历史预测高度一致，已按软恢复规则使用拟合中心ToF");
    if (result.guardedReadOnlyRecovered)
        result.error += QStringLiteral("；本帧为低相关/低SNR边界只读恢复，不更新连续跟踪历史");
    if (result.lockedBroadPacketRecovered)
        result.error += QStringLiteral("；LOCK宽能量核心略超常规宽度，但模板相关/历史趋势/门内能量峰一致，已按宽包恢复规则使用模板ToF");
    if (result.lockedFragmentedPacketRecovered)
        result.error += QStringLiteral("；LOCK高阈值核心虽仅4~7点，已由低阈值支持区重建完整波包并通过RECENT高相关/预测残差复核，厚度使用模板相关ToF");
    if (result.lockedAmbiguousReadOnlyRecovered)
        result.error += QStringLiteral("；候选能量接近，但仅门内分支通过RECENT模板身份认证，本帧只读输出且不更新历史");
    if (result.trackingFitFallback)
        result.error += QStringLiteral("；连续相关峰不唯一，已由历史粗门内的整包拟合中心定位");
    result.trackingAnchor = integer;
    constexpr int templateHalf = 24;
    if (integer >= templateHalf && integer + templateHalf < n)
        result.trackingTemplate.assign(signal.begin() + integer - templateHalf,
                                        signal.begin() + integer + templateHalf + 1);
    return result;
}

WaveformResult runCurrentWaveformAlgorithm(
    const std::vector<double> &raw,
    const TestParameters &p,
    std::vector<double> *filteredOut = nullptr,
    const PredictionGate &prediction = {},
    AlgorithmWorkspace *workspace = nullptr)
{
    AlgorithmWorkspace localWorkspace;
    AlgorithmWorkspace &ws = workspace ? *workspace : localWorkspace;
    WaveformResult result;
    if (raw.size() < 3 || !std::all_of(raw.begin(), raw.end(),
            [](double value) { return std::isfinite(value); })) {
        rejectWaveform(result, QStringLiteral("INVALID_DATA"),
            QStringLiteral("原始波形为空、过短或包含非有限数据"));
        return result;
    }
    if (!std::isfinite(p.sampleRateHz) || !std::isfinite(p.lowFreqMHz)
        || !std::isfinite(p.highFreqMHz) || !std::isfinite(p.thicknessStdMm)
        || p.sampleRateHz <= 0.0 || p.lowFreqMHz <= 0.0
        || p.highFreqMHz <= p.lowFreqMHz || p.highFreqMHz * 1e6 >= p.sampleRateHz / 2.0
        || p.thicknessStdMm < 0.0
        || p.filterOrder > 1000 || p.structLen > 1000 || p.minWidth > 1000
        || (p.detectMode != "auto" && p.detectMode != "peak" && p.detectMode != "valley")) {
        rejectWaveform(result, QStringLiteral("INVALID_PARAMETERS"),
            QStringLiteral("采样率、带通频率、厚度或窗口参数无效；带通高频必须小于奈奎斯特频率"));
        return result;
    }

    const int n = static_cast<int>(raw.size());
    const int energyHalf = p.energyWindowSamples / 2;

    // 三阶段窗口：搜索位置不再由参考厚度thicknessStdMm决定。
    // GLOBAL_SEARCH使用首波物理ROI；WARMUP围绕已确认身份使用较宽局部门；
    // 稳定后PREDICTION才收窄到历史中心±predictionHalfWidth。LOST则从冻结身份中心
    // 渐进扩窗，失败越久窗口越宽，避免一失锁就重新在全局ROI竞争错误波包。
    // 当前物理ROI左端避开FIR边缘，右端利用采集时序delayCount留在首波区，避免把
    // Echo2/Echo3直接纳入启动候选竞争。后续可将该物理ROI暴露为现场参数。
    const int globalLeft = std::clamp(
        std::max(energyHalf, std::max(1, p.filterOrder / 2)),
        energyHalf, n - 1 - energyHalf);
    const int delayDerivedRight = p.delayCount > 0
        ? p.delayCount + 8 * p.predictionHalfWidth
        : n / 4;
    const int minimumGlobalSpan = std::max(160, 10 * p.predictionHalfWidth);
    const int globalRight = std::clamp(
        std::max(globalLeft + minimumGlobalSpan, delayDerivedRight),
        globalLeft + 2, n - 1 - energyHalf);
    if (globalLeft + 2 > globalRight) {
        rejectWaveform(result, QStringLiteral("INVALID_WINDOW"),
            QStringLiteral("首波物理全局搜索ROI无有效范围"));
        return result;
    }

    const int localSearchHalf = std::max(
        1, static_cast<int>(std::floor(p.sampleRateHz / 80000000.0 * 27.0)));
    // 旧160 MHz稳定数据中最终TOF约78~82点，而固定搜索窗为38:146，
    // 因此相对真实TOF的等效几何约为前42/后66点。用0.75*predictionHalfWidth
    // 表达该12点偏移；Session Window建立后固定，不随逐帧prediction抖动。
    const int packetGeometryOffset = std::clamp(
        static_cast<int>(std::llround(0.75 * p.predictionHalfWidth)),
        0, localSearchHalf - energyHalf);
    const int localLeftMargin = std::max(energyHalf,
                                         localSearchHalf - packetGeometryOffset);
    const int localRightMargin = localSearchHalf + packetGeometryOffset;
    result.predictionHistoryCount = prediction.historyCount;
    result.predictedPeakIndex = prediction.center;
    const bool hasPredictionCenter = std::isfinite(prediction.center);

    // Discovery ROI只用于粗定位；真正的searchLeft/searchRight会在滤波后根据粗位置
    // 重新收回到约±54点的Local Search Window，保证noise floor和packet分割不被全局窗污染。
    int discoveryLeft = globalLeft;
    int discoveryRight = globalRight;
    switch (prediction.windowStage) {
    case SearchWindowStage::Prediction:
        result.searchWindowStage = QStringLiteral("PREDICTION");
        result.trackingState = QStringLiteral("LOCK");
        if (std::isfinite(prediction.searchWindowAnchor)) {
            discoveryLeft = std::max(globalLeft,
                static_cast<int>(std::floor(prediction.searchWindowAnchor - localLeftMargin)));
            discoveryRight = std::min(globalRight,
                static_cast<int>(std::ceil(prediction.searchWindowAnchor + localRightMargin)));
        }
        break;
    case SearchWindowStage::Warmup:
        result.searchWindowStage = QStringLiteral("WARMUP");
        result.trackingState = QStringLiteral("WARMUP");
        if (std::isfinite(prediction.searchWindowAnchor)) {
            discoveryLeft = std::max(globalLeft,
                static_cast<int>(std::floor(prediction.searchWindowAnchor - localLeftMargin)));
            discoveryRight = std::min(globalRight,
                static_cast<int>(std::ceil(prediction.searchWindowAnchor + localRightMargin)));
        }
        break;
    case SearchWindowStage::GlobalReacquire:
        result.searchWindowStage = prediction.longTermIdentityAvailable
            ? QStringLiteral("GLOBAL_REACQUIRE")
            : QStringLiteral("REACQUIRE_EXPANDED");
        result.trackingState = QStringLiteral("LOST");
        if (!prediction.longTermIdentityAvailable
            && std::isfinite(prediction.longTermIdentityCenter)) {
            // LOST前期仍围绕冻结位置渐进扩窗，不允许全局最强峰立即接管身份。
            const int baseReacquireHalfWidth = std::max(40, 2 * p.predictionHalfWidth);
            const int expandedHalfWidth = std::min(
                globalRight - globalLeft,
                baseReacquireHalfWidth + 3 * std::max(0, prediction.missCount));
            discoveryLeft = std::max(globalLeft,
                static_cast<int>(std::floor(
                    prediction.longTermIdentityCenter - expandedHalfWidth)));
            discoveryRight = std::min(globalRight,
                static_cast<int>(std::ceil(
                    prediction.longTermIdentityCenter + expandedHalfWidth)));
        }
        // 只有冻结身份模板已正式开放时，才把Discovery放回整个物理ROI；
        // 后续候选仍必须通过LOST_CONFIRMED/EMA模板身份认证。
        break;
    case SearchWindowStage::GlobalSearch:
    default:
        result.searchWindowStage = QStringLiteral("GLOBAL_SEARCH");
        result.trackingState = QStringLiteral("SEARCH");
        break;
    }
    discoveryLeft = std::max(discoveryLeft, energyHalf);
    discoveryRight = std::min(discoveryRight, n - 1 - energyHalf);
    result.searchLeftIndex = discoveryLeft;
    result.searchRightIndex = discoveryRight;
    result.predictionLeftIndex = discoveryLeft;
    result.predictionRightIndex = discoveryRight;
    result.predictionApplied = false;

    // 100 Hz实时路径：滤波参数不变时复用FIR系数和滤波输出缓冲。
    // 缓存属于显式AlgorithmWorkspace，任务结束前正常析构，不依赖TLS生命周期。
    if (ws.filterOrder != p.filterOrder
        || ws.filterLowFreqMHz != p.lowFreqMHz
        || ws.filterHighFreqMHz != p.highFreqMHz
        || ws.filterSampleRateHz != p.sampleRateHz) {
        designBandpassFilter(p.lowFreqMHz * 1e6, p.highFreqMHz * 1e6,
                             p.sampleRateHz, p.filterOrder, ws.filterCoeffs);
        ws.filterLowFreqMHz = p.lowFreqMHz;
        ws.filterHighFreqMHz = p.highFreqMHz;
        ws.filterSampleRateHz = p.sampleRateHz;
        ws.filterOrder = p.filterOrder;
    }
    std::vector<double> &filtered = ws.filtered;
    applyBandpassFilter(raw, filtered, ws.filterCoeffs, ws);
    detrendSignal(filtered);
    if (filteredOut)
        *filteredOut = filtered;

    if (!std::all_of(filtered.begin(), filtered.end(),
            [](double value) { return std::isfinite(value); })) {
        rejectWaveform(result, QStringLiteral("INVALID_DATA"),
            QStringLiteral("滤波后出现非有限数据"));
        return result;
    }

    // 第一层：Discovery只用滤波后局部能量粗定位，不依赖参考厚度，也不参与最终SNR判据。
    double discoveryMaximumEnergy = 0.0;
    int discoveryMaxIndex = -1;
    for (int i = discoveryLeft; i <= discoveryRight; ++i) {
        const double energy = localEnergy(filtered, i, energyHalf);
        if (energy > discoveryMaximumEnergy) {
            discoveryMaximumEnergy = energy;
            discoveryMaxIndex = i;
        }
    }

    // GLOBAL_SEARCH左边界附近容易包含采集起始基线跃迁/滤波过渡。若全局最强点
    // 落在“无法向左容纳一个完整Local Search Window”的边缘区，不直接把它当首波。
    // 仅当更深处存在能量至少达到边缘峰20%的候选时才切换到内部候选；因此不使用
    // reference thickness，也不会因为普通弱噪声就覆盖真正靠左的物理首波。
    bool globalEdgeTransientSuppressed = false;
    int globalDiscoveryCompetitionLeft = discoveryLeft;
    if (prediction.windowStage == SearchWindowStage::GlobalSearch
        && discoveryMaxIndex >= 0) {
        const int interiorLeft = std::min(discoveryRight,
            globalLeft + std::max(localLeftMargin, p.filterOrder / 2));
        if (discoveryMaxIndex < interiorLeft && interiorLeft < discoveryRight) {
            double interiorMaximumEnergy = 0.0;
            int interiorMaxIndex = -1;
            for (int i = interiorLeft; i <= discoveryRight; ++i) {
                const double energy = localEnergy(filtered, i, energyHalf);
                if (energy > interiorMaximumEnergy) {
                    interiorMaximumEnergy = energy;
                    interiorMaxIndex = i;
                }
            }
            constexpr double minimumInteriorEnergyFraction = 0.20;
            if (interiorMaxIndex >= 0
                && interiorMaximumEnergy >= minimumInteriorEnergyFraction * discoveryMaximumEnergy) {
                discoveryMaximumEnergy = interiorMaximumEnergy;
                discoveryMaxIndex = interiorMaxIndex;
                globalEdgeTransientSuppressed = true;
                globalDiscoveryCompetitionLeft = interiorLeft;
            }
        }
    }

    // 冷启动全局搜索不能只围绕“最强点”立即裁窗，否则两个相距较远、能量接近的
    // 首波候选会被先验裁掉一个，绕过后续AMBIGUOUS_PACKETS安全门。额外寻找一个
    // 与最强点相隔至少2*candidateSeparation的竞争峰；只有能量比已达到原独立候选
    // 歧义标准时，才把Local Search Window中心放到两者中点，让正常packet链同时评估。
    int discoverySecondIndex = -1;
    double discoverySecondEnergy = 0.0;
    bool globalDiscoveryAmbiguous = false;
    if (prediction.windowStage == SearchWindowStage::GlobalSearch && discoveryMaxIndex >= 0) {
        const int exclusion = std::max(2 * p.candidateSeparationSamples,
                                       p.packetMaxWidth / 2);
        for (int i = globalDiscoveryCompetitionLeft; i <= discoveryRight; ++i) {
            // 已被识别为启动边缘瞬态的区域不能再次以“第二候选”身份回流，
            // 否则主候选与瞬态取中点仍会把真正首波裁在Local Window边缘。
            if (globalEdgeTransientSuppressed && i < globalDiscoveryCompetitionLeft)
                continue;
            if (std::abs(i - discoveryMaxIndex) < exclusion)
                continue;
            const double energy = localEnergy(filtered, i, energyHalf);
            if (energy > discoverySecondEnergy) {
                discoverySecondEnergy = energy;
                discoverySecondIndex = i;
            }
        }
        globalDiscoveryAmbiguous = discoverySecondIndex >= 0
            && discoverySecondEnergy > 1e-24
            && discoveryMaximumEnergy / discoverySecondEnergy < p.minCandidateRatio;
    }

    double localCenter = NaN;
    switch (prediction.windowStage) {
    case SearchWindowStage::Prediction:
        localCenter = std::isfinite(prediction.searchWindowAnchor)
            ? prediction.searchWindowAnchor
            : (hasPredictionCenter ? prediction.center : double(discoveryMaxIndex));
        break;
    case SearchWindowStage::Warmup:
        localCenter = std::isfinite(prediction.searchWindowAnchor)
            ? prediction.searchWindowAnchor
            : (hasPredictionCenter ? prediction.center : double(discoveryMaxIndex));
        break;
    case SearchWindowStage::GlobalReacquire:
        if (prediction.longTermIdentityAvailable && discoveryMaxIndex >= 0)
            localCenter = discoveryMaxIndex;
        else if (std::isfinite(prediction.longTermIdentityCenter))
            localCenter = prediction.longTermIdentityCenter;
        else if (std::isfinite(prediction.searchWindowAnchor))
            localCenter = prediction.searchWindowAnchor;
        else
            localCenter = discoveryMaxIndex;
        break;
    case SearchWindowStage::GlobalSearch:
    default:
        localCenter = globalDiscoveryAmbiguous
            ? 0.5 * (discoveryMaxIndex + discoverySecondIndex)
            : double(discoveryMaxIndex);
        break;
    }

    // 第二层：Local Search Window保持与旧理论窗近似相同的±54点宽度，但中心来自
    // Discovery/历史身份，而不是reference thickness。Raw污染、noise floor和packet分割
    // 全部只在这个局部窗内运行，避免全局ROI改变成熟算法的统计基线。
    if (std::isfinite(localCenter)) {
        // 首波局部窗采用与旧稳定算法等效的非对称几何，但不再依赖参考厚度中心。
        // 默认160 MHz / predictionHalfWidth=16时得到前42/后66点，总宽度与旧±54窗一致。
        result.searchLeftIndex = std::max(globalLeft,
            static_cast<int>(std::floor(localCenter - localLeftMargin)));
        result.searchRightIndex = std::min(globalRight,
            static_cast<int>(std::ceil(localCenter + localRightMargin)));
    } else {
        result.searchLeftIndex = discoveryLeft;
        result.searchRightIndex = discoveryRight;
    }

    result.predictionLeftIndex = result.searchLeftIndex;
    result.predictionRightIndex = result.searchRightIndex;
    result.predictionApplied = false;
    if (prediction.windowStage == SearchWindowStage::Prediction && hasPredictionCenter) {
        // 正常LOCK用±predictionHalfWidth；出现连续miss后每帧扩大8点，优先在保持
        // RECENT身份的前提下追上真实位移，而不是直接清空历史进入LOST重学。
        const int expandedPredictionHalfWidth = std::min(
            localSearchHalf,
            p.predictionHalfWidth + 8 * std::max(0, prediction.missCount));
        result.predictionLeftIndex = std::max(result.searchLeftIndex,
            static_cast<int>(std::ceil(prediction.center - expandedPredictionHalfWidth)));
        result.predictionRightIndex = std::min(result.searchRightIndex,
            static_cast<int>(std::floor(prediction.center + expandedPredictionHalfWidth)));
        result.predictionApplied = true;
    } else if (prediction.windowStage == SearchWindowStage::Warmup
               && std::isfinite(localCenter)) {
        constexpr int warmupCandidateHalfWidth = 32;
        const double warmupGateCenter = hasPredictionCenter
            ? prediction.center : (discoveryMaxIndex >= 0 ? double(discoveryMaxIndex) : localCenter);
        result.predictionLeftIndex = std::max(result.searchLeftIndex,
            static_cast<int>(std::floor(warmupGateCenter - warmupCandidateHalfWidth)));
        result.predictionRightIndex = std::min(result.searchRightIndex,
            static_cast<int>(std::ceil(warmupGateCenter + warmupCandidateHalfWidth)));
        result.predictionApplied = true;
    } else if (prediction.windowStage == SearchWindowStage::GlobalReacquire
               && std::isfinite(localCenter)) {
        const int reacquireCandidateHalfWidth = prediction.longTermIdentityAvailable
            ? 40
            : std::min(localSearchHalf,
                       p.predictionHalfWidth + 4 * std::max(1, prediction.missCount));
        result.predictionLeftIndex = std::max(result.searchLeftIndex,
            static_cast<int>(std::floor(localCenter - reacquireCandidateHalfWidth)));
        result.predictionRightIndex = std::min(result.searchRightIndex,
            static_cast<int>(std::ceil(localCenter + reacquireCandidateHalfWidth)));
        result.predictionApplied = true;
    }
    result.predictionLeftIndex = std::max(result.predictionLeftIndex, energyHalf);
    result.predictionRightIndex = std::min(result.predictionRightIndex, n - 1 - energyHalf);

    // Raw污染与噪声估计现在都基于Local Search Window，而不是全局Discovery ROI。
    const std::vector<bool> &contaminated = detectRawImpulses(raw, p, result, ws);
    if (result.firstContaminatedIndex >= 0)
        return result;
    if (!estimateNoiseEnergy(filtered, contaminated, p, result, prediction, ws))
        return result;

    double maximumEnergy = 0.0;
    double predictionGateMaximumEnergy = 0.0;
    result.predictionGateMaxIndex = -1;
    for (int i = std::max(result.searchLeftIndex, energyHalf);
         i <= std::min(result.searchRightIndex, n - 1 - energyHalf); ++i) {
        const double energy = localEnergy(filtered, i, energyHalf);
        maximumEnergy = std::max(maximumEnergy, energy);
        if (i >= result.predictionLeftIndex && i <= result.predictionRightIndex
            && energy > predictionGateMaximumEnergy) {
            predictionGateMaximumEnergy = energy;
            result.predictionGateMaxIndex = i;
        }
    }
    result.maxLocalSnrDb = energySnrDb(maximumEnergy, result.noiseEnergy);
    result.predictionGateMaxLocalSnrDb = energySnrDb(
        predictionGateMaximumEnergy, result.noiseEnergy);
    if (result.predictionApplied) {
        const int predictedCenter = static_cast<int>(std::round(result.predictedPeakIndex));
        if (predictedCenter >= energyHalf && predictedCenter < n - energyHalf)
            result.predictedCenterLocalSnrDb = energySnrDb(
                localEnergy(filtered, predictedCenter, energyHalf), result.noiseEnergy);
    }
    const bool stableLockForLowSnr = prediction.lockActive
        && !prediction.reacquireRequired
        && !prediction.templateValues.empty();
    const double searchSnrFloor = stableLockForLowSnr
        ? p.lowSnrSoftMinDb
        : (prediction.windowStage == SearchWindowStage::GlobalSearch
            ? p.minLocalSnrDb - 0.25 : p.minLocalSnrDb);
    // 一旦Discovery已经生成局部Candidate Gate，信号存在性也以该局部门为准；
    // 不能让远处强能量团把全局maxLocalSNR抬高后误导当前身份分支。
    const double signalPresenceSnrDb = result.predictionApplied
        ? result.predictionGateMaxLocalSnrDb : result.maxLocalSnrDb;
    const double signalPresenceEnergy = result.predictionApplied
        ? predictionGateMaximumEnergy : maximumEnergy;
    if (stableLockForLowSnr
        && std::isfinite(signalPresenceSnrDb)
        && signalPresenceSnrDb >= p.lowSnrSoftMinDb
        && signalPresenceSnrDb < p.minLocalSnrDb)
        result.lowSnrSoftCandidate = true;
    if (signalPresenceEnergy <= 1e-24 || signalPresenceSnrDb < searchSnrFloor) {
        result.snrDb = signalPresenceSnrDb;
        rejectWaveform(result, QStringLiteral("NO_SIGNAL"),
            stableLockForLowSnr
                ? QStringLiteral("当前候选门最大局部SNR %1 dB，低于软恢复下限 %2 dB（普通硬门%3 dB）")
                    .arg(signalPresenceSnrDb, 0, 'f', 2)
                    .arg(p.lowSnrSoftMinDb, 0, 'f', 2)
                    .arg(p.minLocalSnrDb, 0, 'f', 2)
                : QStringLiteral("当前候选门最大局部能量SNR %1 dB，低于门限 %2 dB")
                    .arg(signalPresenceSnrDb, 0, 'f', 2).arg(p.minLocalSnrDb, 0, 'f', 2), true);
        return result;
    }
    if (result.predictionLeftIndex + 2 > result.predictionRightIndex) {
        rejectWaveform(result, QStringLiteral("PREDICTION_GATE"),
            QStringLiteral("预测门与有效搜索范围没有足够交集"));
        return result;
    }
    return detectLongitudinalPacket(filtered, contaminated, p, result, prediction, ws);
}

// -----------------------------------------------------------------------------
// 当前ProcessAnalyzer频谱逻辑的离线复制
// -----------------------------------------------------------------------------

int nextPowerOfTwo(int value)
{
    int n = 1;
    while (n < value)
        n <<= 1;
    return n;
}

void fft(std::vector<std::complex<double>> &a)
{
    const int n =
        static_cast<int>(a.size());

    for (int i = 1, j = 0;
         i < n;
         ++i) {

        int bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap(a[i], a[j]);
    }

    for (int len = 2;
         len <= n;
         len <<= 1) {

        const double angle =
            -2.0 * PI / len;

        const std::complex<double> wLen(
            std::cos(angle),
            std::sin(angle));

        for (int i = 0;
             i < n;
             i += len) {

            std::complex<double> w(
                1.0,
                0.0);

            for (int j = 0;
                 j < len / 2;
                 ++j) {

                const auto u =
                    a[i + j];

                const auto v =
                    a[i + j + len / 2] *
                    w;

                a[i + j] = u + v;
                a[i + j + len / 2] =
                    u - v;

                w *= wLen;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 最终高精度TOF输出层：Hilbert包络 + 宽packet二次分解 + t50 + Envelope NCC
// + LeadingEdge三证据质量判别。该层只接管最终厚度，不改变上面的候选检测、LOCK/LOST
// 和主算法身份链。
// -----------------------------------------------------------------------------

struct HighPrecisionFftPlan
{
    int size = 0;
    std::vector<int> bitReverse;
    std::vector<std::vector<std::complex<double>>> stageTwiddles;

    void prepare(int n)
    {
        if (size == n)
            return;
        size = n;
        bitReverse.assign(static_cast<size_t>(n), 0);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i) {
            unsigned value = static_cast<unsigned>(i);
            unsigned reversed = 0;
            for (int b = 0; b < bits; ++b) {
                reversed = (reversed << 1U) | (value & 1U);
                value >>= 1U;
            }
            bitReverse[static_cast<size_t>(i)] = static_cast<int>(reversed);
        }

        stageTwiddles.clear();
        stageTwiddles.reserve(static_cast<size_t>(bits));
        for (int len = 2; len <= n; len <<= 1) {
            const double angle = -2.0 * PI / len;
            const std::complex<double> wLen(std::cos(angle), std::sin(angle));
            std::vector<std::complex<double>> twiddles(static_cast<size_t>(len / 2));
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                twiddles[static_cast<size_t>(j)] = w;
                w *= wLen;
            }
            stageTwiddles.push_back(std::move(twiddles));
        }
    }
};

void highPrecisionFft(std::vector<std::complex<double>> &a,
                      HighPrecisionFftPlan &plan)
{
    const int n = static_cast<int>(a.size());
    plan.prepare(n);
    for (int i = 1; i < n; ++i) {
        const int j = plan.bitReverse[static_cast<size_t>(i)];
        if (i < j)
            std::swap(a[static_cast<size_t>(i)], a[static_cast<size_t>(j)]);
    }

    size_t stageIndex = 0;
    for (int len = 2; len <= n; len <<= 1, ++stageIndex) {
        const auto &twiddles = plan.stageTwiddles[stageIndex];
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < len / 2; ++j) {
                const size_t i0 = static_cast<size_t>(i + j);
                const size_t i1 = static_cast<size_t>(i + j + len / 2);
                const double ur = a[i0].real();
                const double ui = a[i0].imag();
                const double br = a[i1].real();
                const double bi = a[i1].imag();
                const double wr = twiddles[static_cast<size_t>(j)].real();
                const double wi = twiddles[static_cast<size_t>(j)].imag();
                const double vr = br * wr - bi * wi;
                const double vi = br * wi + bi * wr;
                a[i0] = std::complex<double>(ur + vr, ui + vi);
                a[i1] = std::complex<double>(ur - vr, ui - vi);
            }
        }
    }
}

struct HighPrecisionHistory
{
    int sampleCount = 0;
    std::vector<double> envelopeTemplate;
    HighPrecisionFftPlan hilbertFftPlan;
    std::vector<std::complex<double>> hilbertSpectrum;
    std::vector<double> hilbertRawEnvelope;
    std::vector<double> hilbertEnvelope;
    std::vector<double> hilbertCorrected;
    std::vector<double> hilbertComponentEnvelope;
    std::vector<double> hilbertActivity;
    std::vector<double> hilbertSmoothed;
    std::vector<double> medianScratch;
    std::vector<double> templateScratch;
    // 多回波周期链只学习与绝对位置无关的量：差分周期→厚度比例、
    // 绝对/差分估计器残差，以及NCC-LeadingEdge包内几何。
    std::vector<double> echoPeriodThicknessScaleHistory;
    std::vector<double> echoAbsoluteResidualHistory;
    std::vector<double> packetInternalDelayHistory;

    void prepare(int count)
    {
        if (sampleCount != count) {
            envelopeTemplate.clear();
            echoPeriodThicknessScaleHistory.clear();
            echoAbsoluteResidualHistory.clear();
            packetInternalDelayHistory.clear();
        }
        sampleCount = count;
    }
};

struct StartupProvisionalState
{
    bool pending = false;
    int clock = 0;
    double anchorTof = NaN;
    double peakAnchorTof = NaN;
    int packetLeft = -1;
    int packetRight = -1;
    std::vector<double> envelopeTemplate;
    std::vector<double> peakEnvelopeTemplate;
    std::vector<double> correctedEnvelope;
    std::vector<double> filteredWaveform;
    WaveformResult waveform;

    void reset()
    {
        pending = false;
        clock = 0;
        anchorTof = NaN;
        peakAnchorTof = NaN;
        packetLeft = -1;
        packetRight = -1;
        envelopeTemplate.clear();
        peakEnvelopeTemplate.clear();
        correctedEnvelope.clear();
        filteredWaveform.clear();
        waveform = WaveformResult{};
    }
};

struct StartupEnvelopeObservation
{
    bool valid = false;
    double t50Tof = NaN;
    double peakTof = NaN;
    int packetLeft = -1;
    int packetRight = -1;
    std::vector<double> envelopeTemplate;
    std::vector<double> peakEnvelopeTemplate;
};

struct HighPrecisionDecision
{
    bool allowed = false;
    bool usedMainFallback = false;
    bool nccAvailable = false;
    bool t50Available = false;
    bool positiveCommonTimeShiftCorrected = false;
    double tofIndex = NaN;
    double thicknessMm = NaN;
    double envelopeNccCorrelation = NaN;
    double nccT50DeltaSamples = NaN;
    double echoChainPeriodDifferenceSamples = NaN;
    double echoChainResidualDeviationMm = NaN;
};

bool highPrecisionHilbertEnvelope(const std::vector<double> &signal,
                                  HighPrecisionHistory *history,
                                  std::vector<double> &envelope)
{
    const int n = static_cast<int>(signal.size());
    if (n < 3)
        return false;

    HighPrecisionFftPlan localPlan;
    std::vector<std::complex<double>> localSpectrum;
    HighPrecisionFftPlan &plan = history ? history->hilbertFftPlan : localPlan;
    std::vector<std::complex<double>> &spectrum = history
        ? history->hilbertSpectrum : localSpectrum;

    const int fftCount = nextPowerOfTwo(3 * n);
    spectrum.resize(static_cast<size_t>(fftCount));
    for (int i = 0; i < n; ++i) {
        spectrum[static_cast<size_t>(i)] = signal[static_cast<size_t>(n - 1 - i)];
        spectrum[static_cast<size_t>(n + i)] = signal[static_cast<size_t>(i)];
        spectrum[static_cast<size_t>(2 * n + i)] = signal[static_cast<size_t>(n - 1 - i)];
    }
    std::fill(spectrum.begin() + 3 * n, spectrum.end(), std::complex<double>(0.0, 0.0));

    highPrecisionFft(spectrum, plan);
    const int half = fftCount / 2;
    for (int k = 1; k < half; ++k)
        spectrum[static_cast<size_t>(k)] *= 2.0;
    for (int k = half + 1; k < fftCount; ++k)
        spectrum[static_cast<size_t>(k)] = std::complex<double>(0.0, 0.0);
    for (auto &v : spectrum)
        v = std::conj(v);
    highPrecisionFft(spectrum, plan);
    for (auto &v : spectrum)
        v = std::conj(v) / static_cast<double>(fftCount);

    // 直接把Hilbert幅值和原有三点平滑合并为一次顺序扫描，避免额外rawEnvelope整帧写回/读取。
    envelope.resize(static_cast<size_t>(n));
    double previous = std::abs(spectrum[static_cast<size_t>(n)]);
    double current = std::abs(spectrum[static_cast<size_t>(n + 1)]);
    envelope[0] = (previous + current) / 2.0;
    for (int i = 1; i + 1 < n; ++i) {
        const double next = std::abs(spectrum[static_cast<size_t>(n + i + 1)]);
        double sum = previous;
        sum += current;
        sum += next;
        envelope[static_cast<size_t>(i)] = sum / 3.0;
        previous = current;
        current = next;
    }
    envelope[static_cast<size_t>(n - 1)] = (previous + current) / 2.0;
    return true;
}

void highPrecisionSmoothThreePoint(const std::vector<double> &source,
                                   std::vector<double> &dest)
{
    const int n = static_cast<int>(source.size());
    dest.resize(source.size());
    if (n == 0)
        return;
    if (n == 1) {
        dest[0] = source[0];
        return;
    }
    dest[0] = (source[0] + source[1]) / 2.0;
    for (int i = 1; i + 1 < n; ++i) {
        double sum = source[static_cast<size_t>(i - 1)];
        sum += source[static_cast<size_t>(i)];
        sum += source[static_cast<size_t>(i + 1)];
        dest[static_cast<size_t>(i)] = sum / 3.0;
    }
    dest[static_cast<size_t>(n - 1)] =
        (source[static_cast<size_t>(n - 2)] + source[static_cast<size_t>(n - 1)]) / 2.0;
}

double highPrecisionSampleLinear(const std::vector<double> &values, double x)
{
    if (values.empty() || !std::isfinite(x))
        return NaN;
    if (x <= 0.0)
        return values.front();
    const double maxIndex = static_cast<double>(values.size() - 1);
    if (x >= maxIndex)
        return values.back();
    const int i0 = static_cast<int>(std::floor(x));
    const int i1 = i0 + 1;
    const double f = x - i0;
    return values[static_cast<size_t>(i0)] * (1.0 - f)
        + values[static_cast<size_t>(i1)] * f;
}

double highPrecisionInterpolateCrossing(double x0, double y0,
                                        double x1, double y1, double target)
{
    const double dy = y1 - y0;
    if (!std::isfinite(dy) || std::abs(dy) <= 1e-18)
        return x0;
    return x0 + std::clamp((target - y0) / dy, 0.0, 1.0) * (x1 - x0);
}

bool highPrecisionExtractTemplate(const std::vector<double> &envelope,
                                  double center,
                                  int halfWidth,
                                  std::vector<double> &templ)
{
    templ.clear();
    if (!std::isfinite(center) || envelope.empty())
        return false;
    templ.resize(static_cast<size_t>(2 * halfWidth + 1));
    double energy = 0.0;
    int outIndex = 0;
    for (int k = -halfWidth; k <= halfWidth; ++k, ++outIndex) {
        const double v = std::max(0.0, highPrecisionSampleLinear(envelope, center + k));
        templ[static_cast<size_t>(outIndex)] = v;
        energy += v * v;
    }
    if (energy <= 1e-24) {
        templ.clear();
        return false;
    }
    const double scale = 1.0 / std::sqrt(energy);
    for (double &v : templ)
        v *= scale;
    return true;
}

bool computeStartupEnvelopeObservation(const std::vector<double> &filtered,
                                       const WaveformResult &r,
                                       HighPrecisionHistory &history,
                                       StartupEnvelopeObservation &out)
{
    out = StartupEnvelopeObservation{};
    if (filtered.size() < 9 || r.packetLeftIndex < 0
        || r.packetRightIndex <= r.packetLeftIndex)
        return false;

    auto &envelope = history.hilbertEnvelope;
    auto &corrected = history.hilbertCorrected;
    auto &scratch = history.medianScratch;
    if (!highPrecisionHilbertEnvelope(filtered, &history, envelope)
        || envelope.size() != filtered.size())
        return false;

    const int n = static_cast<int>(envelope.size());
    const int left = std::clamp(r.packetLeftIndex, 1, n - 2);
    const int right = std::clamp(r.packetRightIndex, left + 1, n - 2);
    scratch.clear();
    scratch.reserve(32);
    for (int i = std::max(0, left - 12); i < left; ++i)
        scratch.push_back(envelope[static_cast<size_t>(i)]);
    for (int i = right + 1; i <= std::min(n - 1, right + 12); ++i)
        scratch.push_back(envelope[static_cast<size_t>(i)]);
    const double baseline = scratch.empty() ? 0.0 : medianValueInPlace(scratch);

    corrected.resize(envelope.size());
    for (size_t i = 0; i < envelope.size(); ++i)
        corrected[i] = std::max(0.0, envelope[i] - baseline);

    int peak = left;
    for (int i = left + 1; i <= right; ++i)
        if (corrected[static_cast<size_t>(i)] > corrected[static_cast<size_t>(peak)])
            peak = i;
    const double peakValue = corrected[static_cast<size_t>(peak)];
    if (!std::isfinite(peakValue) || peakValue <= 1e-18)
        return false;

    double peakTof = static_cast<double>(peak);
    if (peak > left && peak < right) {
        const double yl = corrected[static_cast<size_t>(peak - 1)];
        const double yc = corrected[static_cast<size_t>(peak)];
        const double yr = corrected[static_cast<size_t>(peak + 1)];
        const double curvature = yl - 2.0 * yc + yr;
        if (std::isfinite(curvature) && curvature < -1e-18) {
            const double delta = std::clamp(0.5 * (yl - yr) / curvature, -0.5, 0.5);
            peakTof += delta;
        }
    }

    const double supportLevel = 0.10 * peakValue;
    int supportLeft = left;
    int supportRight = right;
    for (int i = peak - 1; i >= left; --i) {
        if (corrected[static_cast<size_t>(i)] <= supportLevel) {
            supportLeft = i;
            break;
        }
    }
    for (int i = peak + 1; i <= right; ++i) {
        if (corrected[static_cast<size_t>(i)] <= supportLevel) {
            supportRight = i;
            break;
        }
    }
    if (supportRight - supportLeft < 2)
        return false;

    double total = 0.0;
    for (int i = supportLeft + 1; i <= supportRight; ++i) {
        const double e0 = corrected[static_cast<size_t>(i - 1)]
            * corrected[static_cast<size_t>(i - 1)];
        const double e1 = corrected[static_cast<size_t>(i)]
            * corrected[static_cast<size_t>(i)];
        total += 0.5 * (e0 + e1);
    }
    if (!(total > 1e-24))
        return false;

    const double target = 0.5 * total;
    double cumulative = 0.0;
    double t50 = NaN;
    for (int i = supportLeft + 1; i <= supportRight; ++i) {
        const double e0 = corrected[static_cast<size_t>(i - 1)]
            * corrected[static_cast<size_t>(i - 1)];
        const double e1 = corrected[static_cast<size_t>(i)]
            * corrected[static_cast<size_t>(i)];
        const double next = cumulative + 0.5 * (e0 + e1);
        if (next >= target) {
            const double fraction = next > cumulative
                ? std::clamp((target - cumulative) / (next - cumulative), 0.0, 1.0)
                : 0.0;
            t50 = (i - 1) + fraction;
            break;
        }
        cumulative = next;
    }
    if (!std::isfinite(t50))
        return false;

    constexpr int templateHalfWidth = 20;
    if (!highPrecisionExtractTemplate(corrected, t50, templateHalfWidth,
                                      out.envelopeTemplate)
        || !highPrecisionExtractTemplate(corrected, peakTof, templateHalfWidth,
                                         out.peakEnvelopeTemplate))
        return false;
    out.valid = true;
    out.t50Tof = t50;
    out.peakTof = peakTof;
    out.packetLeft = left;
    out.packetRight = right;
    return true;
}

double startupEnvelopeNccTof(const std::vector<double> &templ,
                             const std::vector<double> &envelope,
                             int packetLeft, int packetRight,
                             double &correlation,
                             double guideCenter = NaN,
                             double guideHalfWidth = 0.0)
{
    correlation = NaN;
    if (templ.empty() || envelope.empty() || templ.size() % 2 == 0)
        return NaN;
    const int half = static_cast<int>(templ.size()) / 2;
    const int n = static_cast<int>(envelope.size());
    int left = std::max(packetLeft, half);
    int right = std::min(packetRight, n - 1 - half);
    if (std::isfinite(guideCenter) && guideHalfWidth > 0.0) {
        left = std::max(left, static_cast<int>(std::floor(guideCenter - guideHalfWidth)));
        right = std::min(right, static_cast<int>(std::ceil(guideCenter + guideHalfWidth)));
    }
    if (left > right)
        return NaN;

    int bestCenter = -1;
    double bestRho = -std::numeric_limits<double>::infinity();
    double previousRho = NaN;
    double bestLeftRho = NaN;
    double bestRightRho = NaN;
    for (int center = left; center <= right; ++center) {
        const double rho = packetCorrelation(
            templ.data(), envelope.data() + center - half, templ.size());
        const double finiteRho = std::isfinite(rho) ? rho : -1.0;
        if (finiteRho > bestRho) {
            bestRho = finiteRho;
            bestCenter = center;
            bestLeftRho = previousRho;
            bestRightRho = NaN;
        } else if (bestCenter == center - 1) {
            bestRightRho = finiteRho;
        }
        previousRho = finiteRho;
    }
    if (bestCenter < 0 || !std::isfinite(bestRho))
        return NaN;

    if (!std::isfinite(bestLeftRho) && bestCenter > left) {
        bestLeftRho = packetCorrelation(
            templ.data(), envelope.data() + bestCenter - 1 - half, templ.size());
    }
    if (!std::isfinite(bestRightRho) && bestCenter < right) {
        bestRightRho = packetCorrelation(
            templ.data(), envelope.data() + bestCenter + 1 - half, templ.size());
    }

    double delta = 0.0;
    if (std::isfinite(bestLeftRho) && std::isfinite(bestRightRho)) {
        const double curvature = bestLeftRho - 2.0 * bestRho + bestRightRho;
        if (std::isfinite(curvature) && curvature < -1e-12)
            delta = std::clamp(0.5 * (bestLeftRho - bestRightRho) / curvature,
                               -0.5, 0.5);
    }
    correlation = bestRho;
    return bestCenter + delta;
}

bool startupAverageTemplates(const std::vector<double> &first,
                             const std::vector<double> &second,
                             std::vector<double> &combined)
{
    if (first.empty() || first.size() != second.size())
        return false;
    combined.resize(first.size());
    double energy = 0.0;
    for (size_t i = 0; i < first.size(); ++i) {
        combined[i] = 0.5 * (first[i] + second[i]);
        energy += combined[i] * combined[i];
    }
    if (!(energy > 1e-24))
        return false;
    const double scale = 1.0 / std::sqrt(energy);
    for (double &value : combined)
        value *= scale;
    return true;
}

void promoteStartupWaveform(WaveformResult &r,
                            const std::vector<double> &filtered,
                            const TestParameters &p,
                            double tofIndex)
{
    const int n = static_cast<int>(filtered.size());
    const int integer = std::clamp(static_cast<int>(std::llround(tofIndex)), 0, n - 1);
    r.interpolatedPeakIndex = tofIndex;
    r.peakIndex = integer;
    r.subsampleOffset = tofIndex - integer;
    r.peakTimeSec = tofIndex / p.sampleRateHz;
    r.peakVoltage = filtered[static_cast<size_t>(integer)];
    r.thicknessMm = p.longitudinalVelocityMps
        * (tofIndex + p.delayCount) / p.sampleRateHz / 2.0 * 1000.0
        - CURRENT_FIXED_BIAS_MM;
    r.snrDb = std::isfinite(r.maxLocalSnrDb) ? r.maxLocalSnrDb : r.snrDb;
    r.startupSupportSnrDb = r.snrDb;
    r.startupSupportRecovered = true;
    r.startupRecovered = true;
    r.longitudinalCandidate = true;
    r.longitudinalStatus = QStringLiteral("CONSISTENT");
    r.longitudinalEvidence = r.echoConfirmed
        ? QStringLiteral("STARTUP_PROVISIONAL_ENVELOPE_PLUS_ECHO")
        : QStringLiteral("STARTUP_PROVISIONAL_ENVELOPE");
    r.measurementStatus = QStringLiteral("VALID");
    r.rejectionReason.clear();
    r.error = QStringLiteral("STARTUP_PROVISIONAL：强packet中心落谷，已由临时Hilbert包络身份恢复");
    r.trackingAnchor = integer;
    constexpr int trackingHalf = 24;
    r.trackingTemplate.clear();
    if (integer >= trackingHalf && integer + trackingHalf < n) {
        r.trackingTemplate.assign(filtered.begin() + integer - trackingHalf,
                                  filtered.begin() + integer + trackingHalf + 1);
    }
}

struct HighPrecisionComponent
{
    int left = -1;
    int right = -1;
    int peak = -1;
    double peakValue = NaN;
    double energy = 0.0;
    double energyFraction = NaN;
    double peakRatio = NaN;
};

struct HighPrecisionDecomposition
{
    std::vector<HighPrecisionComponent> components;
    bool decomposed = false;
    int selectedIndex = -1;
};

HighPrecisionDecomposition highPrecisionDecompose(
    const std::vector<double> &activity, int packetLeft, int packetRight)
{
    HighPrecisionDecomposition out;
    if (activity.empty() || packetLeft < 0 || packetRight <= packetLeft
        || packetRight >= static_cast<int>(activity.size()))
        return out;

    int globalPeak = packetLeft;
    double totalEnergy = 0.0;
    for (int i = packetLeft; i <= packetRight; ++i) {
        const double v = std::max(0.0, activity[static_cast<size_t>(i)]);
        totalEnergy += v * v;
        if (v > activity[static_cast<size_t>(globalPeak)])
            globalPeak = i;
    }
    const double globalPeakValue = activity[static_cast<size_t>(globalPeak)];
    if (!std::isfinite(globalPeakValue) || globalPeakValue <= 1e-18 || totalEnergy <= 1e-24)
        return out;

    const double coreLevel = 0.22 * globalPeakValue;
    std::vector<std::pair<int, int>> cores;
    int i = packetLeft;
    while (i <= packetRight) {
        while (i <= packetRight && activity[static_cast<size_t>(i)] < coreLevel)
            ++i;
        if (i > packetRight)
            break;
        const int begin = i;
        while (i <= packetRight && activity[static_cast<size_t>(i)] >= coreLevel)
            ++i;
        const int end = i - 1;
        if (!cores.empty() && begin - cores.back().second <= 2)
            cores.back().second = end;
        else
            cores.push_back({begin, end});
    }
    if (cores.empty())
        cores.push_back({packetLeft, packetRight});

    if (cores.size() == 1 && packetRight - packetLeft >= 12) {
        std::vector<int> localPeaks;
        for (int x = packetLeft + 1; x < packetRight; ++x) {
            const double y = activity[static_cast<size_t>(x)];
            if (y >= 0.30 * globalPeakValue
                && y >= activity[static_cast<size_t>(x - 1)]
                && y > activity[static_cast<size_t>(x + 1)]) {
                if (!localPeaks.empty() && x - localPeaks.back() < 4) {
                    if (y > activity[static_cast<size_t>(localPeaks.back())])
                        localPeaks.back() = x;
                } else {
                    localPeaks.push_back(x);
                }
            }
        }
        int bestValley = -1;
        double bestValleyRatio = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k + 1 < localPeaks.size(); ++k) {
            const int a = localPeaks[k];
            const int b = localPeaks[k + 1];
            if (b - a < 8)
                continue;
            int valley = a;
            for (int x = a + 1; x < b; ++x)
                if (activity[static_cast<size_t>(x)] < activity[static_cast<size_t>(valley)])
                    valley = x;
            const double weaker = std::min(activity[static_cast<size_t>(a)],
                                           activity[static_cast<size_t>(b)]);
            if (weaker <= 1e-18)
                continue;
            const double ratio = activity[static_cast<size_t>(valley)] / weaker;
            if (ratio <= 0.72 && ratio < bestValleyRatio) {
                bestValleyRatio = ratio;
                bestValley = valley;
            }
        }
        if (bestValley > packetLeft && bestValley < packetRight) {
            cores.clear();
            cores.push_back({packetLeft, bestValley});
            cores.push_back({bestValley + 1, packetRight});
        }
    }

    std::vector<int> peaks;
    for (const auto &core : cores) {
        int peak = core.first;
        for (int x = core.first + 1; x <= core.second; ++x)
            if (activity[static_cast<size_t>(x)] > activity[static_cast<size_t>(peak)])
                peak = x;
        peaks.push_back(peak);
    }
    std::vector<int> boundaries{packetLeft};
    for (size_t c = 0; c + 1 < peaks.size(); ++c) {
        int valley = peaks[c];
        for (int x = peaks[c] + 1; x < peaks[c + 1]; ++x)
            if (activity[static_cast<size_t>(x)] < activity[static_cast<size_t>(valley)])
                valley = x;
        boundaries.push_back(valley);
    }
    boundaries.push_back(packetRight);

    std::vector<int> significant;
    for (size_t c = 0; c < cores.size(); ++c) {
        HighPrecisionComponent component;
        component.peak = peaks[c];
        component.peakValue = activity[static_cast<size_t>(component.peak)];
        const int segmentLeft = boundaries[c];
        const int segmentRight = boundaries[c + 1];
        const double supportLevel = 0.08 * component.peakValue;
        component.left = segmentLeft;
        for (int x = component.peak - 1; x >= segmentLeft; --x) {
            if (activity[static_cast<size_t>(x)] <= supportLevel) {
                component.left = x;
                break;
            }
        }
        component.right = segmentRight;
        for (int x = component.peak + 1; x <= segmentRight; ++x) {
            if (activity[static_cast<size_t>(x)] <= supportLevel) {
                component.right = x;
                break;
            }
        }
        for (int x = component.left; x <= component.right; ++x) {
            const double v = activity[static_cast<size_t>(x)];
            component.energy += v * v;
        }
        component.energyFraction = component.energy / totalEnergy;
        component.peakRatio = component.peakValue / globalPeakValue;
        const int index = static_cast<int>(out.components.size());
        out.components.push_back(component);
        if (component.right - component.left + 1 >= 4
            && component.peakRatio >= 0.35
            && component.energyFraction >= 0.10)
            significant.push_back(index);
    }

    if (significant.size() >= 2) {
        for (size_t a = 0; a + 1 < significant.size() && !out.decomposed; ++a)
            for (size_t b = a + 1; b < significant.size(); ++b)
                if (std::abs(out.components[static_cast<size_t>(significant[a])].peak
                             - out.components[static_cast<size_t>(significant[b])].peak) >= 8) {
                    out.decomposed = true;
                    break;
                }
    }

    if (out.decomposed) {
        out.selectedIndex = *std::min_element(significant.begin(), significant.end(),
            [&](int lhs, int rhs) {
                return out.components[static_cast<size_t>(lhs)].peak
                    < out.components[static_cast<size_t>(rhs)].peak;
            });
    } else {
        for (int c = 0; c < static_cast<int>(out.components.size()); ++c) {
            const auto &component = out.components[static_cast<size_t>(c)];
            if (globalPeak >= component.left && globalPeak <= component.right) {
                out.selectedIndex = c;
                break;
            }
        }
    }
    if (out.selectedIndex < 0 && !out.components.empty())
        out.selectedIndex = 0;
    return out;
}

HighPrecisionDecision computeHighPrecisionDecision(
    const std::vector<double> &filtered,
    const WaveformResult &r,
    const TestParameters &p,
    HighPrecisionHistory *history)
{
    HighPrecisionDecision decision;
    if (filtered.size() < 9 || r.packetLeftIndex < 0 || r.packetRightIndex < r.packetLeftIndex)
        return decision;

    std::vector<double> localEnvelope;
    std::vector<double> localCorrected;
    std::vector<double> localComponentEnvelope;
    std::vector<double> localActivity;
    std::vector<double> localSmoothed;
    std::vector<double> localScratch;
    std::vector<double> localTemplateScratch;
    std::vector<double> &envelope = history ? history->hilbertEnvelope : localEnvelope;
    std::vector<double> &corrected = history ? history->hilbertCorrected : localCorrected;
    std::vector<double> &componentBuffer = history
        ? history->hilbertComponentEnvelope : localComponentEnvelope;
    std::vector<double> &activity = history ? history->hilbertActivity : localActivity;
    std::vector<double> &smoothed = history ? history->hilbertSmoothed : localSmoothed;
    std::vector<double> &scratch = history ? history->medianScratch : localScratch;
    std::vector<double> &templateScratch = history ? history->templateScratch : localTemplateScratch;

    const auto medianReusable = [&](const std::vector<double> &values) {
        scratch.assign(values.begin(), values.end());
        return scratch.empty() ? NaN : medianValueInPlace(scratch);
    };

    if (!highPrecisionHilbertEnvelope(filtered, history, envelope)
        || envelope.size() != filtered.size())
        return decision;

    const int n = static_cast<int>(envelope.size());
    const int left = std::clamp(r.packetLeftIndex, 1, n - 2);
    const int right = std::clamp(r.packetRightIndex, left + 1, n - 2);
    scratch.clear();
    scratch.reserve(64);
    for (int i = std::max(0, left - 12); i < left; ++i)
        scratch.push_back(envelope[static_cast<size_t>(i)]);
    for (int i = right + 1; i <= std::min(n - 1, right + 12); ++i)
        scratch.push_back(envelope[static_cast<size_t>(i)]);
    const double baseline = scratch.empty() ? 0.0 : medianValueInPlace(scratch);
    corrected.resize(envelope.size());
    for (size_t i = 0; i < envelope.size(); ++i)
        corrected[i] = std::max(0.0, envelope[i] - baseline);

    int componentLeft = left;
    int componentRight = right;
    const int packetSpan = right - left + 1;
    const bool secondaryEligible = packetSpan >= 36
        && (r.lockedBroadPacketCandidate || (std::isfinite(r.fitR2) && r.fitR2 < 0.95));
    if (secondaryEligible) {
        const int half = std::clamp(p.energyWindowSamples / 2, 4, 10);
        scratch.clear();
        for (int i = std::max(half, left - 12); i < left; ++i) {
            const double e = localEnergy(filtered, i, half);
            if (std::isfinite(e)) scratch.push_back(e);
        }
        for (int i = right + 1; i <= std::min(n - 1 - half, right + 12); ++i) {
            const double e = localEnergy(filtered, i, half);
            if (std::isfinite(e)) scratch.push_back(e);
        }
        const double noiseEnergy = scratch.empty() ? 0.0 : medianValueInPlace(scratch);
        activity.assign(corrected.size(), 0.0);
        for (int i = std::max(left, half); i <= std::min(right, n - 1 - half); ++i) {
            const double e = localEnergy(filtered, i, half);
            if (std::isfinite(e))
                activity[static_cast<size_t>(i)] = std::sqrt(std::max(0.0, e - noiseEnergy));
        }
        highPrecisionSmoothThreePoint(activity, smoothed);
        auto decomposition = highPrecisionDecompose(smoothed, left, right);
        if (decomposition.selectedIndex < 0)
            decomposition = highPrecisionDecompose(corrected, left, right);
        if (decomposition.decomposed && decomposition.selectedIndex >= 0) {
            const auto &component = decomposition.components[
                static_cast<size_t>(decomposition.selectedIndex)];
            const double minPeakRatio = r.lockedBroadPacketCandidate ? 0.55 : 0.75;
            if (component.energyFraction >= 0.20
                && component.peakRatio >= minPeakRatio && component.peakRatio < 0.995) {
                componentLeft = std::clamp(component.left, left, right - 1);
                componentRight = std::clamp(component.right, componentLeft + 1, right);
            }
        }
    }

    int peak = componentLeft;
    for (int i = componentLeft + 1; i <= componentRight; ++i)
        if (corrected[static_cast<size_t>(i)] > corrected[static_cast<size_t>(peak)])
            peak = i;
    const double peakValue = corrected[static_cast<size_t>(peak)];
    if (!std::isfinite(peakValue) || peakValue <= 1e-18)
        return decision;

    const bool componentMasked = componentLeft != left || componentRight != right;
    if (componentMasked) {
        componentBuffer.assign(corrected.size(), 0.0);
        std::copy(corrected.begin() + componentLeft,
                  corrected.begin() + componentRight + 1,
                  componentBuffer.begin() + componentLeft);
    }
    const std::vector<double> &componentEnvelope = componentMasked ? componentBuffer : corrected;

    // t50：当前物理分量10%支撑区内的累计包络能量中位时刻。
    double t50 = NaN;
    const double supportLevel = 0.10 * peakValue;
    int supportLeft = componentLeft;
    int supportRight = componentRight;
    for (int i = peak - 1; i >= componentLeft; --i)
        if (componentEnvelope[static_cast<size_t>(i)] <= supportLevel) {
            supportLeft = i;
            break;
        }
    for (int i = peak + 1; i <= componentRight; ++i)
        if (componentEnvelope[static_cast<size_t>(i)] <= supportLevel) {
            supportRight = i;
            break;
        }
    if (supportRight - supportLeft >= 2) {
        double total = 0.0;
        for (int i = supportLeft + 1; i <= supportRight; ++i) {
            const double e0 = std::pow(componentEnvelope[static_cast<size_t>(i - 1)], 2.0);
            const double e1 = std::pow(componentEnvelope[static_cast<size_t>(i)], 2.0);
            total += 0.5 * (e0 + e1);
        }
        const double target = 0.5 * total;
        if (total > 1e-24) {
            double cumulative = 0.0;
            for (int i = supportLeft + 1; i <= supportRight; ++i) {
                const double e0 = std::pow(componentEnvelope[static_cast<size_t>(i - 1)], 2.0);
                const double e1 = std::pow(componentEnvelope[static_cast<size_t>(i)], 2.0);
                const double next = cumulative + 0.5 * (e0 + e1);
                if (next >= target) {
                    const double f = next > cumulative
                        ? std::clamp((target - cumulative) / (next - cumulative), 0.0, 1.0)
                        : 0.0;
                    t50 = i - 1 + f;
                    break;
                }
                cumulative = next;
            }
        }
    }

    // LeadingEdge：20/30/40/50/60%交点拟合，用于质量判断，不直接替代NCC。
    bool leadingReliable = false;
    double leadingTof = NaN;
    {
        constexpr std::array<double, 5> fractions{{0.20, 0.30, 0.40, 0.50, 0.60}};
        constexpr std::array<double, 5> weights{{1.0, 1.25, 1.5, 1.25, 1.0}};
        std::array<double, 5> crossing{};
        std::array<bool, 5> valid{};
        int count = 0;
        const int searchLeft = std::max(0, left - 12);
        for (size_t k = 0; k < fractions.size(); ++k) {
            const double target = fractions[k] * peakValue;
            for (int i = peak - 1; i >= searchLeft; --i) {
                if (componentEnvelope[static_cast<size_t>(i)] <= target
                    && componentEnvelope[static_cast<size_t>(i + 1)] > target) {
                    crossing[k] = highPrecisionInterpolateCrossing(
                        i, componentEnvelope[static_cast<size_t>(i)],
                        i + 1, componentEnvelope[static_cast<size_t>(i + 1)], target);
                    valid[k] = true;
                    ++count;
                    break;
                }
            }
        }
        bool monotonic = count >= 4;
        double previous = -std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < valid.size(); ++k) {
            if (!valid[k]) continue;
            if (crossing[k] + 0.15 < previous) {
                monotonic = false;
                break;
            }
            previous = crossing[k];
        }
        if (monotonic) {
            double sw = 0.0, sf = 0.0, sx = 0.0, sff = 0.0, sfx = 0.0;
            for (size_t k = 0; k < valid.size(); ++k) {
                if (!valid[k]) continue;
                sw += weights[k]; sf += weights[k] * fractions[k]; sx += weights[k] * crossing[k];
                sff += weights[k] * fractions[k] * fractions[k];
                sfx += weights[k] * fractions[k] * crossing[k];
            }
            const double den = sw * sff - sf * sf;
            if (std::abs(den) > 1e-18) {
                const double slope = (sw * sfx - sf * sx) / den;
                const double intercept = (sx - slope * sf) / sw;
                const double meanX = sx / sw;
                double sse = 0.0, sst = 0.0;
                for (size_t k = 0; k < valid.size(); ++k) {
                    if (!valid[k]) continue;
                    const double residual = crossing[k] - (intercept + slope * fractions[k]);
                    sse += weights[k] * residual * residual;
                    const double centered = crossing[k] - meanX;
                    sst += weights[k] * centered * centered;
                }
                const double r2 = sst > 1e-18 ? std::clamp(1.0 - sse / sst, -1.0, 1.0) : NaN;
                const double rms = std::sqrt(sse / sw);
                const double fittedTof = intercept + 0.10 * slope;
                // 与 LSTAlgorithmTest 完全一致：除了R²/RMS/斜率质量门，
                // 10%外推前沿还必须落在本帧实际前沿搜索区内，且不能越过包络主峰。
                // 否则像 Frame_0058 这类“包络主峰贴 packet 左边界”的波形会被误判为可靠前沿。
                const bool fittedTofPhysical = std::isfinite(fittedTof)
                    && std::isfinite(slope)
                    && slope > 0.0 && slope <= 40.0
                    && fittedTof >= searchLeft - 1.0 && fittedTof <= peak;
                leadingReliable = fittedTofPhysical
                    && std::isfinite(r2) && r2 >= 0.95
                    && std::isfinite(rms) && rms <= 0.50
                    && slope >= 1.0 && slope <= 30.0;
                if (fittedTofPhysical)
                    leadingTof = fittedTof;
            }
        }
    }

    // Envelope NCC：整个当前packet内自由搜索，三点抛物线亚采样，不使用历史绝对位置门。
    double nccTof = NaN;
    double nccCorrelation = NaN;
    constexpr int templateHalfWidth = 20;
    if (history && !history->envelopeTemplate.empty()) {
        const int half = static_cast<int>(history->envelopeTemplate.size()) / 2;
        const int searchLeft = std::max(componentLeft, half);
        const int searchRight = std::min(componentRight, n - 1 - half);
        int bestCenter = -1;
        double bestCorrelation = -std::numeric_limits<double>::infinity();
        for (int center = searchLeft; center <= searchRight; ++center) {
            const double rawRho = packetCorrelation(
                history->envelopeTemplate.data(),
                componentEnvelope.data() + center - half,
                history->envelopeTemplate.size());
            const double rho = std::isfinite(rawRho) ? rawRho : -1.0;
            if (bestCenter < 0 || rho > bestCorrelation) {
                bestCenter = center;
                bestCorrelation = rho;
            }
        }
        if (bestCenter >= 0) {
            double delta = 0.0;
            if (bestCenter > searchLeft && bestCenter < searchRight) {
                const auto correlationAt = [&](int center) {
                    const double rho = packetCorrelation(
                        history->envelopeTemplate.data(),
                        componentEnvelope.data() + center - half,
                        history->envelopeTemplate.size());
                    return std::isfinite(rho) ? rho : -1.0;
                };
                const double ym = correlationAt(bestCenter - 1);
                const double y0 = bestCorrelation;
                const double yp = correlationAt(bestCenter + 1);
                const double curvature = ym - 2.0 * y0 + yp;
                if (std::isfinite(curvature) && curvature < -1e-12)
                    delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
            }
            nccCorrelation = bestCorrelation;
            nccTof = bestCenter + delta;
        }
    }

    // Echo1→Echo2→Echo3 多回波周期一致性认证。三个回波都用当前帧首波包络定位，
    // 不使用历史绝对TOF；周期差与能量衰减用于排除“相关很高但不是同阶回波”的分支。
    bool echoChainCertified = false;
    double echoChainPeriod12 = NaN;
    double echoChainPeriod23 = NaN;
    double echoChainAveragePeriod = NaN;
    double echoChainPeriodDifference = NaN;
    double echoChainCorrelation12 = NaN;
    double echoChainCorrelation23 = NaN;
    double echoChainCorrelation13 = NaN;
    double echoChainEnergyRatio2 = NaN;
    double echoChainEnergyRatio3 = NaN;
    {
        const int firstLeft = componentLeft;
        const int firstRight = componentRight;
        const int firstCount = firstRight - firstLeft + 1;
        if (firstCount >= 8 && p.longitudinalVelocityMps > 0.0 && p.sampleRateHz > 0.0) {
            double firstEnergy = 0.0;
            for (int i = firstLeft; i <= firstRight; ++i) {
                const double v = corrected[static_cast<size_t>(i)];
                firstEnergy += v * v;
            }
            const double nominalLag = 2.0 * p.thicknessStdMm * 1e-3
                / p.longitudinalVelocityMps * p.sampleRateHz;
            const double lagTolerance = std::clamp(p.echoPeriodTolerance, 0.08, 0.35);
            const int lagLeft = std::max(1,
                static_cast<int>(std::floor(nominalLag * (1.0 - lagTolerance))));
            const int lagRight = std::min((n - 1 - firstRight) / 2,
                static_cast<int>(std::ceil(nominalLag * (1.0 + lagTolerance))));

            struct EchoChainCandidate {
                int lag = 0;
                double rho12 = -1.0;
                double rho23 = -1.0;
                double rho13 = -1.0;
                double energyRatio2 = 0.0;
                double energyRatio3 = 0.0;
                double score = -1.0;
            };
            EchoChainCandidate bestCandidate;
            bool haveBestCandidate = false;
            constexpr double minEcho2EnergyRatio = 0.01;
            constexpr double minEcho3EnergyRatio = 0.002;
            for (int lag = lagLeft; lag <= lagRight; ++lag) {
                const int secondLeft = firstLeft + lag;
                const int thirdLeft = firstLeft + 2 * lag;
                double energy2 = 0.0, energy3 = 0.0;
                for (int k = 0; k < firstCount; ++k) {
                    const double v2 = corrected[static_cast<size_t>(secondLeft + k)];
                    const double v3 = corrected[static_cast<size_t>(thirdLeft + k)];
                    energy2 += v2 * v2;
                    energy3 += v3 * v3;
                }
                const double ratio2 = firstEnergy > 1e-24 ? energy2 / firstEnergy : 0.0;
                const double ratio3 = firstEnergy > 1e-24 ? energy3 / firstEnergy : 0.0;
                if (ratio2 < minEcho2EnergyRatio || ratio3 < minEcho3EnergyRatio)
                    continue;
                const double rho12 = packetCorrelation(
                    corrected.data() + firstLeft,
                    corrected.data() + secondLeft,
                    static_cast<size_t>(firstCount));
                const double rho23 = packetCorrelation(
                    corrected.data() + secondLeft,
                    corrected.data() + thirdLeft,
                    static_cast<size_t>(firstCount));
                const double rho13 = packetCorrelation(
                    corrected.data() + firstLeft,
                    corrected.data() + thirdLeft,
                    static_cast<size_t>(firstCount));
                if (!std::isfinite(rho12) || !std::isfinite(rho23) || !std::isfinite(rho13))
                    continue;
                const double weakest = std::min({rho12, rho23, rho13});
                const double meanRho = (rho12 + rho23 + rho13) / 3.0;
                const double score = 0.75 * weakest + 0.25 * meanRho;
                if (!haveBestCandidate || score > bestCandidate.score) {
                    bestCandidate = {lag, rho12, rho23, rho13, ratio2, ratio3, score};
                    haveBestCandidate = true;
                }
            }

            if (haveBestCandidate) {
                const int bestLag = bestCandidate.lag;
                auto refineLag = [&](const double *reference, int targetBase,
                                     int centerLag, int radius) {
                    struct Sample { int lag = 0; double rho = NaN; };
                    std::array<Sample, 17> samples{};
                    int sampleCount = 0;
                    for (int lag = std::max(1, centerLag - radius);
                         lag <= centerLag + radius; ++lag) {
                        const int targetLeft = targetBase + lag;
                        if (targetLeft < 0 || targetLeft + firstCount > n)
                            continue;
                        const double rho = packetCorrelation(
                            reference, corrected.data() + targetLeft,
                            static_cast<size_t>(firstCount));
                        if (std::isfinite(rho) && sampleCount < static_cast<int>(samples.size()))
                            samples[static_cast<size_t>(sampleCount++)] = {lag, rho};
                    }
                    if (sampleCount == 0)
                        return std::pair<double, double>{NaN, NaN};
                    int bestIndex = 0;
                    for (int i = 1; i < sampleCount; ++i)
                        if (samples[static_cast<size_t>(i)].rho
                            > samples[static_cast<size_t>(bestIndex)].rho)
                            bestIndex = i;
                    double delta = 0.0;
                    if (bestIndex > 0 && bestIndex + 1 < sampleCount) {
                        const double ym = samples[static_cast<size_t>(bestIndex - 1)].rho;
                        const double y0 = samples[static_cast<size_t>(bestIndex)].rho;
                        const double yp = samples[static_cast<size_t>(bestIndex + 1)].rho;
                        const double curvature = ym - 2.0 * y0 + yp;
                        if (std::isfinite(curvature) && curvature < -1e-12)
                            delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                    }
                    const auto &best = samples[static_cast<size_t>(bestIndex)];
                    return std::pair<double, double>{best.lag + delta, best.rho};
                };

                const auto refined12 = refineLag(
                    corrected.data() + firstLeft, firstLeft, bestLag, 4);
                const auto refined13 = refineLag(
                    corrected.data() + firstLeft, firstLeft, 2 * bestLag, 8);
                const int secondLeft = firstLeft
                    + static_cast<int>(std::lround(std::isfinite(refined12.first)
                        ? refined12.first : bestLag));
                std::pair<double, double> refined23{NaN, NaN};
                if (secondLeft >= 0 && secondLeft + firstCount <= n) {
                    const double nominal23 = std::isfinite(refined13.first)
                        ? refined13.first - refined12.first : bestLag;
                    refined23 = refineLag(
                        corrected.data() + secondLeft, secondLeft,
                        static_cast<int>(std::lround(nominal23)), 4);
                }
                if (std::isfinite(refined12.first) && std::isfinite(refined13.first)) {
                    echoChainPeriod12 = refined12.first;
                    echoChainPeriod23 = refined13.first - refined12.first;
                    echoChainAveragePeriod = 0.5 * refined13.first;
                    echoChainPeriodDifference = std::abs(echoChainPeriod12 - echoChainPeriod23);
                    echoChainCorrelation12 = refined12.second;
                    echoChainCorrelation23 = refined23.second;
                    echoChainCorrelation13 = refined13.second;
                    echoChainEnergyRatio2 = bestCandidate.energyRatio2;
                    echoChainEnergyRatio3 = bestCandidate.energyRatio3;
                    constexpr double minCorrelation12 = 0.88;
                    constexpr double minCorrelation23 = 0.80;
                    constexpr double minCorrelation13 = 0.72;
                    constexpr double maxPeriodDifferenceSamples = 3.0;
                    echoChainCertified = std::isfinite(echoChainCorrelation12)
                        && std::isfinite(echoChainCorrelation23)
                        && std::isfinite(echoChainCorrelation13)
                        && echoChainCorrelation12 >= minCorrelation12
                        && echoChainCorrelation23 >= minCorrelation23
                        && echoChainCorrelation13 >= minCorrelation13
                        && echoChainPeriodDifference <= maxPeriodDifferenceSamples
                        && echoChainEnergyRatio2 >= minEcho2EnergyRatio
                        && echoChainEnergyRatio3 >= minEcho3EnergyRatio;
                }
            }
        }
    }

    const bool hasNcc = std::isfinite(nccTof);
    const bool hasT50 = std::isfinite(t50);
    decision.nccAvailable = hasNcc;
    decision.t50Available = hasT50;
    const bool templateReliable = std::isfinite(nccCorrelation) && nccCorrelation >= 0.82;
    const double deltaNccT50 = hasNcc && hasT50 ? std::abs(nccTof - t50) : NaN;
    decision.envelopeNccCorrelation = nccCorrelation;
    decision.nccT50DeltaSamples = deltaNccT50;

    bool highPrecisionAllowed = false;
    if (hasNcc && templateReliable) {
        if (leadingReliable)
            highPrecisionAllowed = true; // 一致或能量重分配均保留NCC。
        else
            highPrecisionAllowed = hasT50 && std::isfinite(deltaNccT50) && deltaNccT50 <= 2.0;
    }

    if (highPrecisionAllowed) {
        decision.allowed = true;
        decision.usedMainFallback = false;
        decision.tofIndex = nccTof;
    } else if (std::isfinite(r.thicknessMm) && std::isfinite(r.interpolatedPeakIndex)) {
        bool mainFallback = false;
        if (r.fallbackPeakUsed && r.fallbackReason == QStringLiteral("THEORETICAL_WINDOW_CLIPPED")) {
            mainFallback = true;
        } else if (!hasNcc
                   && std::isfinite(r.snrDb) && r.snrDb >= p.minLocalSnrDb
                   && r.candidateCount >= 1 && r.candidateCount <= 2
                   && !r.fallbackPeakUsed
                   && ((r.fitSucceeded && std::isfinite(r.fitR2)
                        && ((r.candidateCount == 1 && r.fitR2 >= 0.80)
                            || (r.candidateCount == 2 && r.fitR2 >= 0.93)))
                       || (r.echoConfirmed && std::isfinite(r.echoCorrelation)
                           && r.echoCorrelation >= std::max(p.echoMinCorrelation, 0.80)))) {
            mainFallback = true;
        } else if ((r.searchWindowStage == QStringLiteral("WARMUP")
                    || r.trackingState == QStringLiteral("WARMUP"))
                   && leadingReliable && hasNcc && !templateReliable && hasT50
                   && std::isfinite(deltaNccT50) && deltaNccT50 <= 2.0
                   && r.candidateCount == 1
                   && std::isfinite(r.snrDb) && r.snrDb >= p.minLocalSnrDb + 5.0
                   && r.fitSucceeded && std::isfinite(r.fitR2) && r.fitR2 >= 0.95
                   && !r.fallbackPeakUsed
                   && r.echoConfirmed && std::isfinite(r.echoCorrelation)
                   && r.echoCorrelation >= std::max(p.echoMinCorrelation, 0.75)) {
            // WARMUP阶段Envelope模板仍在收敛；若仅因模板相关偏弱而被高精度层拒绝，
            // 但核心链已有唯一候选、极高R²、强SNR、Echo佐证且NCC/t50位置一致，
            // 允许暂用核心整包拟合TOF，避免模板学习期清空可靠厚度。
            mainFallback = true;
        } else if (r.trackingState == QStringLiteral("SEARCH")
                   && r.predictionHistoryCount <= 2 && r.candidateCount == 1
                   && std::isfinite(r.snrDb) && r.snrDb >= p.minLocalSnrDb + 5.0
                   && r.fitSucceeded && std::isfinite(r.fitR2) && r.fitR2 >= 0.95
                   && !r.fallbackPeakUsed && r.echoConfirmed
                   && std::isfinite(r.echoCorrelation) && r.echoCorrelation >= 0.90) {
            mainFallback = true;
        } else if (std::isfinite(r.trackingResidualSamples)
                   && std::abs(r.trackingResidualSamples) <= 2.0) {
            mainFallback = true;
        } else if (!std::isfinite(r.trackingResidualSamples)
                   && r.trackingState == QStringLiteral("LOCK_TEMPLATE_ONLY_RECOVERED")
                   && std::isfinite(r.trackingCorrelation) && r.trackingCorrelation >= 0.88) {
            mainFallback = true;
        } else if (!std::isfinite(r.trackingResidualSamples)
                   && r.trackingState == QStringLiteral("LOCK_FRAGMENTED_RECOVERED")
                   && std::isfinite(r.trackingCorrelation) && r.trackingCorrelation >= 0.75
                   && std::isfinite(deltaNccT50) && deltaNccT50 <= 3.0) {
            mainFallback = true;
        }
        if (mainFallback) {
            decision.allowed = true;
            decision.usedMainFallback = true;
            decision.tofIndex = r.interpolatedPeakIndex;
        }
    }

    if (decision.allowed && std::isfinite(decision.tofIndex)) {
        decision.thicknessMm = p.longitudinalVelocityMps
            * (decision.tofIndex + p.delayCount) / p.sampleRateHz / 2.0 * 1000.0
            - CURRENT_FIXED_BIAS_MM;
    }

    // ---------------------------------------------------------------------
    // Echo1→Echo2→Echo3 公共时间基准漂移校正。
    // 只学习差分周期比例、绝对/差分估计器残差和包内几何，均不包含历史绝对TOF位置。
    // ---------------------------------------------------------------------
    const bool triConsistent = highPrecisionAllowed
        && leadingReliable && templateReliable
        && hasNcc && hasT50 && std::isfinite(deltaNccT50) && deltaNccT50 <= 2.0;
    const double nccThicknessMm = hasNcc
        ? p.longitudinalVelocityMps * (nccTof + p.delayCount)
            / p.sampleRateHz / 2.0 * 1000.0 - CURRENT_FIXED_BIAS_MM
        : NaN;

    if (history && echoChainCertified
        && std::isfinite(echoChainAveragePeriod) && echoChainAveragePeriod > 1.0
        && triConsistent && std::isfinite(nccThicknessMm) && nccThicknessMm > 0.0
        && std::isfinite(nccCorrelation) && nccCorrelation >= 0.95) {
        const double ratio = nccThicknessMm / echoChainAveragePeriod;
        bool accept = std::isfinite(ratio) && ratio > 0.0;
        if (accept && history->echoPeriodThicknessScaleHistory.size() >= 15) {
            const double currentMedian = medianReusable(history->echoPeriodThicknessScaleHistory);
            if (std::isfinite(currentMedian) && currentMedian > 0.0)
                accept = std::abs(ratio / currentMedian - 1.0) <= 0.015;
        }
        if (accept) {
            history->echoPeriodThicknessScaleHistory.push_back(ratio);
            if (history->echoPeriodThicknessScaleHistory.size() > 61)
                history->echoPeriodThicknessScaleHistory.erase(
                    history->echoPeriodThicknessScaleHistory.begin());
        }
    }

    const int echoScaleCount = history
        ? static_cast<int>(history->echoPeriodThicknessScaleHistory.size()) : 0;
    const double echoScale = history && !history->echoPeriodThicknessScaleHistory.empty()
        ? medianReusable(history->echoPeriodThicknessScaleHistory) : NaN;
    const double echoThicknessMm = echoChainCertified
        && std::isfinite(echoScale) && std::isfinite(echoChainAveragePeriod)
        ? echoScale * echoChainAveragePeriod : NaN;

    const double internalDelay = hasNcc && std::isfinite(leadingTof)
        ? nccTof - leadingTof : NaN;
    const int internalHistoryCount = history
        ? static_cast<int>(history->packetInternalDelayHistory.size()) : 0;
    const double internalMedian = history && !history->packetInternalDelayHistory.empty()
        ? medianReusable(history->packetInternalDelayHistory) : NaN;
    const double internalDeviation = std::isfinite(internalDelay) && std::isfinite(internalMedian)
        ? internalDelay - internalMedian : NaN;

    const double absoluteResidualMm = std::isfinite(echoThicknessMm) && std::isfinite(nccThicknessMm)
        ? nccThicknessMm - echoThicknessMm : NaN;
    const int residualHistoryCount = history
        ? static_cast<int>(history->echoAbsoluteResidualHistory.size()) : 0;
    const double residualMedian = history && !history->echoAbsoluteResidualHistory.empty()
        ? medianReusable(history->echoAbsoluteResidualHistory) : NaN;
    const double residualDeviation = std::isfinite(absoluteResidualMm) && std::isfinite(residualMedian)
        ? absoluteResidualMm - residualMedian : NaN;
    decision.echoChainPeriodDifferenceSamples = echoChainPeriodDifference;
    decision.echoChainResidualDeviationMm = residualDeviation;

    constexpr int minEchoScaleCalibrationCount = 15;
    const bool residualMature = residualHistoryCount >= minEchoScaleCalibrationCount;
    const double physicalResidual = residualMature ? residualDeviation : absoluteResidualMm;
    if (echoChainCertified && echoScaleCount >= minEchoScaleCalibrationCount
        && std::isfinite(physicalResidual)
        && std::abs(physicalResidual) > (residualMature ? 0.25 : 0.30)) {
        echoChainCertified = false;
    }

    constexpr double commonShiftTriggerMm = 0.055;
    constexpr double commonShiftMaxMm = 0.20;
    constexpr double commonShiftDeadbandMm = 0.040;
    constexpr double commonShiftMaxPeriodDifferenceSamples = 0.50;
    constexpr double commonShiftMinCorrelation12 = 0.93;
    constexpr double commonShiftMinCorrelation23 = 0.90;
    constexpr double commonShiftMinCorrelation13 = 0.86;
    constexpr double commonShiftMaxEcho3ToEcho2EnergyRatio = 0.40;
    const bool internalGeometryNormal = internalHistoryCount >= 15
        && std::isfinite(internalDeviation) && std::abs(internalDeviation) <= 2.0;
    const bool echoEnergyDecayNormal = std::isfinite(echoChainEnergyRatio2)
        && std::isfinite(echoChainEnergyRatio3) && echoChainEnergyRatio2 > 1e-12
        && echoChainEnergyRatio3 / echoChainEnergyRatio2
            <= commonShiftMaxEcho3ToEcho2EnergyRatio;
    const bool strongPeriodicChain = echoChainCertified
        && std::isfinite(echoChainPeriodDifference)
        && echoChainPeriodDifference <= commonShiftMaxPeriodDifferenceSamples
        && std::isfinite(echoChainCorrelation12) && echoChainCorrelation12 >= commonShiftMinCorrelation12
        && std::isfinite(echoChainCorrelation23) && echoChainCorrelation23 >= commonShiftMinCorrelation23
        && std::isfinite(echoChainCorrelation13) && echoChainCorrelation13 >= commonShiftMinCorrelation13
        && echoEnergyDecayNormal;
    const bool firstPacketInternallyConsistent = triConsistent
        && std::isfinite(nccCorrelation) && nccCorrelation >= 0.95
        && deltaNccT50 <= 1.0;

    const bool positiveCommonShift = echoScaleCount >= minEchoScaleCalibrationCount
        && residualMature && std::isfinite(echoThicknessMm)
        && highPrecisionAllowed && std::isfinite(nccThicknessMm)
        && std::isfinite(residualDeviation)
        && strongPeriodicChain && firstPacketInternallyConsistent && internalGeometryNormal
        && residualDeviation >= commonShiftTriggerMm
        && residualDeviation <= commonShiftMaxMm;

    if (positiveCommonShift && decision.allowed && !decision.usedMainFallback) {
        const double correctionMm = std::max(0.0, residualDeviation - commonShiftDeadbandMm);
        decision.thicknessMm = nccThicknessMm - correctionMm;
        decision.tofIndex = ((decision.thicknessMm + CURRENT_FIXED_BIAS_MM) * 1e-3 * 2.0
            * p.sampleRateHz / p.longitudinalVelocityMps) - p.delayCount;
        decision.positiveCommonTimeShiftCorrected = true;
    }

    // 残差基线只学习三回波身份可靠、首波三证据一致且未触发公共时移的帧。
    if (history && echoChainCertified
        && std::isfinite(echoChainPeriodDifference)
        && echoChainPeriodDifference <= commonShiftMaxPeriodDifferenceSamples
        && triConsistent && std::isfinite(nccCorrelation) && nccCorrelation >= 0.95
        && deltaNccT50 <= 1.0 && std::isfinite(absoluteResidualMm)
        && !positiveCommonShift) {
        bool accept = true;
        if (history->echoAbsoluteResidualHistory.size() >= 15) {
            const double med = medianReusable(history->echoAbsoluteResidualHistory);
            if (std::isfinite(med))
                accept = std::abs(absoluteResidualMm - med) <= 0.040;
        }
        if (accept) {
            history->echoAbsoluteResidualHistory.push_back(absoluteResidualMm);
            if (history->echoAbsoluteResidualHistory.size() > 61)
                history->echoAbsoluteResidualHistory.erase(
                    history->echoAbsoluteResidualHistory.begin());
        }
    }

    // 包内几何历史同样只学习平移不敏感的NCC-LeadingEdge差值。
    if (history && triConsistent
        && std::isfinite(nccCorrelation) && nccCorrelation >= 0.95
        && deltaNccT50 <= 1.0 && std::isfinite(internalDelay)) {
        history->packetInternalDelayHistory.push_back(internalDelay);
        if (history->packetInternalDelayHistory.size() > 61)
            history->packetInternalDelayHistory.erase(
                history->packetInternalDelayHistory.begin());
    }

    // 当前帧无论最终是否走NCC，只要核心身份链有效且t50存在，就更新EMA包络模板。
    if (history && r.measurementStatus == QStringLiteral("VALID")
        && std::isfinite(r.thicknessMm) && std::isfinite(t50)
        && r.candidateCount == 1 && std::isfinite(r.snrDb) && r.snrDb >= p.minLocalSnrDb) {
        if (highPrecisionExtractTemplate(
                componentEnvelope, t50, templateHalfWidth, templateScratch)) {
            if (history->envelopeTemplate.size() != templateScratch.size()) {
                history->envelopeTemplate = templateScratch;
            } else {
                constexpr double alpha = 0.08;
                double energy = 0.0;
                for (size_t k = 0; k < templateScratch.size(); ++k) {
                    history->envelopeTemplate[k] = (1.0 - alpha) * history->envelopeTemplate[k]
                        + alpha * templateScratch[k];
                    energy += history->envelopeTemplate[k] * history->envelopeTemplate[k];
                }
                if (energy > 1e-24) {
                    const double scale = 1.0 / std::sqrt(energy);
                    for (double &v : history->envelopeTemplate)
                        v *= scale;
                }
            }
        }
    }
    return decision;
}

QVector<double> interpolateInvalid(
    const QVector<double> &source)
{
    QVector<double> result = source;

    if (result.isEmpty())
        return result;

    int i = 0;

    while (i < result.size()) {

        if (std::isfinite(result[i])) {
            ++i;
            continue;
        }

        const int begin = i;

        while (i < result.size() &&
               !std::isfinite(result[i]))
            ++i;

        const int end = i - 1;

        const bool hasLeft =
            begin > 0 &&
            std::isfinite(
                result[begin - 1]);

        const bool hasRight =
            i < result.size() &&
            std::isfinite(result[i]);

        if (hasLeft && hasRight) {

            const double left =
                result[begin - 1];

            const double right =
                result[i];

            const int count =
                end - begin + 1;

            for (int k = 0;
                 k < count;
                 ++k) {

                const double t =
                    static_cast<double>(
                        k + 1) /
                    (count + 1);

                result[begin + k] =
                    left +
                    (right - left) * t;
            }

        } else if (hasLeft) {

            for (int k = begin;
                 k <= end;
                 ++k)
                result[k] =
                    result[begin - 1];

        } else if (hasRight) {

            for (int k = begin;
                 k <= end;
                 ++k)
                result[k] =
                    result[i];

        } else {

            std::fill(
                result.begin() + begin,
                result.begin() + end + 1,
                0.0);
        }
    }

    return result;
}

QVariantMap runCurrentSpectrumAlgorithm(
    const CsvData &csv,
    const TestParameters &p)
{
    QVariantMap out;

    QVector<double> thickness =
        csv.thickness;

    const QVector<double> rawThickness =
        thickness;

    const QVector<double> position =
        csv.position;

    if (thickness.size() < 8) {
        out["valid"] = false;
        out["error"] =
            QStringLiteral("壁厚点数少于8点");
        return out;
    }

    int validCount = 0;

    for (double value : thickness) {
        if (std::isfinite(value))
            ++validCount;
    }

    if (validCount < 8) {
        out["valid"] = false;
        out["error"] =
            QStringLiteral("有效壁厚点少于8点");
        return out;
    }

    thickness =
        interpolateInvalid(thickness);

    double mean = 0.0;

    for (double value : thickness)
        mean += value;

    mean /= thickness.size();

    double variance = 0.0;

    for (double &value : thickness) {
        value -= mean;
        variance += value * value;
    }

    variance /= thickness.size();

    const double stdDev =
        std::sqrt(
            std::max(
                0.0,
                variance));

    bool spatialAxis =
        position.size() ==
        thickness.size();

    double dx = 1.0;

    if (spatialAxis) {

        double sumDx = 0.0;
        int dxCount = 0;

        for (int i = 1;
             i < position.size();
             ++i) {

            if (std::isfinite(position[i]) &&
                std::isfinite(position[i - 1])) {

                const double delta =
                    position[i] -
                    position[i - 1];

                if (delta > 0.0) {
                    sumDx += delta;
                    ++dxCount;
                }
            }
        }

        if (dxCount > 0) {
            dx =
                sumDx / dxCount;
        } else {
            spatialAxis = false;
            dx = 1.0;
        }
    }

    const int nfft =
        nextPowerOfTwo(
            thickness.size());

    std::vector<std::complex<double>> bins(
        nfft,
        {0.0, 0.0});

    for (int i = 0;
         i < thickness.size();
         ++i) {

        const double window =
            thickness.size() > 1
            ? 0.5 *
              (1.0 -
               std::cos(
                   2.0 * PI * i /
                   (thickness.size() - 1)))
            : 1.0;

        bins[i] =
            thickness[i] * window;
    }

    fft(bins);

    const double sampleSpacing = dx;

    const double freqStep =
        1.0 /
        (nfft * sampleSpacing);

    const double cutoffFrequency =
        spatialAxis
        ? 1.0 / p.cutoffWavelengthM
        : 0.1;

    double longEnergy = 0.0;
    double shortEnergy = 0.0;
    double totalEnergy = 0.0;

    double dominantAmplitude = 0.0;
    double dominantFrequency = 0.0;

    const int half =
        nfft / 2;

    for (int k = 1;
         k < half;
         ++k) {

        const double frequency =
            k * freqStep;

        const double amplitude =
            2.0 *
            std::abs(bins[k]) /
            thickness.size();

        const double energy =
            std::norm(bins[k]);

        totalEnergy += energy;

        if (frequency <=
            cutoffFrequency)
            longEnergy += energy;
        else
            shortEnergy += energy;

        if (amplitude >
            dominantAmplitude) {

            dominantAmplitude =
                amplitude;

            dominantFrequency =
                frequency;
        }
    }

    const double longRatio =
        totalEnergy > 0.0
        ? longEnergy / totalEnergy
        : 0.0;

    const double shortRatio =
        totalEnergy > 0.0
        ? shortEnergy / totalEnergy
        : 0.0;

    const double dominantWavelength =
        dominantFrequency > 0.0
        ? 1.0 / dominantFrequency
        : 0.0;

    const bool variationSignificant =
        stdDev >= p.minVariationMm;

    const bool longAbnormal =
        variationSignificant &&
        longRatio >=
            p.dominanceThreshold;

    const bool shortAbnormal =
        variationSignificant &&
        shortRatio >=
            p.dominanceThreshold;

    QString diagnosis;

    if (longAbnormal &&
        shortAbnormal) {

        diagnosis =
            QStringLiteral("长、短周期成分均明显");

    } else if (longAbnormal) {

        diagnosis =
            QStringLiteral("长周期壁厚成分占主导");

    } else if (shortAbnormal) {

        diagnosis =
            QStringLiteral("短周期壁厚成分占主导");

    } else {

        diagnosis =
            QStringLiteral("未达到当前诊断阈值");
    }

    out["valid"] = true;
    out["pointCount"] = thickness.size();
    out["validCount"] = validCount;
    out["spatialAxis"] = spatialAxis;
    out["sampleSpacing"] = sampleSpacing;
    out["stdDevMm"] = stdDev;
    out["cutoffFrequency"] = cutoffFrequency;
    out["longEnergyRatio"] = longRatio;
    out["shortEnergyRatio"] = shortRatio;
    out["dominantFrequency"] = dominantFrequency;
    out["dominantWavelength"] = dominantWavelength;
    out["dominantAmplitudeMm"] = dominantAmplitude;
    out["longAbnormal"] = longAbnormal;
    out["shortAbnormal"] = shortAbnormal;
    out["diagnosis"] = diagnosis;

    return out;
}

// -----------------------------------------------------------------------------
// 单文件执行
// -----------------------------------------------------------------------------

QVariantMap runOneFile(
    const QString &filePath,
    const TestParameters &p,
    PeakHistory *history = nullptr,
    AlgorithmWorkspace *workspace = nullptr)
{
    QVariantMap result;

    const QFileInfo info(filePath);

    result["fileName"] =
        info.fileName();

    result["filePath"] =
        info.absoluteFilePath();

    QElapsedTimer timer;
    timer.start();

    const CsvData csv =
        loadCsv(filePath);

    if (!csv.error.isEmpty()) {
        if (history)
            history->miss(p);
        result["measurementStatus"] = QStringLiteral("INVALID");
        result["rejectionReason"] = QStringLiteral("INVALID_DATA");
        result["qualityLevel"] = QStringLiteral("REJECTED");
        result["qualityLabel"] = QStringLiteral("已剔除");
        result["confidenceScore"] = 0.0;
        result["rejectionStage"] = QStringLiteral("输入数据");
        result["rejectionThresholdDetail"] = QStringLiteral("CSV读取失败或数据格式无效，未进入波形寻峰");
        result["confidenceBasis"] = result["rejectionThresholdDetail"];
        result["mode"] =
            QStringLiteral("未知");

        result["status"] =
            QStringLiteral("INVALID");

        result["valid"] = false;

        result["message"] =
            csv.error;

        result["processingMs"] =
            timer.nsecsElapsed() /
            1e6;

        return result;
    }

    QString mode = p.mode;

    if (mode == "auto")
        mode = csv.detectedType;

    if (mode == "waveform") {

        result["mode"] =
            QStringLiteral("波形测厚");

        if (csv.original.empty()) {
            if (history)
                history->miss(p);
            result["measurementStatus"] = QStringLiteral("INVALID");
            result["rejectionReason"] = QStringLiteral("INVALID_DATA");
            result["qualityLevel"] = QStringLiteral("REJECTED");
            result["qualityLabel"] = QStringLiteral("已剔除");
            result["confidenceScore"] = 0.0;
            result["rejectionStage"] = QStringLiteral("输入数据");
            result["rejectionThresholdDetail"] = QStringLiteral("缺少Original/波形列，未进入寻峰");
            result["confidenceBasis"] = result["rejectionThresholdDetail"];

            result["status"] =
                QStringLiteral("INVALID");

            result["valid"] = false;

            result["message"] =
                QStringLiteral(
                    "没有可用于测厚算法的Original/波形列");

            result["processingMs"] =
                timer.nsecsElapsed() /
                1e6;

            return result;
        }

        PredictionGate prediction;
        if (history) {
            history->prepare(info.absolutePath(), static_cast<int>(csv.original.size()));
            result["_historyClock"] = history->clock;
            prediction = history->predict(p);
        }
        // 工业实时性评估：只统计“采集卡已给出一帧内存波形”之后的核心处理链，
        // 排除CSV读取、磁盘I/O、QVariant结果封装和界面刷新。100 Hz意味着单帧预算10 ms。
        QElapsedTimer coreTimer;
        coreTimer.start();
        WaveformResult r =
            runCurrentWaveformAlgorithm(csv.original, p, nullptr, prediction, workspace);
        if (history)
            history->observe(r, p);
        evaluateWaveformQuality(r, p);
        result["coreProcessingMs"] = coreTimer.nsecsElapsed() / 1e6;
        // 保存当前帧使用的预测门和质量证据，预览直接复用，避免点击顺序改变检测结果。
        const QVariantMap diagnostics = waveformDiagnostics(r);
        for (auto it = diagnostics.constBegin(); it != diagnostics.constEnd(); ++it)
            result[it.key()] = it.value();

        const bool valid =
            std::isfinite(
                r.thicknessMm);

        result["valid"] = valid;
        result["sampleCount"] =
            static_cast<int>(
                csv.original.size());

        result["thicknessMm"] =
            valid
            ? QVariant(r.thicknessMm)
            : QVariant();

        result["peakIndex"] =
            r.peakIndex;

        result["subsampleOffset"] =
            r.subsampleOffset;

        result["interpolatedPeakIndex"] =
            r.interpolatedPeakIndex;

        result["searchLeftIndex"] =
            r.searchLeftIndex;

        result["searchRightIndex"] =
            r.searchRightIndex;

        result["peakTimeUs"] = finiteVariant(r.peakTimeSec * 1e6);
        result["peakVoltage"] = finiteVariant(r.peakVoltage);

        if (std::isfinite(
                csv.recordedThicknessMm)) {

            result["recordedThicknessMm"] =
                csv.recordedThicknessMm;

            if (valid) {
                result["recordedDeltaMm"] =
                    r.thicknessMm -
                    csv.recordedThicknessMm;
            }
        }

        double reference =
            std::numeric_limits<double>::quiet_NaN();

        QString referenceSource;

        // 手工期望厚度优先。
        if (p.expectedThicknessMm >= 0.0) {
            reference =
                p.expectedThicknessMm;

            referenceSource =
                QStringLiteral("手工期望厚度");

        } else if (std::isfinite(
                       csv.recordedThicknessMm)) {

            // 如果保存的Frame CSV含Thickness列，
            // 可把原上位机结果作为回归参考。
            reference =
                csv.recordedThicknessMm;

            referenceSource =
                QStringLiteral("文件内原算法厚度");
        }

        if (!valid) {

            result["status"] = r.measurementStatus;

            result["message"] =
                r.error.isEmpty()
                ? QStringLiteral("算法未得到有效厚度")
                : r.error;

        } else if (std::isfinite(reference)) {

            const double delta =
                r.thicknessMm -
                reference;

            result["referenceThicknessMm"] =
                reference;

            result["referenceSource"] =
                referenceSource;

            result["deltaMm"] =
                delta;

            const bool pass =
                std::fabs(delta) <=
                p.assertToleranceMm;

            result["status"] =
                pass
                ? QStringLiteral("PASS")
                : QStringLiteral("FAIL");

            result["message"] =
                QStringLiteral(
                    "与参考厚度比较");

        } else {

            result["status"] =
                QStringLiteral("OK");

            result["message"] =
                QStringLiteral(
                    "算法运行完成，未设置参考厚度");
        }

    } else if (mode == "spectrum") {
        if (history)
            *history = PeakHistory{};

        result["mode"] =
            QStringLiteral("壁厚频谱");

        if (csv.thickness.isEmpty()) {

            result["status"] =
                QStringLiteral("INVALID");

            result["valid"] = false;

            result["message"] =
                QStringLiteral(
                    "没有Thickness(mm)列");

            result["processingMs"] =
                timer.nsecsElapsed() /
                1e6;

            return result;
        }

        const QVariantMap spectrum =
            runCurrentSpectrumAlgorithm(
                csv,
                p);

        for (auto it =
                 spectrum.constBegin();
             it !=
                 spectrum.constEnd();
             ++it) {

            result[it.key()] =
                it.value();
        }

        const bool valid =
            spectrum.value(
                "valid").toBool();

        result["status"] =
            valid
            ? QStringLiteral("OK")
            : QStringLiteral("INVALID");

        result["message"] =
            valid
            ? spectrum.value(
                  "diagnosis").toString()
            : spectrum.value(
                  "error").toString();

    } else {
        if (history)
            history->miss(p);

        result["mode"] =
            QStringLiteral("未知");

        result["status"] =
            QStringLiteral("INVALID");

        result["valid"] = false;

        result["message"] =
            QStringLiteral("测试模式无效");
    }

    result["processingMs"] =
        timer.nsecsElapsed() /
        1e6;

    return result;
}

// 下一帧确认后，将首个PENDING帧当时真实测得的WaveformResult回填到已有结果行。
// 只恢复原始寻峰/厚度，不使用插值或预测厚度替代真实测量。
void applyConfirmedBackfill(QVariantMap &result,
                            const WaveformResult &r,
                            const TestParameters &p)
{
    WaveformResult confirmed = r;
    evaluateWaveformQuality(confirmed, p);
    const QVariantMap diagnostics = waveformDiagnostics(confirmed);
    for (auto it = diagnostics.constBegin(); it != diagnostics.constEnd(); ++it)
        result[it.key()] = it.value();

    result["valid"] = true;
    result["thicknessMm"] = r.thicknessMm;
    result["peakIndex"] = r.peakIndex;
    result["subsampleOffset"] = r.subsampleOffset;
    result["interpolatedPeakIndex"] = r.interpolatedPeakIndex;
    result["peakTimeUs"] = finiteVariant(r.peakTimeSec * 1e6);
    result["peakVoltage"] = finiteVariant(r.peakVoltage);

    if (result.contains("recordedThicknessMm")) {
        bool ok = false;
        const double recorded = result.value("recordedThicknessMm").toDouble(&ok);
        if (ok && std::isfinite(recorded))
            result["recordedDeltaMm"] = r.thicknessMm - recorded;
    }

    double reference = NaN;
    QString referenceSource;
    if (p.expectedThicknessMm >= 0.0) {
        reference = p.expectedThicknessMm;
        referenceSource = QStringLiteral("手工期望厚度");
    } else if (result.contains("recordedThicknessMm")) {
        bool ok = false;
        const double recorded = result.value("recordedThicknessMm").toDouble(&ok);
        if (ok && std::isfinite(recorded)) {
            reference = recorded;
            referenceSource = QStringLiteral("文件内原算法厚度");
        }
    }

    if (std::isfinite(reference)) {
        const double delta = r.thicknessMm - reference;
        result["referenceThicknessMm"] = reference;
        result["referenceSource"] = referenceSource;
        result["deltaMm"] = delta;
        result["status"] = std::abs(delta) <= p.assertToleranceMm
            ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        result["message"] = QStringLiteral("下一帧确认后回填；与参考厚度比较");
    } else {
        result["status"] = QStringLiteral("OK");
        result["message"] = QStringLiteral("下一帧确认后回填有效厚度");
    }
}

QString csvEscape(QString text)
{
    if (text.contains('"'))
        text.replace(
            "\"",
            "\"\"");

    if (text.contains(',') ||
        text.contains('"') ||
        text.contains('\n')) {

        text =
            "\"" + text + "\"";
    }

    return text;
}

} // namespace

// -----------------------------------------------------------------------------
// 主程序实时适配层
// -----------------------------------------------------------------------------

struct ThicknessProcessor::Impl
{
    PeakHistory history;
    AlgorithmWorkspace workspace;
    HighPrecisionHistory highPrecisionHistory;
    StartupProvisionalState startupProvisional;
    // 保存最近帧“当时”的高精度决策。PENDING帧下一帧确认时必须回填这一时刻的
    // NCC/三证据结果，不能再用旧核心厚度覆盖高精度序列。
    std::deque<std::pair<int, HighPrecisionDecision>> highPrecisionDecisionHistory;
    double lastSampleRate = NaN;
    double lastLowFreqMHz = NaN;
    double lastHighFreqMHz = NaN;
    double lastThicknessStdMm = NaN;
    double lastThicknessToleranceMm = NaN;
    int lastStructLen = 0;
    int lastMinWidth = 0;
    int lastDelayCount = 0;
    int lastFilterOrder = 0;
    std::uint64_t lastAlgorithmRevision = std::numeric_limits<std::uint64_t>::max();
    ThicknessDetectionMode lastDetectMode = ThicknessDetectionMode::Auto;
};

ThicknessProcessor::ThicknessProcessor()
    : m_impl(std::make_unique<Impl>())
{
}

ThicknessProcessor::~ThicknessProcessor() = default;

void ThicknessProcessor::reset()
{
    m_impl = std::make_unique<Impl>();
}

ThicknessResult ThicknessProcessor::process(
    const std::vector<double>& raw,
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
    const ThicknessAlgorithmParameters& parameters)
{
    if (filteredOut)
        filteredOut->clear();

    TestParameters p;
    p.mode = QStringLiteral("waveform");
    p.sampleRateHz = sampleRate;
    p.lowFreqMHz = lowFreqMHz;
    p.highFreqMHz = highFreqMHz;
    p.filterOrder = filterOrder;
    p.thicknessStdMm = thicknessStdMm;
    p.thicknessToleranceMm = thicknessToleranceMm;
    p.structLen = structLen;
    p.minWidth = minWidth;
    p.delayCount = delayCount;

    switch (detectMode) {
    case ThicknessDetectionMode::Peak:
        p.detectMode = QStringLiteral("peak");
        break;
    case ThicknessDetectionMode::Valley:
        p.detectMode = QStringLiteral("valley");
        break;
    case ThicknessDetectionMode::Auto:
    default:
        p.detectMode = QStringLiteral("auto");
        break;
    }

    // 参数来自数据采集页；其结构体默认值与测试程序当前界面保持一致。
    p.impulseMadMultiplier = parameters.impulseMadMultiplier;
    p.impulseMinJumpV = parameters.impulseMinJumpV;
    p.impulseIsolationRatio = parameters.impulseIsolationRatio;
    p.impulseGuardSamples = parameters.impulseGuardSamples;
    p.energyWindowSamples = parameters.energyWindowSamples;
    p.minLocalSnrDb = parameters.minLocalSnrDb;
    p.lowSnrSoftMinDb = parameters.lowSnrSoftMinDb;
    p.lowSnrSoftMinTrackingCorrelation = parameters.lowSnrSoftMinTrackingCorrelation;
    p.lowSnrSoftMaxResidualSamples = parameters.lowSnrSoftMaxResidualSamples;
    p.lowSnrSoftMinFitR2 = parameters.lowSnrSoftMinFitR2;
    p.noiseMadRejectMultiplier = parameters.noiseMadRejectMultiplier;
    p.noiseEwmaAlphaUp = parameters.noiseEwmaAlphaUp;
    p.noiseEwmaAlphaDown = parameters.noiseEwmaAlphaDown;
    p.minCandidateRatio = parameters.minCandidateRatio;
    p.candidateSeparationSamples = parameters.candidateSeparationSamples;
    p.predictionHistorySize = parameters.predictionHistorySize;
    p.predictionMinValid = parameters.predictionMinValid;
    p.predictionHalfWidth = parameters.predictionHalfWidth;
    p.predictionMaxMisses = parameters.predictionMaxMisses;
    p.trackingResidualAcceptSamples = parameters.trackingResidualAcceptSamples;
    p.trackingResidualRejectSamples = parameters.trackingResidualRejectSamples;
    p.reacquireConfirmFrames = parameters.reacquireConfirmFrames;
    p.reacquireConsistencySamples = parameters.reacquireConsistencySamples;
    p.longitudinalVelocityMps = parameters.longitudinalVelocityMps;
    p.echoMinCorrelation = parameters.echoMinCorrelation;
    p.echoMinSnrDb = parameters.echoMinSnrDb;
    p.echoMinRelativeEnergy = parameters.echoMinRelativeEnergy;
    p.echoPeriodTolerance = parameters.echoPeriodTolerance;
    p.packetMinWidth = parameters.packetMinWidth;
    p.packetMaxWidth = parameters.packetMaxWidth;
    p.packetMergeGap = parameters.packetMergeGap;
    p.packetEnergyFraction = parameters.packetEnergyFraction;
    p.lockedPacketMaxCoreWidth = parameters.lockedPacketMaxCoreWidth;
    p.lockedBroadMinTrackingCorrelation = parameters.lockedBroadMinTrackingCorrelation;
    p.lockedBroadEnergyPeakToleranceSamples = parameters.lockedBroadEnergyPeakToleranceSamples;
    p.lockedFragmentedCoreMinWidth = parameters.lockedFragmentedCoreMinWidth;
    p.lockedFragmentedMinMaxSnrDb = parameters.lockedFragmentedMinMaxSnrDb;
    p.lockedFragmentedSupportSnrDb = parameters.lockedFragmentedSupportSnrDb;
    p.lockedFragmentedSupportEnergyFraction = parameters.lockedFragmentedSupportEnergyFraction;
    p.lockedFragmentedMinTrackingCorrelation = parameters.lockedFragmentedMinTrackingCorrelation;
    p.lockedFragmentedMaxPredictionResidualSamples = parameters.lockedFragmentedMaxPredictionResidualSamples;
    p.lockedFragmentedMinFitR2 = parameters.lockedFragmentedMinFitR2;
    p.lockRecoveryDirectCorrelation = parameters.lockRecoveryDirectCorrelation;
    p.lockRecoveryDirectResidualSamples = parameters.lockRecoveryDirectResidualSamples;
    p.lockRecoveryPendingCorrelation = parameters.lockRecoveryPendingCorrelation;
    p.lockRecoveryPendingResidualSamples = parameters.lockRecoveryPendingResidualSamples;
    p.lockRecoveryConfirmConsistencySamples = parameters.lockRecoveryConfirmConsistencySamples;
    p.lockTemplateOnlyMinMaxSnrDb = parameters.lockTemplateOnlyMinMaxSnrDb;
    p.lockStraddlingMaxSeparationSamples = parameters.lockStraddlingMaxSeparationSamples;
    p.fitFootFraction = parameters.fitFootFraction;
    p.fitFootConfirmSamples = parameters.fitFootConfirmSamples;
    p.fitFootSmoothRadius = parameters.fitFootSmoothRadius;
    p.fitCenterHalfRange = parameters.fitCenterHalfRange;
    p.fitCenterSearchCoarseStep = 0.08;
    p.fitAmplitudeMinRatio = parameters.fitAmplitudeMinRatio;
    p.fitAmplitudeMaxRatio = parameters.fitAmplitudeMaxRatio;
    p.fitMinR2 = parameters.fitMinR2;
    p.adaptiveWidthHistorySize = parameters.adaptiveWidthHistorySize;
    p.adaptiveWidthMinHistory = parameters.adaptiveWidthMinHistory;
    p.adaptiveWidthMadMultiplier = parameters.adaptiveWidthMadMultiplier;
    p.adaptiveWidthMinToleranceSamples = parameters.adaptiveWidthMinToleranceSamples;
    p.adaptiveWidthOutlierMultiplier = parameters.adaptiveWidthOutlierMultiplier;
    p.trackingMinCorrelation = parameters.trackingMinCorrelation;
    p.trackingCorrelationMargin = parameters.trackingCorrelationMargin;
    p.trackingEmaTemplateAlpha = parameters.trackingEmaTemplateAlpha;
    p.trackingShapeTemplateAlpha = parameters.trackingShapeTemplateAlpha;
    p.trackingMultiTemplateAgreementSamples = parameters.trackingMultiTemplateAgreementSamples;
    p.trackingBackupTemplateMaxResidualSamples = parameters.trackingBackupTemplateMaxResidualSamples;
    p.trackingBackupRecentBlendAlpha = parameters.trackingBackupRecentBlendAlpha;
    p.trackingSoftCorrelation = parameters.trackingSoftCorrelation;
    p.trackingSoftFitPredictionToleranceSamples =
        parameters.trackingSoftFitPredictionToleranceSamples;
    p.trackingSoftMinFitR2 = parameters.trackingSoftMinFitR2;
    p.trackingSoftMinMaxSnrDb = parameters.trackingSoftMinMaxSnrDb;

    const bool parametersChanged =
        m_impl->lastSampleRate != sampleRate
        || m_impl->lastLowFreqMHz != lowFreqMHz
        || m_impl->lastHighFreqMHz != highFreqMHz
        || m_impl->lastThicknessStdMm != thicknessStdMm
        || m_impl->lastThicknessToleranceMm != thicknessToleranceMm
        || m_impl->lastStructLen != structLen
        || m_impl->lastMinWidth != minWidth
        || m_impl->lastDelayCount != delayCount
        || m_impl->lastFilterOrder != filterOrder
        || m_impl->lastAlgorithmRevision != parameters.revision
        || m_impl->lastDetectMode != detectMode;
    if (parametersChanged) {
        m_impl->history.resetAll();
        m_impl->workspace = AlgorithmWorkspace{};
        m_impl->highPrecisionHistory = HighPrecisionHistory{};
        m_impl->startupProvisional.reset();
        m_impl->highPrecisionDecisionHistory.clear();
        m_impl->lastSampleRate = sampleRate;
        m_impl->lastLowFreqMHz = lowFreqMHz;
        m_impl->lastHighFreqMHz = highFreqMHz;
        m_impl->lastThicknessStdMm = thicknessStdMm;
        m_impl->lastThicknessToleranceMm = thicknessToleranceMm;
        m_impl->lastStructLen = structLen;
        m_impl->lastMinWidth = minWidth;
        m_impl->lastDelayCount = delayCount;
        m_impl->lastFilterOrder = filterOrder;
        m_impl->lastAlgorithmRevision = parameters.revision;
        m_impl->lastDetectMode = detectMode;
    }

    m_impl->history.prepare(
        QStringLiteral("REALTIME_ACQUISITION"),
        static_cast<int>(raw.size()));
    if (m_impl->highPrecisionHistory.sampleCount != static_cast<int>(raw.size()))
        m_impl->startupProvisional.reset();
    m_impl->highPrecisionHistory.prepare(static_cast<int>(raw.size()));
    const PredictionGate prediction = m_impl->history.predict(p);

    // 测厚、界面显示和逐帧保存共用同源核心生成的唯一滤波结果。
    WaveformResult waveform = runCurrentWaveformAlgorithm(
        raw, p, filteredOut, prediction, &m_impl->workspace);

    // ---------------------------------------------------------------------
    // STARTUP_PROVISIONAL：只处理“SEARCH冷启动 + 单个强packet + 拟合中心落在多瓣谷底”
    // 的特殊场景。第一帧只缓存临时Hilbert包络，不对外输出；紧邻第二帧必须通过
    // 包络模板互相关和ToF一致性后，才用两帧Envelope-NCC位置建立初始LOCK并回填首帧。
    // 正常VALID启动、LOCK/LOST和成熟高精度链完全不经过该旁路。
    // ---------------------------------------------------------------------
    bool startupEnvelopeConfirmed = false;
    int startupBackfillClock = 0;
    double startupBackfillTof = NaN;
    WaveformResult startupBackfillWaveform;

    const bool startupValleyCandidate = prediction.startupConfirmationEnabled
        && !prediction.lockActive && !prediction.reacquireRequired
        && !m_impl->history.everLocked && m_impl->history.validPeaks.empty()
        && waveform.measurementStatus == QStringLiteral("NO_SIGNAL")
        && waveform.rejectionReason == QStringLiteral("NO_SIGNAL")
        && waveform.candidateCount == 1
        && waveform.fitSucceeded && !waveform.fallbackPeakUsed
        && std::isfinite(waveform.fitR2) && waveform.fitR2 >= 0.98
        && waveform.packetLeftIndex >= 0 && waveform.packetRightIndex > waveform.packetLeftIndex
        && std::isfinite(waveform.maxLocalSnrDb)
        && waveform.maxLocalSnrDb >= p.minLocalSnrDb + 3.0
        && std::isfinite(waveform.snrDb)
        && waveform.maxLocalSnrDb - waveform.snrDb >= 3.0
        && waveform.echoConfirmed && std::isfinite(waveform.echoCorrelation)
        && waveform.echoCorrelation >= 0.85;

    StartupEnvelopeObservation startupObservation;
    bool startupObservationReady = false;
    if (startupValleyCandidate) {
        startupObservationReady = computeStartupEnvelopeObservation(
            m_impl->workspace.filtered, waveform,
            m_impl->highPrecisionHistory, startupObservation);
    }

    if (startupObservationReady) {
        auto &pending = m_impl->startupProvisional;
        const bool consecutivePending = pending.pending
            && pending.clock == m_impl->history.clock - 1;
        bool confirmed = false;
        std::vector<double> combinedTemplate;
        double firstTof = NaN;
        double currentTof = NaN;
        double firstCorrelation = NaN;
        double currentCorrelation = NaN;
        double templateCorrelation = NaN;

        constexpr double startupAnchorDisagreementSamples = 2.0;
        const bool usePeakAnchor = consecutivePending
            && std::isfinite(pending.anchorTof) && std::isfinite(pending.peakAnchorTof)
            && std::isfinite(startupObservation.t50Tof) && std::isfinite(startupObservation.peakTof)
            && (std::abs(pending.anchorTof - pending.peakAnchorTof)
                    > startupAnchorDisagreementSamples
                || std::abs(startupObservation.t50Tof - startupObservation.peakTof)
                    > startupAnchorDisagreementSamples);
        const std::vector<double> &pendingTemplate = usePeakAnchor
            ? pending.peakEnvelopeTemplate : pending.envelopeTemplate;
        const std::vector<double> &currentTemplate = usePeakAnchor
            ? startupObservation.peakEnvelopeTemplate : startupObservation.envelopeTemplate;

        if (consecutivePending
            && pendingTemplate.size() == currentTemplate.size()
            && !pendingTemplate.empty()) {
            templateCorrelation = packetCorrelation(
                pendingTemplate.data(), currentTemplate.data(), pendingTemplate.size());
            if (std::isfinite(templateCorrelation) && templateCorrelation >= 0.85
                && startupAverageTemplates(pendingTemplate, currentTemplate,
                                           combinedTemplate)) {
                constexpr double startupPeakGuidedNccHalfWidth = 3.0;
                firstTof = startupEnvelopeNccTof(
                    combinedTemplate, pending.correctedEnvelope,
                    pending.packetLeft, pending.packetRight, firstCorrelation,
                    usePeakAnchor ? pending.peakAnchorTof : NaN,
                    usePeakAnchor ? startupPeakGuidedNccHalfWidth : 0.0);
                currentTof = startupEnvelopeNccTof(
                    combinedTemplate, m_impl->highPrecisionHistory.hilbertCorrected,
                    startupObservation.packetLeft, startupObservation.packetRight,
                    currentCorrelation,
                    usePeakAnchor ? startupObservation.peakTof : NaN,
                    usePeakAnchor ? startupPeakGuidedNccHalfWidth : 0.0);
                confirmed = std::isfinite(firstTof) && std::isfinite(currentTof)
                    && std::isfinite(firstCorrelation) && firstCorrelation >= 0.85
                    && std::isfinite(currentCorrelation) && currentCorrelation >= 0.85
                    && std::abs(currentTof - firstTof) <= 3.0;

                // 冷启动多瓣包的包络相关面可能存在多个很高的近邻分支：两帧模板形态几乎
                // 一样，但各自全包NCC会落到不同内部支路。此时不放宽两帧一致性门，而是
                // 用当前两帧各自的Hilbert峰只提供“相对位移”；绝对位置仍锚定在相关峰更
                // 接近自身Hilbert峰的那一帧NCC上。这个分支不读取历史绝对位置。
                if (!confirmed && usePeakAnchor
                    && std::isfinite(templateCorrelation) && templateCorrelation >= 0.90
                    && std::isfinite(firstCorrelation) && firstCorrelation >= 0.95
                    && std::isfinite(currentCorrelation) && currentCorrelation >= 0.95
                    && std::isfinite(pending.peakAnchorTof)
                    && std::isfinite(startupObservation.peakTof)
                    && std::abs(startupObservation.peakTof - pending.peakAnchorTof) <= 3.0
                    && std::isfinite(firstTof) && std::isfinite(currentTof)
                    && std::abs(currentTof - firstTof) > 3.0) {
                    const double firstBranchResidual =
                        std::abs(firstTof - pending.peakAnchorTof);
                    const double currentBranchResidual =
                        std::abs(currentTof - startupObservation.peakTof);
                    constexpr double startupBranchAnchorMaxResidualSamples = 2.0;
                    if (std::min(firstBranchResidual, currentBranchResidual)
                        <= startupBranchAnchorMaxResidualSamples) {
                        if (currentBranchResidual <= firstBranchResidual) {
                            firstTof = currentTof
                                + (pending.peakAnchorTof - startupObservation.peakTof);
                        } else {
                            currentTof = firstTof
                                + (startupObservation.peakTof - pending.peakAnchorTof);
                        }
                        confirmed = std::abs(currentTof - firstTof) <= 3.0
                            && std::abs(firstTof - pending.peakAnchorTof) <= 2.0
                            && std::abs(currentTof - startupObservation.peakTof) <= 2.0;
                    }
                }
            }
        }

        if (confirmed) {
            // 认证成功只恢复这两帧输出，不把临时Envelope位置写入正式PeakHistory。
            // 正式状态机仍按原NO_SIGNAL节奏运行，直到后续正常强信号自行建立LOCK；
            // 这样冷启动增益不会改变成熟段的预测轨迹、RECENT模板或正式Envelope模板。
            WaveformResult rejectedForHistory = waveform;
            m_impl->history.observe(rejectedForHistory, p);

            startupBackfillClock = pending.clock;
            startupBackfillTof = firstTof;
            startupBackfillWaveform = pending.waveform;
            promoteStartupWaveform(startupBackfillWaveform,
                                   pending.filteredWaveform, p, firstTof);
            startupBackfillWaveform.delayedConfirmationBackfilled = true;
            startupBackfillWaveform.delayedConfirmationDelayFrames = 1;
            startupBackfillWaveform.startupPendingCount = 2;
            startupBackfillWaveform.trackingState = QStringLiteral("BACKFILLED_STARTUP_PROVISIONAL_NCC");

            promoteStartupWaveform(waveform, m_impl->workspace.filtered, p, currentTof);
            waveform.startupPendingCount = 2;
            waveform.trackingState = QStringLiteral("STARTUP_PROVISIONAL_CONFIRMED_READ_ONLY");
            waveform.error += QStringLiteral("；两帧临时Envelope-NCC认证通过，仅恢复输出，不写正式身份历史");
            startupEnvelopeConfirmed = true;
            pending.reset();
        } else {
            // 未形成紧邻两帧一致身份时，当前帧成为新的provisional起点；仍不输出。
            pending.pending = true;
            pending.clock = m_impl->history.clock;
            pending.anchorTof = startupObservation.t50Tof;
            pending.peakAnchorTof = startupObservation.peakTof;
            pending.packetLeft = startupObservation.packetLeft;
            pending.packetRight = startupObservation.packetRight;
            pending.envelopeTemplate = startupObservation.envelopeTemplate;
            pending.peakEnvelopeTemplate = startupObservation.peakEnvelopeTemplate;
            pending.correctedEnvelope = m_impl->highPrecisionHistory.hilbertCorrected;
            pending.filteredWaveform = m_impl->workspace.filtered;
            pending.waveform = waveform;
            waveform.trackingState = QStringLiteral("STARTUP_PROVISIONAL");
            waveform.error += QStringLiteral("；已缓存临时Hilbert包络，等待紧邻下一帧Envelope-NCC认证");
            m_impl->history.observe(waveform, p);
        }
    } else {
        // provisional必须由紧邻帧确认；任何其它帧都会终止上一临时候选。
        m_impl->startupProvisional.reset();
        m_impl->history.observe(waveform, p);
    }

    // 旧核心继续负责纵波身份、候选、LOCK/LOST与预测；最终显示厚度改由高精度TOF层接管。
    // STARTUP_PROVISIONAL确认帧已经由临时Envelope-NCC给出最终ToF，必须保持只读：
    // 不调用正式高精度模板学习，避免冷启动临时形态污染后续成熟Envelope模板。
    HighPrecisionDecision highPrecision;
    if (startupEnvelopeConfirmed && std::isfinite(waveform.interpolatedPeakIndex)
        && std::isfinite(waveform.thicknessMm)) {
        highPrecision.allowed = true;
        highPrecision.usedMainFallback = false;
        highPrecision.tofIndex = waveform.interpolatedPeakIndex;
        highPrecision.thicknessMm = waveform.thicknessMm;
    } else {
        highPrecision = computeHighPrecisionDecision(
            m_impl->workspace.filtered, waveform, p, &m_impl->highPrecisionHistory);
    }
    m_impl->highPrecisionDecisionHistory.push_back({m_impl->history.clock, highPrecision});
    while (m_impl->highPrecisionDecisionHistory.size() > 32)
        m_impl->highPrecisionDecisionHistory.pop_front();

    WaveformResult backfillWaveform;
    int backfillTargetClock = 0;
    const bool hasBackfill =
        m_impl->history.takeBackfill(backfillWaveform, backfillTargetClock);

    ThicknessResult result;
    const bool legacyCoreValid = waveform.measurementStatus == QStringLiteral("VALID")
        && std::isfinite(waveform.thicknessMm)
        && std::isfinite(waveform.interpolatedPeakIndex);
    // 高精度TOF只能替换“已由核心身份链确认有效”的当前帧厚度；
    // TRACKING_JUMP / REACQUIRE_PENDING / NO_SIGNAL 等核心拒绝帧不能仅凭NCC重新变成VALID。
    result.valid = legacyCoreValid
        && highPrecision.allowed && std::isfinite(highPrecision.thicknessMm)
        && std::isfinite(highPrecision.tofIndex);
    result.thicknessMm = result.valid ? highPrecision.thicknessMm : NaN;
    result.peakTimeSec = result.valid ? highPrecision.tofIndex / p.sampleRateHz : NaN;
    result.peakVoltage = result.valid ? waveform.peakVoltage : NaN;
    result.snrDb = result.valid ? waveform.snrDb : NaN;
    result.peakIndex = result.valid
        ? static_cast<int>(std::llround(highPrecision.tofIndex)) : -1;

    if (hasBackfill && std::isfinite(backfillWaveform.thicknessMm)) {
        const auto decisionIt = std::find_if(
            m_impl->highPrecisionDecisionHistory.begin(),
            m_impl->highPrecisionDecisionHistory.end(),
            [&](const auto &entry) { return entry.first == backfillTargetClock; });

        bool backfillAllowed = false;
        double backfillTof = NaN;
        double backfillThickness = NaN;
        if (decisionIt != m_impl->highPrecisionDecisionHistory.end()
            && decisionIt->second.allowed
            && std::isfinite(decisionIt->second.tofIndex)
            && std::isfinite(decisionIt->second.thicknessMm)) {
            // 首选PENDING帧当时已经形成的高精度NCC/安全决策。
            backfillAllowed = true;
            backfillTof = decisionIt->second.tofIndex;
            backfillThickness = decisionIt->second.thicknessMm;
        } else {
            // 与测试程序applyConfirmedBackfill保持相同的保守主算法回退范围。
            const bool clippedSafe = backfillWaveform.fallbackPeakUsed
                && backfillWaveform.fallbackReason == QStringLiteral("THEORETICAL_WINDOW_CLIPPED");
            const bool nccUnavailable = decisionIt != m_impl->highPrecisionDecisionHistory.end()
                && !decisionIt->second.nccAvailable;
            const bool startupSafe = nccUnavailable
                && std::isfinite(backfillWaveform.snrDb)
                && backfillWaveform.snrDb >= p.minLocalSnrDb
                && backfillWaveform.candidateCount >= 1 && backfillWaveform.candidateCount <= 2
                && !backfillWaveform.fallbackPeakUsed
                && ((backfillWaveform.fitSucceeded && std::isfinite(backfillWaveform.fitR2)
                     && ((backfillWaveform.candidateCount == 1 && backfillWaveform.fitR2 >= 0.80)
                         || (backfillWaveform.candidateCount == 2
                             && backfillWaveform.fitR2 >= 0.93)))
                    || (backfillWaveform.echoConfirmed && std::isfinite(backfillWaveform.echoCorrelation)
                        && backfillWaveform.echoCorrelation >= std::max(p.echoMinCorrelation, 0.80)));
            const bool residualSafe = std::isfinite(backfillWaveform.trackingResidualSamples)
                && std::abs(backfillWaveform.trackingResidualSamples) <= 2.0;
            if (clippedSafe || startupSafe || residualSafe) {
                backfillAllowed = true;
                backfillTof = backfillWaveform.interpolatedPeakIndex;
                backfillThickness = backfillWaveform.thicknessMm;
            }
        }

        if (backfillAllowed && std::isfinite(backfillTof) && std::isfinite(backfillThickness)) {
            result.hasBackfill = true;
            result.backfillFramesAgo =
                (std::max)(1, m_impl->history.clock - backfillTargetClock);
            result.backfillThicknessMm = backfillThickness;
            result.backfillPeakTimeSec = backfillTof / p.sampleRateHz;
            result.backfillPeakVoltage = backfillWaveform.peakVoltage;
            result.backfillSnrDb = backfillWaveform.snrDb;
            result.backfillPeakIndex = static_cast<int>(std::llround(backfillTof));
        }
    }

    if (startupEnvelopeConfirmed && std::isfinite(startupBackfillTof)
        && std::isfinite(startupBackfillWaveform.thicknessMm)) {
        result.hasBackfill = true;
        result.backfillFramesAgo =
            (std::max)(1, m_impl->history.clock - startupBackfillClock);
        result.backfillThicknessMm = startupBackfillWaveform.thicknessMm;
        result.backfillPeakTimeSec = startupBackfillTof / p.sampleRateHz;
        result.backfillPeakVoltage = startupBackfillWaveform.peakVoltage;
        result.backfillSnrDb = startupBackfillWaveform.snrDb;
        result.backfillPeakIndex = static_cast<int>(std::llround(startupBackfillTof));
    }
    return result;
}

