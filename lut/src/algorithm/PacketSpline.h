#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

// 单脉冲等效高斯拟合。
//
// 目标不是逐点复现双峰/多峰的振荡细节，而是把当前极性的主纵波脉冲归一成
// 一个“从脚底起落、脉宽固定、中心唯一”的高斯脉冲，供稳定 ToF 定位使用。
//
// 新流程：
//   1. 用 packet 两端估计滤波波形的局部线性基线；
//   2. 按 peak/valley/auto 确定极性，把主脉冲统一翻成正脉冲；
//   3. 负半周截为0，仅做很轻的平滑，在真实滤波波形上寻找主脉冲脚底；
//   4. 以主脉冲峰高 10% 作为脚底门限，连续3点低于门限才确认波包结束；
//   5. 左右脚底用线性插值得到亚采样位置，脚底间距直接决定固定 sigma；
//   6. 高斯中心只允许在脚底中点附近 ±0.75 sample 微调，不允许跳向某个局部尖峰；
//   7. 高斯幅值按脚底窗口内正脉冲能量等效确定，不再由宽窗口最小二乘压低幅值；
//   8. R² 字段改为“平滑正脉冲与固定宽度高斯的形状相关系数平方”，用于质量门。
//
// 这样特别针对“右峰强 -> 双峰相近 -> 左峰强”的热态波形：局部最高峰可以交换，
// 但脚底、整体脉宽和等效高斯中心仍保持连续。
namespace PacketPeakFit {

constexpr double GAUSSIAN_FWHM_FACTOR = 2.3548200450309493;

struct FitOptions
{
    // 主脉冲脚底相对峰高比例，例如0.10表示10%峰高。
    double footFraction = 0.10;
    // 左右脚底需要连续多少个点低于门限才确认。
    int footConfirmSamples = 3;
    // 对极性校正后的正脉冲做轻微平滑的半径；0表示不平滑。
    int footSmoothRadius = 1;
    // 等效高斯中心只允许在脚底中点附近±该范围内微调。
    double centerFineHalfRange = 0.75;
    // 中心搜索先按该步长粗搜，再在最佳粗搜点±一个粗步长内按0.01 sample精搜。
    // 设为0.01时等价于原来的全范围0.01穷举。
    double centerSearchCoarseStep = 0.08;
    // 等效高斯峰值相对实测主峰值的允许范围。
    double amplitudeMinRatio = 0.75;
    double amplitudeMaxRatio = 1.50;
};

struct Fit
{
    bool solved = false;

    // left/right 为重新从主脉冲脚底得到的实际拟合窗口。
    int left = -1;
    int right = -1;
    int inputLeft = -1;
    int inputRight = -1;

    double center = std::numeric_limits<double>::quiet_NaN();
    double sigma = std::numeric_limits<double>::quiet_NaN();
    double pulseWidthSamples = std::numeric_limits<double>::quiet_NaN();
    double fwhmSamples = std::numeric_limits<double>::quiet_NaN();
    double footLevel = std::numeric_limits<double>::quiet_NaN();
    double envelopeBaseline = 0.0;

    // 兼容现有调用：value() 用 sqrt(amplitudeEnergy) 作为等效高斯电压幅值。
    double amplitudeEnergy = std::numeric_limits<double>::quiet_NaN();
    double backgroundEnergy = 0.0;

    double baselineOrigin = 0.0;
    double baselineIntercept = 0.0;
    double baselineSlope = 0.0;
    double polarity = 1.0;

    // rSquared 现在表示形状相关系数平方，而不是对原始振荡波形逐点回归的传统 R²。
    double rSquared = std::numeric_limits<double>::quiet_NaN();
    double residualRms = std::numeric_limits<double>::quiet_NaN();

    double value(double x) const
    {
        const double baseline = baselineIntercept + baselineSlope * (x - baselineOrigin);
        if (!solved || !std::isfinite(center) || !std::isfinite(sigma) || sigma <= 0.0)
            return baseline;

        const double normalized = (x - center) / sigma;
        const double amplitude = std::sqrt(std::max(0.0, amplitudeEnergy));
        const double pulse = amplitude * std::exp(-0.5 * normalized * normalized);
        return baseline + polarity * pulse;
    }
};

// 由调用方持有的显式拟合工作区。工业实时路径按处理线程/批次复用，
// 避免 non-trivial thread_local 在 QtConcurrent 工作线程退出阶段触发析构时序问题。
struct FitWorkspace
{
    std::vector<double> residual;
    std::vector<double> positive;
    std::vector<double> footShape;
    std::vector<double> pulseForQuality;
    std::vector<double> pulseCdf;
    std::vector<double> gaussianEnergy;
};

inline void movingAverage(const std::vector<double> &source, int radius,
                          std::vector<double> &result)
{
    if (source.empty()) {
        result.clear();
        return;
    }
    if (radius <= 0) {
        result.assign(source.begin(), source.end());
        return;
    }

    result.resize(source.size());
    for (int i = 0; i < static_cast<int>(source.size()); ++i) {
        const int lo = std::max(0, i - radius);
        const int hi = std::min(static_cast<int>(source.size()) - 1, i + radius);
        double sum = 0.0;
        for (int j = lo; j <= hi; ++j)
            sum += source[j];
        result[i] = sum / (hi - lo + 1);
    }
}

inline int findLeftBelowRun(const std::vector<double> &shape, int peak, double threshold,
                            int confirmSamples)
{
    confirmSamples = std::max(1, confirmSamples);
    for (int i = peak - 1; i >= confirmSamples - 1; --i) {
        bool below = true;
        for (int k = 0; k < confirmSamples; ++k)
            below = below && shape[i - k] <= threshold;
        if (below)
            return i;
    }
    for (int i = peak - 1; i >= 0; --i)
        if (shape[i] <= threshold)
            return i;
    return -1;
}

inline int findRightBelowRun(const std::vector<double> &shape, int peak, double threshold,
                             int confirmSamples)
{
    const int count = static_cast<int>(shape.size());
    confirmSamples = std::max(1, confirmSamples);
    for (int i = peak + 1; i + confirmSamples - 1 < count; ++i) {
        bool below = true;
        for (int k = 0; k < confirmSamples; ++k)
            below = below && shape[i + k] <= threshold;
        if (below)
            return i;
    }
    for (int i = peak + 1; i < count; ++i)
        if (shape[i] <= threshold)
            return i;
    return -1;
}

inline double interpolateLeftCrossing(const std::vector<double> &shape,
                                      int belowIndex, double threshold)
{
    if (belowIndex < 0 || belowIndex + 1 >= static_cast<int>(shape.size()))
        return static_cast<double>(belowIndex);
    const double y0 = shape[belowIndex];
    const double y1 = shape[belowIndex + 1];
    const double denominator = y1 - y0;
    if (std::fabs(denominator) <= 1e-15)
        return static_cast<double>(belowIndex);
    const double fraction = std::clamp((threshold - y0) / denominator, 0.0, 1.0);
    return belowIndex + fraction;
}

inline double interpolateRightCrossing(const std::vector<double> &shape,
                                       int belowIndex, double threshold)
{
    if (belowIndex <= 0 || belowIndex >= static_cast<int>(shape.size()))
        return static_cast<double>(belowIndex);
    const double y0 = shape[belowIndex - 1];
    const double y1 = shape[belowIndex];
    const double denominator = y1 - y0;
    if (std::fabs(denominator) <= 1e-15)
        return static_cast<double>(belowIndex);
    const double fraction = std::clamp((threshold - y0) / denominator, 0.0, 1.0);
    return (belowIndex - 1) + fraction;
}

struct CumulativeEnergyReference
{
    const std::vector<double> *pulseCdf = nullptr;
    double variation = std::numeric_limits<double>::quiet_NaN();
    bool valid = false;
};

inline CumulativeEnergyReference makeCumulativeEnergyReference(
    const std::vector<double> &pulse, std::vector<double> &pulseCdfWorkspace)
{
    CumulativeEnergyReference reference;
    if (pulse.size() < 3)
        return reference;

    double totalPulse = 0.0;
    for (double value : pulse)
        totalPulse += value * value;
    if (totalPulse <= 1e-24)
        return reference;

    pulseCdfWorkspace.resize(pulse.size());
    double runningPulse = 0.0;
    double meanPulse = 0.0;
    for (size_t i = 0; i < pulse.size(); ++i) {
        runningPulse += pulse[i] * pulse[i];
        pulseCdfWorkspace[i] = runningPulse / totalPulse;
        meanPulse += pulseCdfWorkspace[i];
    }
    meanPulse /= pulseCdfWorkspace.size();

    double variation = 0.0;
    for (double value : pulseCdfWorkspace) {
        const double centered = value - meanPulse;
        variation += centered * centered;
    }
    if (variation <= 1e-24)
        return reference;

    reference.pulseCdf = &pulseCdfWorkspace;
    reference.variation = variation;
    reference.valid = true;
    return reference;
}

inline double cumulativeEnergyR2AtCenter(const CumulativeEnergyReference &reference,
                                         int left, int right,
                                         double center, double sigma,
                                         std::vector<double> &gaussianEnergyWorkspace)
{
    if (!reference.valid || !reference.pulseCdf || sigma <= 0.0
        || right < left
        || static_cast<size_t>(right - left + 1) != reference.pulseCdf->size())
        return std::numeric_limits<double>::quiet_NaN();

    const std::vector<double> &pulseCdf = *reference.pulseCdf;
    const size_t count = pulseCdf.size();
    gaussianEnergyWorkspace.resize(count);
    double totalGaussian = 0.0;
    size_t local = 0;
    for (int i = left; i <= right; ++i, ++local) {
        const double normalized = (i - center) / sigma;
        // 旧算法先算g=exp(-0.5*x²)，随后使用g²；这里直接得到相同的g²，
        // 每个样点只调用一次exp，并把结果写入复用工作区供累计阶段使用。
        const double g2 = std::exp(-normalized * normalized);
        gaussianEnergyWorkspace[local] = g2;
        totalGaussian += g2;
    }
    if (totalGaussian <= 1e-24)
        return std::numeric_limits<double>::quiet_NaN();

    double runningGaussian = 0.0;
    double squaredError = 0.0;
    for (size_t i = 0; i < count; ++i) {
        runningGaussian += gaussianEnergyWorkspace[i];
        const double gaussianCdf = runningGaussian / totalGaussian;
        const double error = pulseCdf[i] - gaussianCdf;
        squaredError += error * error;
    }

    return std::clamp(1.0 - squaredError / reference.variation, 0.0, 1.0);
}

inline Fit fit(const std::vector<double> &signal, int left, int right,
               const char *detectMode, const FitOptions &options = {},
               FitWorkspace *workspace = nullptr)
{
    Fit result;
    result.inputLeft = left;
    result.inputRight = right;

    FitWorkspace localWorkspace;
    FitWorkspace &ws = workspace ? *workspace : localWorkspace;
    auto &residual = ws.residual;
    auto &positive = ws.positive;
    auto &footShape = ws.footShape;
    auto &pulseForQuality = ws.pulseForQuality;
    auto &pulseCdfWorkspace = ws.pulseCdf;
    auto &gaussianEnergyWorkspace = ws.gaussianEnergy;

    const int count = right - left + 1;
    if (left < 0 || right >= static_cast<int>(signal.size()) || count < 9)
        return result;
    for (int i = left; i <= right; ++i)
        if (!std::isfinite(signal[i]))
            return result;

    // packet 两端只用于估计当前局部线性基线；真正拟合窗口随后由主脉冲脚底重新确定。
    const int edgeCount = std::clamp(count / 8, 2, 10);
    double leftMean = 0.0;
    double rightMean = 0.0;
    for (int i = 0; i < edgeCount; ++i) {
        leftMean += signal[left + i];
        rightMean += signal[right - i];
    }
    leftMean /= edgeCount;
    rightMean /= edgeCount;

    const double leftX = left + 0.5 * (edgeCount - 1);
    const double rightX = right - 0.5 * (edgeCount - 1);
    result.baselineOrigin = 0.5 * (leftX + rightX);
    result.baselineSlope = rightX > leftX ? (rightMean - leftMean) / (rightX - leftX) : 0.0;
    result.baselineIntercept = 0.5 * (leftMean + rightMean);

    residual.assign(count, 0.0);
    double largestPositive = 0.0;
    double largestNegative = 0.0;
    for (int i = 0; i < count; ++i) {
        const double x = left + i;
        const double baseline = result.baselineIntercept
            + result.baselineSlope * (x - result.baselineOrigin);
        residual[i] = signal[left + i] - baseline;
        largestPositive = std::max(largestPositive, residual[i]);
        largestNegative = std::max(largestNegative, -residual[i]);
    }

    const std::string mode = detectMode ? detectMode : "auto";
    result.polarity = mode == "valley" ? -1.0
        : mode == "peak" ? 1.0
        : (largestPositive >= largestNegative ? 1.0 : -1.0);

    // 只保留当前极性的正脉冲。负半周不参与脚底宽度计算，避免 RMS/绝对值把前后振铃
    // 一并包入窗口。半径1轻微平滑仅用于抗单点噪声，不足以改变脉冲主宽度。
    positive.assign(count, 0.0);
    for (int i = 0; i < count; ++i)
        positive[i] = std::max(0.0, result.polarity * residual[i]);
    const int footSmoothRadius = std::clamp(options.footSmoothRadius, 0, 8);
    movingAverage(positive, footSmoothRadius, footShape);

    const auto peakIt = std::max_element(footShape.begin(), footShape.end());
    const int peakLocal = static_cast<int>(std::distance(footShape.begin(), peakIt));
    const double peakHeight = *peakIt;
    if (!std::isfinite(peakHeight) || peakHeight <= 1e-12)
        return result;

    const double footFraction = std::clamp(options.footFraction, 0.01, 0.80);
    const double footLevel = footFraction * peakHeight;
    result.footLevel = footLevel;
    result.envelopeBaseline = 0.0;

    const int footConfirmSamples = std::clamp(options.footConfirmSamples, 1, 20);
    const int leftBelow = findLeftBelowRun(footShape, peakLocal, footLevel, footConfirmSamples);
    const int rightBelow = findRightBelowRun(footShape, peakLocal, footLevel, footConfirmSamples);
    if (leftBelow < 0 || rightBelow < 0
        || leftBelow >= peakLocal || rightBelow <= peakLocal)
        return result;

    const double leftFootLocal = interpolateLeftCrossing(footShape, leftBelow, footLevel);
    const double rightFootLocal = interpolateRightCrossing(footShape, rightBelow, footLevel);
    if (!std::isfinite(leftFootLocal) || !std::isfinite(rightFootLocal)
        || rightFootLocal - leftFootLocal < 4.0)
        return result;

    const double leftFoot = left + leftFootLocal;
    const double rightFoot = left + rightFootLocal;
    result.left = std::max(left, static_cast<int>(std::floor(leftFoot)));
    result.right = std::min(right, static_cast<int>(std::ceil(rightFoot)));
    result.pulseWidthSamples = rightFoot - leftFoot;

    // 固定脉宽：脚底比例可调，因此宽度换算系数必须随 footFraction 一起变化。
    // 对高斯 exp(-0.5*(x/sigma)^2)，fraction宽度 = 2*sqrt(2*ln(1/fraction))*sigma。
    const double gaussianWidthFactor =
        2.0 * std::sqrt(2.0 * std::log(1.0 / footFraction));
    const double fixedSigma = result.pulseWidthSamples / gaussianWidthFactor;
    if (!std::isfinite(fixedSigma) || fixedSigma < 0.5)
        return result;
    result.sigma = fixedSigma;
    result.fwhmSamples = fixedSigma * GAUSSIAN_FWHM_FACTOR;

    const double footMidpoint = 0.5 * (leftFoot + rightFoot);

    // 质量比较不再要求单高斯逐点贴合双峰原形，而比较“累计能量到达分布”。
    // 这样右峰强/双峰相近/左峰强都仍可被压缩成一个等效单脉冲。
    pulseForQuality.clear();
    pulseForQuality.reserve(result.right - result.left + 1);
    for (int i = result.left; i <= result.right; ++i)
        pulseForQuality.push_back(positive[i - left]);

    // 中心只能围绕脚底中点小范围调整，避免双峰强弱交换时中心跳到某一侧局部峰。
    double bestCenter = footMidpoint;
    double bestScore = -1.0;
    const double centerFineHalfRange = std::clamp(options.centerFineHalfRange, 0.0, 10.0);
    const double centerMin = std::max(leftFoot,
        footMidpoint - centerFineHalfRange);
    const double centerMax = std::min(rightFoot,
        footMidpoint + centerFineHalfRange);
    const CumulativeEnergyReference energyReference =
        makeCumulativeEnergyReference(pulseForQuality, pulseCdfWorkspace);
    if (!energyReference.valid)
        return result;
    gaussianEnergyWorkspace.clear();
    gaussianEnergyWorkspace.reserve(pulseForQuality.size());

    constexpr double fineStep = 0.01;
    const int fineStepCount = std::max(0, static_cast<int>(
        std::floor((centerMax - centerMin + 1e-12) / fineStep)));
    const double coarseStep = std::clamp(options.centerSearchCoarseStep, fineStep, 1.0);
    const int coarseStride = std::max(1, static_cast<int>(std::llround(coarseStep / fineStep)));

    auto evaluateIndex = [&](int index) {
        const double center = centerMin + fineStep * index;
        const double score = cumulativeEnergyR2AtCenter(
            energyReference, result.left, result.right, center, fixedSigma,
            gaussianEnergyWorkspace);
        if (std::isfinite(score) && score > bestScore) {
            bestScore = score;
            bestCenter = center;
            return true;
        }
        return false;
    };

    if (coarseStride <= 1) {
        // 与原算法等价的0.01 sample全范围穷举，用于回归基线和极限精度模式。
        for (int index = 0; index <= fineStepCount; ++index)
            evaluateIndex(index);
    } else {
        int bestCoarseIndex = 0;
        for (int index = 0; index <= fineStepCount; index += coarseStride) {
            if (evaluateIndex(index))
                bestCoarseIndex = index;
        }
        // 若末端不落在粗网格上，也补测最后一个0.01网格点，避免边界最优被漏掉。
        if (fineStepCount % coarseStride != 0 && evaluateIndex(fineStepCount))
            bestCoarseIndex = fineStepCount;

        // 在最佳粗搜点±一个粗步长内恢复原0.01 sample网格精搜。
        const int refineLeft = std::max(0, bestCoarseIndex - coarseStride);
        const int refineRight = std::min(fineStepCount, bestCoarseIndex + coarseStride);
        for (int index = refineLeft; index <= refineRight; ++index)
            evaluateIndex(index);
    }
    if (bestScore < 0.0)
        return result;

    result.center = bestCenter;
    result.rSquared = bestScore;

    // 用“能量等效”确定高斯幅值：保证脚底窗口内正脉冲能量与等效高斯尽量一致。
    // 相比宽窗口最小二乘，这不会为了照顾大量接近基线的点把峰值压得过低。
    double pulseEnergy = 0.0;
    double gaussianEnergy = 0.0;
    for (int i = result.left; i <= result.right; ++i) {
        const double y = positive[i - left];
        const double normalized = (i - result.center) / fixedSigma;
        const double g = std::exp(-0.5 * normalized * normalized);
        pulseEnergy += y * y;
        gaussianEnergy += g * g;
    }
    if (pulseEnergy <= 1e-24 || gaussianEnergy <= 1e-24)
        return result;

    double amplitude = std::sqrt(pulseEnergy / gaussianEnergy);
    // 防止个别噪声或窗口异常产生离谱幅值；等效高斯峰值限制在实测峰值的合理范围内。
    double amplitudeMinRatio = std::clamp(options.amplitudeMinRatio, 0.05, 3.0);
    double amplitudeMaxRatio = std::clamp(options.amplitudeMaxRatio, amplitudeMinRatio, 5.0);
    amplitude = std::clamp(amplitude,
                           amplitudeMinRatio * peakHeight,
                           amplitudeMaxRatio * peakHeight);
    result.amplitudeEnergy = amplitude * amplitude;
    result.backgroundEnergy = 0.0;

    double residualSum = 0.0;
    for (int i = result.left; i <= result.right; ++i) {
        const double target = positive[i - left];
        const double normalized = (i - result.center) / fixedSigma;
        const double fitted = amplitude * std::exp(-0.5 * normalized * normalized);
        const double delta = target - fitted;
        residualSum += delta * delta;
    }
    result.residualRms = std::sqrt(
        residualSum / std::max(1, result.right - result.left + 1));

    result.solved = std::isfinite(result.center)
        && std::isfinite(result.sigma)
        && std::isfinite(result.rSquared)
        && result.center >= leftFoot && result.center <= rightFoot
        && result.sigma > 0.0;
    return result;
}

} // namespace PacketPeakFit
