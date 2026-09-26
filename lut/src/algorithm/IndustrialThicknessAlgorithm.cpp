#include "IndustrialThicknessAlgorithm.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr double kBiasMm = 1.1;
constexpr double kVelocity = 6459.7;
constexpr double kImpulseMad = 12.0;
constexpr double kMinJump = 0.05;
constexpr int kEnergyWindow = 17;
constexpr double kMinSnrDb = 10.0;
}

double localEnergy(const std::vector<double>& signal, int center, int halfWidth)
{
    if (center - halfWidth < 0 || center + halfWidth >= static_cast<int>(signal.size()))
        return 0.0;

    double mean = 0.0;
    const int count = 2 * halfWidth + 1;
    for (int i = center - halfWidth; i <= center + halfWidth; ++i)
        mean += signal[i];
    mean /= count;

    double energy = 0.0;
    for (int i = center - halfWidth; i <= center + halfWidth; ++i) {
        const double value = signal[i] - mean;
        energy += value * value;
    }

    return energy / count;
}

void IndustrialThicknessAlgorithm::reset()
{
    m_peakHistory.clear();
    m_noiseFloorDb = std::numeric_limits<double>::quiet_NaN();
    m_tracker.reset();
}

bool IndustrialThicknessAlgorithm::detectImpulse(const std::vector<double>& raw)
{
    if (raw.size() < 3)
        return false;

    // 与 LSTAlgorithmTest 保持一致：
    // 1) 使用 raw 一阶差分；
    // 2) MAD 只用于估计稳健尺度；
    // 3) 连续波包边沿不能简单按 MAD 超限判定。
    std::vector<double> differences;
    differences.reserve(raw.size() - 1);
    for (size_t i = 1; i < raw.size(); ++i)
        differences.push_back(raw[i] - raw[i - 1]);

    std::vector<double> medianScratch = differences;
    std::nth_element(medianScratch.begin(),
                     medianScratch.begin() + medianScratch.size() / 2,
                     medianScratch.end());
    const double center = medianScratch[medianScratch.size() / 2];

    std::vector<double> deviations;
    deviations.reserve(differences.size());
    for (double value : differences)
        deviations.push_back(std::abs(value - center));

    std::nth_element(deviations.begin(),
                     deviations.begin() + deviations.size() / 2,
                     deviations.end());
    const double sigma = 1.4826 * deviations[deviations.size() / 2];

    const double threshold = std::max(kMinJump, kImpulseMad * sigma);
    const double localThreshold = std::max(3.0 * sigma, 0.25 * kMinJump);
    const int supportRadius = 8;

    for (int i = 0; i < static_cast<int>(differences.size()); ++i) {
        const double jump = std::abs(differences[i] - center);
        if (jump <= threshold)
            continue;

        int supportCount = 0;
        int firstSupport = -1;
        int lastSupport = -1;
        for (int j = std::max(0, i - supportRadius);
             j <= std::min(static_cast<int>(differences.size()) - 1,
                           i + supportRadius);
             ++j) {
            if (j == i)
                continue;
            if (std::abs(differences[j] - center) > localThreshold) {
                ++supportCount;
                if (firstSupport < 0)
                    firstSupport = j;
                lastSupport = j;
            }
        }

        const bool coherentWaveSupport = supportCount >= 3
            && firstSupport >= 0
            && lastSupport - firstSupport >= 4;

        std::cerr << "[ImpulseDebug] index=" << i
                  << " jump=" << jump
                  << " sigma=" << sigma
                  << " threshold=" << threshold
                  << " support=" << supportCount
                  << " coherent=" << coherentWaveSupport
                  << std::endl;

        // 连续波包边沿允许通过，孤立尖峰拒绝。
        if (!coherentWaveSupport)
            return true;
    }

    return false;
}

double IndustrialThicknessAlgorithm::estimateNoise(const std::vector<double>& signal,
                                                     int excludeLeft,
                                                     int excludeRight)
{
    if (signal.empty())
        return -200.0;

    // 与 LSTAlgorithmTest 保持一致：使用背景窗口局部交流能量估计noiseEnergy。
    // 避免直接使用平方幅值导致基线漂移/DC分量影响SNR。
    std::vector<double> backgroundDb;
    const int halfWidth = kEnergyWindow / 2;

    for (int center = halfWidth;
         center + halfWidth < static_cast<int>(signal.size());
         center += kEnergyWindow) {
        if (excludeLeft >= 0 && excludeRight >= excludeLeft
            && center + halfWidth >= excludeLeft
            && center - halfWidth <= excludeRight)
            continue;

        const double energy = localEnergy(signal, center, halfWidth);
        if (energy > 0.0)
            backgroundDb.push_back(10.0 * std::log10(std::max(energy, 1e-24)));
    }

    if (backgroundDb.empty()) {
        m_noiseEnergy = 1e-24;
        return -240.0;
    }

    std::sort(backgroundDb.begin(), backgroundDb.end());
    const double medianDb = backgroundDb[backgroundDb.size() / 2];

    std::vector<double> deviation;
    deviation.reserve(backgroundDb.size());
    for (double value : backgroundDb)
        deviation.push_back(std::abs(value - medianDb));
    std::sort(deviation.begin(), deviation.end());

    const double mad = deviation[deviation.size() / 2];
    const double threshold = medianDb + 3.0 * 1.4826 * mad;

    double sumEnergy = 0.0;
    int count = 0;
    for (double db : backgroundDb) {
        if (db <= threshold) {
            sumEnergy += std::pow(10.0, db / 10.0);
            ++count;
        }
    }

    m_noiseEnergy = std::max(sumEnergy / std::max(count, 1), 1e-24);
    const double noiseDb = 10.0 * std::log10(m_noiseEnergy);

    if (std::isfinite(m_noiseFloorDb))
        m_noiseFloorDb = 0.85 * m_noiseFloorDb + 0.15 * noiseDb;
    else
        m_noiseFloorDb = noiseDb;

    return m_noiseFloorDb;
}

double IndustrialThicknessAlgorithm::findPeak(const std::vector<double>& signal,
                                              int left,
                                              int right)
{
    left = std::max(left, 0);
    right = std::min(right, static_cast<int>(signal.size()) - 1);

    int index = left;
    double value = -1e30;
    for (int i = left; i <= right; ++i) {
        if (signal[i] > value) {
            value = signal[i];
            index = i;
        }
    }

    // 涓夌偣鎶涚墿绾夸簹閲囨牱锛屼笌娴嬭瘯绋嬪簭fallback璺寰勪繚鎸佷竴鑷淬
    double delta = 0.0;
    if (index > left && index < right) {
        const double y1 = signal[index - 1];
        const double y2 = signal[index];
        const double y3 = signal[index + 1];
        const double c = y1 - 2.0 * y2 + y3;
        if (c < -1e-15)
            delta = std::clamp(0.5 * (y1 - y3) / c, -0.5, 0.5);
    }

    return index + delta;
}

IndustrialThicknessResult IndustrialThicknessAlgorithm::process(
    const std::vector<double>& raw,
    const std::vector<double>& filtered,
    double sampleRateHz,
    double thicknessVelocity,
    int delayCount)
{
    IndustrialThicknessResult result;

    if (detectImpulse(raw)) {
        result.invalidPulse = true;
        return result;
    }

    // 与 LSTAlgorithmTest 保持一致：优先使用首波理论搜索窗。
    // 当前离线测试没有外部gate参数，因此根据delayCount建立保护窗口，
    // 避免整段波形搜索导致后续回波/噪声峰竞争。
    int peakLeft = std::max(delayCount, 0);
    constexpr int kSearchMarginSamples = 160;
    int peakRight = std::min(static_cast<int>(filtered.size()) - 1,
                              peakLeft + kSearchMarginSamples);
    if (peakLeft >= peakRight)
        return result;

    // 棣栧厛閫氳繃绛夋晥鍗曡剦鍐叉嫙鍚堣幏鍙栫ǔ瀹歍oF銆傛嫙鍚堝け璐ユ椂淇濈暀鍘熷崟宄癴allback銆
    double peak = std::numeric_limits<double>::quiet_NaN();

    IndustrialPacketPeakFit::FitOptions fitOptions;
    IndustrialPacketPeakFit::Fit fit = IndustrialPacketPeakFit::fit(
        filtered,
        peakLeft,
        peakRight,
        "auto",
        fitOptions,
        &m_fitWorkspace);

    if (fit.solved && std::isfinite(fit.center)) {
        peak = fit.center;
    } else {
        peak = findPeak(filtered, peakLeft, peakRight);
    }

    // LOCK_GRACE_PRE_PACKET_RECOVERY:
    // 当稳定LOCK状态下波包分割/拟合阶段失败时，不立即进入NO_SIGNAL。
    // 在packet生成之前利用历史ToF建立窄搜索窗，恢复弱化或形变波包身份。
    // 该路径只允许LOCK连续性恢复，不改变普通搜索结果。
    const bool lockGracePrePacket = m_tracker.locked()
        && !m_peakHistory.empty()
        && (!fit.solved || !std::isfinite(fit.center));

    if (lockGracePrePacket) {
        const int predicted = static_cast<int>(std::round(m_peakHistory.back()));
        constexpr int graceWindow = 18;
        const int graceLeft = std::max(peakLeft, predicted - graceWindow);
        const int graceRight = std::min(peakRight, predicted + graceWindow);

        if (graceLeft < graceRight) {
            const double gracePeak = findPeak(filtered, graceLeft, graceRight);
            const double graceEnergy = localEnergy(filtered,
                                                   static_cast<int>(std::round(gracePeak)),
                                                   8);
            if (std::isfinite(gracePeak) && graceEnergy > 0.0) {
                peak = gracePeak;
                std::cerr << "[LOCK_GRACE_PRE_PACKET_RECOVERY] peak="
                          << peak << " window=" << graceLeft << "-"
                          << graceRight << std::endl;
            }
        }
    }

    if (!std::isfinite(peak))
        return result;

    // 临时调试输出：用于与 LSTAlgorithmTest 对比峰值和搜索窗。
    std::cerr << "[IndustrialAlgorithm] search=" << peakLeft << "-" << peakRight
              << " fitPeak=" << peak
              << " fitSolved=" << fit.solved
              << " fitR2=" << fit.rSquared << std::endl;

    const int noiseExcludeCenter = static_cast<int>(std::round(peak));
    // 与 LSTAlgorithmTest 保持逻辑一致：噪声估计只排除完整波包区域，
    // 不通过调节阈值降低噪声。kEnergyWindow为已有默认能量窗口，
    // 使用其整数倍作为波包保护范围，避免首波包尾部污染背景统计。
    constexpr int noiseGuardWindows = 3;
    const int noiseGuard = noiseGuardWindows * kEnergyWindow;
    const double noiseDb = estimateNoise(filtered,
                                         noiseExcludeCenter - noiseGuard,
                                         noiseExcludeCenter + noiseGuard);

    // 与 LSTAlgorithmTest 保持一致：SNR使用候选波包窗口内最大局部能量。
    // 不修改默认窗口参数，仅将计算方式从单一peak中心迁移为packet能量搜索。
    const int snrCenter = std::clamp(static_cast<int>(std::round(peak)),
                                     0,
                                     static_cast<int>(filtered.size()) - 1);
    constexpr int packetEnergyHalfWindow = 24;
    const int packetSearchLeft = std::max(packetEnergyHalfWindow,
                                          snrCenter - packetEnergyHalfWindow);
    const int packetSearchRight = std::min(static_cast<int>(filtered.size()) - 1 - packetEnergyHalfWindow,
                                           snrCenter + packetEnergyHalfWindow);

    double packetEnergy = 0.0;
    for (int i = packetSearchLeft; i <= packetSearchRight; ++i)
        packetEnergy = std::max(packetEnergy,
                                localEnergy(filtered, i, packetEnergyHalfWindow));

    const double backgroundEnergy = std::max(m_noiseEnergy, 1e-24);
    result.snrDb = 10.0 * std::log10(std::max(packetEnergy, 1e-24)
                                     / backgroundEnergy);

    std::cerr << "[IndustrialAlgorithm] noiseEnergy=" << m_noiseEnergy
              << " packetEnergy=" << packetEnergy
              << " snrDb=" << result.snrDb << std::endl;

    if (result.snrDb < kMinSnrDb) {
        result.noSignal = true;
        return result;
    }

    // LOCK鐘舵佷笅鍒╃敤鍘嗗彶ToF瓒嬪娍銆佹ā鏉夸竴鑷存у拰寮变俊鍙疯蒋鎭㈠嶇害鏉熻烦鍙樸
    std::vector<double> localPacket;
    const int packetCenter = std::clamp(static_cast<int>(std::round(peak)),
                                        0,
                                        static_cast<int>(filtered.size()) - 1);
    const int packetLeft = std::max(0, packetCenter - 24);
    const int packetRight = std::min(static_cast<int>(filtered.size()) - 1,
                                     packetCenter + 24);
    localPacket.assign(filtered.begin() + packetLeft,
                       filtered.begin() + packetRight + 1);

    const double fitQuality = fit.solved
        ? std::max(0.0, fit.rSquared)
        : 0.55;

    // 鐙绔嬫尝鍖呭欓夋娴嬶細淇濇寔LSTAlgorithmTest涓鐨勫弬鏁伴昏緫銆
    // LOCK鐘舵佷笅寮鍚痯rediction gate锛岄伩鍏嶆梺鐡/绗浜屽洖娉㈢珵浜夈
    CandidateDetectorParameters candidateParams;

    int predictionLeft = -1;
    int predictionRight = -1;
    const bool lockActive = m_tracker.locked();

    if (lockActive && !m_peakHistory.empty()) {
        const int predicted = static_cast<int>(std::round(m_peakHistory.back()));
        constexpr int predictionGateSamples = 20;
        predictionLeft = std::max(packetLeft, predicted - predictionGateSamples);
        predictionRight = std::min(packetRight, predicted + predictionGateSamples);
    }

    const double noiseEnergy = std::isfinite(m_noiseEnergy)
        ? m_noiseEnergy
        : 1e-12;

    const CandidateResult candidates = m_candidateDetector.detect(
        filtered,
        packetLeft,
        packetRight,
        noiseEnergy,
        candidateParams,
        predictionLeft,
        predictionRight,
        lockActive);

    const double candidateRatio = candidates.ratio;
    if (candidates.valid && candidateRatio < 1.5)
        return result;

    TrackingResult tracking = m_tracker.update(peak,
                                                localPacket,
                                                fitQuality,
                                                candidateRatio);
    if (!tracking.accepted)
        return result;

    // 杈撳嚭涓嶭STAlgorithmTest涓鑷寸殑璐ㄩ噺璇勪环淇℃伅銆
    // 浠呭炲姞缁撴灉璁板綍锛屼笉鏀瑰彉鍘熸湁鎺ュ彈/鎷掔粷閫昏緫銆
    result.candidateCount = candidates.count;
    result.candidateRatio = candidates.ratio;
    result.firstSignificance = candidates.firstSignificance;
    result.secondSignificance = candidates.secondSignificance;
    result.fitR2 = fitQuality;
    result.trackingCorrelation = tracking.templateCorrelation;

    // ToF定义为相对于系统固定延时补偿后的传播时间。
    // LSTAlgorithmTest中输出ToF不包含delayCount，因此保持一致。
    result.tofSample = peak - delayCount;
    const double tof = result.tofSample / sampleRateHz;
    result.thicknessMm = tof * thicknessVelocity * 1000.0 / 2.0 + kBiasMm;
    result.valid = true;

    m_peakHistory.push_back(peak);
    if (m_peakHistory.size() > 5)
        m_peakHistory.pop_front();

    return result;
}
