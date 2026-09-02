#include "ProcessAnalyzer.h"
#include "HistoryManager.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

static int nextPowerOfTwo(int value)
{
    int n = 1;
    while (n < value) n <<= 1;
    return n;
}

static void fft(std::vector<std::complex<double>> &a)
{
    const int n = static_cast<int>(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * PI / len;
        const std::complex<double> wLen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wLen;
            }
        }
    }
}

static QVector<double> interpolateInvalid(const QVector<double> &source)
{
    QVector<double> result = source;
    if (result.isEmpty()) return result;

    int i = 0;
    while (i < result.size()) {
        if (std::isfinite(result[i])) {
            ++i;
            continue;
        }

        const int begin = i;
        while (i < result.size() && !std::isfinite(result[i])) ++i;
        const int end = i - 1;
        const bool hasLeft = begin > 0 && std::isfinite(result[begin - 1]);
        const bool hasRight = i < result.size() && std::isfinite(result[i]);

        if (hasLeft && hasRight) {
            const double left = result[begin - 1];
            const double right = result[i];
            const int count = end - begin + 1;
            for (int k = 0; k < count; ++k) {
                const double t = static_cast<double>(k + 1) / (count + 1);
                result[begin + k] = left + (right - left) * t;
            }
        } else if (hasLeft) {
            for (int k = begin; k <= end; ++k) result[k] = result[begin - 1];
        } else if (hasRight) {
            for (int k = begin; k <= end; ++k) result[k] = result[i];
        } else {
            std::fill(result.begin() + begin, result.begin() + end + 1, 0.0);
        }
    }
    return result;
}

} // namespace

ProcessAnalyzer::ProcessAnalyzer(QObject *parent)
    : QObject(parent)
    , m_historyManager(nullptr)
    , m_hasAnalysis(false)
    , m_hasBatchAnalysis(false)
{
}

void ProcessAnalyzer::setHistoryManager(HistoryManager *manager)
{
    m_historyManager = manager;
}

void ProcessAnalyzer::clear()
{
    m_result.clear();
    m_spectrum.clear();
    m_thicknessProfile.clear();
    m_hasAnalysis = false;
    emit analysisChanged();
}

void ProcessAnalyzer::clearBatch()
{
    m_batchResult.clear();
    m_batchTrend.clear();
    m_hasBatchAnalysis = false;
    emit batchAnalysisChanged();
}

bool ProcessAnalyzer::analyzeRecord(int index)
{
    return analyzeRecord(index, 1.0, 0.05, 0.65);
}

bool ProcessAnalyzer::analyzeRecord(int index,
                                    double cutoffWavelengthM,
                                    double minVariationMm,
                                    double dominanceThreshold)
{
    clear();
    if (!m_historyManager) {
        emit errorOccurred(tr("History manager is not available."));
        return false;
    }
    if (cutoffWavelengthM <= 0.0) cutoffWavelengthM = 1.0;
    dominanceThreshold = qBound(0.5, dominanceThreshold, 0.95);
    minVariationMm = qMax(0.0, minVariationMm);

    m_historyManager->loadRecordData(index);
    const QVariantMap record = m_historyManager->getRecord(index);
    QVector<double> rawThickness = m_historyManager->getRecordData(index);
    QVector<double> thickness = rawThickness;
    QVector<double> position = m_historyManager->getRecordPositionData(index);

    if (thickness.size() < 8) {
        emit errorOccurred(tr("Not enough thickness points for process analysis."));
        return false;
    }

    int validCount = 0;
    for (double value : thickness) {
        if (std::isfinite(value)) ++validCount;
    }
    if (validCount < 8) {
        emit errorOccurred(tr("Not enough valid thickness points for process analysis."));
        return false;
    }

    thickness = interpolateInvalid(thickness);
    const QVector<double> profileThickness = thickness;

    double mean = 0.0;
    for (double value : thickness) mean += value;
    mean /= thickness.size();

    double variance = 0.0;
    for (double &value : thickness) {
        value -= mean;
        variance += value * value;
    }
    variance /= thickness.size();
    const double stdDev = std::sqrt(qMax(0.0, variance));

    bool spatialAxis = position.size() == thickness.size();
    double dx = 1.0;
    if (spatialAxis) {
        double sumDx = 0.0;
        int dxCount = 0;
        for (int i = 1; i < position.size(); ++i) {
            if (std::isfinite(position[i]) && std::isfinite(position[i - 1])) {
                const double delta = position[i] - position[i - 1];
                if (delta > 0.0) {
                    sumDx += delta;
                    ++dxCount;
                }
            }
        }
        if (dxCount > 0) {
            dx = sumDx / dxCount;
        } else {
            spatialAxis = false;
            dx = 1.0;
        }
    }

    m_thicknessProfile.reserve(profileThickness.size());
    for (int i = 0; i < profileThickness.size(); ++i) {
        QVariantMap point;
        point["x"] = spatialAxis ? position[i] : static_cast<double>(i + 1);
        point["thickness"] = profileThickness[i];
        point["valid"] = std::isfinite(rawThickness[i]);
        m_thicknessProfile.append(point);
    }

    const int nfft = nextPowerOfTwo(thickness.size());
    std::vector<std::complex<double>> bins(nfft, {0.0, 0.0});
    for (int i = 0; i < thickness.size(); ++i) {
        const double window = thickness.size() > 1
            ? 0.5 * (1.0 - std::cos(2.0 * PI * i / (thickness.size() - 1)))
            : 1.0;
        bins[i] = thickness[i] * window;
    }
    fft(bins);

    const double sampleSpacing = dx;
    const double freqStep = 1.0 / (nfft * sampleSpacing);
    const double cutoffFrequency = spatialAxis ? 1.0 / cutoffWavelengthM : 0.1;

    double longEnergy = 0.0;
    double shortEnergy = 0.0;
    double totalEnergy = 0.0;
    double dominantAmplitude = 0.0;
    double dominantFrequency = 0.0;

    const int half = nfft / 2;
    m_spectrum.reserve(qMin(half - 1, 512));
    const int spectrumStride = qMax(1, (half - 1) / 512);

    for (int k = 1; k < half; ++k) {
        const double frequency = k * freqStep;
        const double amplitude = 2.0 * std::abs(bins[k]) / thickness.size();
        const double energy = std::norm(bins[k]);
        totalEnergy += energy;

        if (frequency <= cutoffFrequency)
            longEnergy += energy;
        else
            shortEnergy += energy;

        if (amplitude > dominantAmplitude) {
            dominantAmplitude = amplitude;
            dominantFrequency = frequency;
        }

        if (k % spectrumStride == 0) {
            QVariantMap point;
            point["frequency"] = frequency;
            point["amplitude"] = amplitude;
            point["wavelength"] = frequency > 0.0 ? 1.0 / frequency : 0.0;
            m_spectrum.append(point);
        }
    }

    const double longRatio = totalEnergy > 0.0 ? longEnergy / totalEnergy : 0.0;
    const double shortRatio = totalEnergy > 0.0 ? shortEnergy / totalEnergy : 0.0;
    const double dominantWavelength = dominantFrequency > 0.0 ? 1.0 / dominantFrequency : 0.0;

    const bool variationSignificant = stdDev >= minVariationMm;
    const bool longAbnormal = variationSignificant && longRatio >= dominanceThreshold;
    const bool shortAbnormal = variationSignificant && shortRatio >= dominanceThreshold;

    QString diagnosis;
    if (longAbnormal && shortAbnormal) {
        diagnosis = tr("长、短周期成分均明显，存在复合型纵向壁厚波动特征。建议同时检查管坯加热均匀性和穿孔顶杆状态。");
    } else if (longAbnormal) {
        diagnosis = tr("长周期壁厚成分占主导，特征与管坯加热不均导致的长周期偏心相符。建议检查环形炉温度均匀性、加热时间和布料状态。");
    } else if (shortAbnormal) {
        diagnosis = tr("短周期壁厚成分占主导，特征与穿孔顶杆直度不足或振动导致的短周期偏心相符。建议检查顶杆直度、刚性和连接件状态。");
    } else {
        diagnosis = tr("当前纵向壁厚波动未达到初始诊断阈值。阈值需结合现场钢种、规格和工艺数据进一步标定。");
    }

    double pipeLengthM = record.value("pipeLengthM").toDouble();
    if (pipeLengthM <= 0.0 && spatialAxis && position.size() > 1)
        pipeLengthM = position.last() - position.first();

    const double validRatePercent = rawThickness.isEmpty()
        ? 0.0
        : 100.0 * static_cast<double>(validCount) / rawThickness.size();

    m_result["recordIndex"] = index;
    m_result["title"] = record.value("title");
    m_result["pipeId"] = record.value("pipeId");
    m_result["steelGrade"] = record.value("steelGrade");
    m_result["nominalThicknessMm"] = record.value("nominalThicknessMm");
    m_result["pipeLengthM"] = pipeLengthM;
    m_result["validRatePercent"] = validRatePercent;
    m_result["pointCount"] = thickness.size();
    m_result["validCount"] = validCount;
    m_result["spatialAxis"] = spatialAxis;
    m_result["sampleSpacing"] = sampleSpacing;
    m_result["cutoffFrequency"] = cutoffFrequency;
    m_result["stdDevMm"] = stdDev;
    m_result["cutoffWavelengthM"] = cutoffWavelengthM;
    m_result["longEnergyRatio"] = longRatio;
    m_result["shortEnergyRatio"] = shortRatio;
    m_result["dominantFrequency"] = dominantFrequency;
    m_result["dominantWavelength"] = dominantWavelength;
    m_result["dominantAmplitudeMm"] = dominantAmplitude;
    m_result["longAbnormal"] = longAbnormal;
    m_result["shortAbnormal"] = shortAbnormal;
    m_result["diagnosis"] = diagnosis;
    //m_result["thresholdNote"] = tr("当前长/短周期分界及报警阈值为软件初始参数，不能替代现场工艺标定。");
    m_result["thresholdNote"] = tr("");

    m_hasAnalysis = true;
    emit analysisChanged();
    return true;
}

bool ProcessAnalyzer::analyzeBatch(int maxRecords,
                                   double rangeRateThresholdPercent,
                                   double minValidRatePercent,
                                   int plugCount,
                                   int cycleTolerancePipes)
{
    clearBatch();
    if (!m_historyManager) {
        emit errorOccurred(tr("History manager is not available."));
        return false;
    }

    const int totalRecords = m_historyManager->recordCount();
    if (totalRecords < 3) {
        emit errorOccurred(tr("At least three pipe records are required for batch analysis."));
        return false;
    }

    maxRecords = qBound(3, maxRecords, totalRecords);
    rangeRateThresholdPercent = qMax(0.0, rangeRateThresholdPercent);
    minValidRatePercent = qBound(0.0, minValidRatePercent, 100.0);
    plugCount = qMax(0, plugCount);
    cycleTolerancePipes = qBound(0, cycleTolerancePipes, 3);

    QVector<int> abnormalSequences;
    int qualifiedCount = 0;

    // HistoryManager keeps newest record at index 0. Analyze the most recent
    // maxRecords records, but expose them to the UI in chronological order.
    for (int sequence = 1; sequence <= maxRecords; ++sequence) {
        const int historyIndex = maxRecords - sequence;
        m_historyManager->loadRecordData(historyIndex);

        const QVariantMap record = m_historyManager->getRecord(historyIndex);
        const QVector<double> thickness = m_historyManager->getRecordData(historyIndex);
        const QVector<int> validFlags = m_historyManager->getRecordValidData(historyIndex);

        double sum = 0.0;
        double sumSq = 0.0;
        double minThickness = std::numeric_limits<double>::max();
        double maxThickness = -std::numeric_limits<double>::max();
        int validCount = 0;

        for (int i = 0; i < thickness.size(); ++i) {
            const bool flaggedValid = validFlags.size() == thickness.size()
                ? validFlags[i] != 0
                : true;
            const double value = thickness[i];
            if (!flaggedValid || !std::isfinite(value))
                continue;

            ++validCount;
            sum += value;
            sumSq += value * value;
            minThickness = qMin(minThickness, value);
            maxThickness = qMax(maxThickness, value);
        }

        const int pointCount = thickness.size();
        const double validRate = pointCount > 0
            ? static_cast<double>(validCount) / pointCount * 100.0
            : 0.0;
        const bool statsAvailable = validCount > 0;
        const double mean = statsAvailable ? sum / validCount : 0.0;
        const double range = statsAvailable ? maxThickness - minThickness : 0.0;
        const double variance = statsAvailable
            ? qMax(0.0, sumSq / validCount - mean * mean)
            : 0.0;
        const double stdDev = std::sqrt(variance);

        const double nominalThickness = record.value("nominalThicknessMm").toDouble();
        const double denominator = nominalThickness > 0.0
            ? nominalThickness
            : std::fabs(mean);
        const double rangeRate = denominator > 1e-12
            ? range / denominator * 100.0
            : 0.0;

        const bool qualified = statsAvailable && validRate >= minValidRatePercent;
        const bool abnormal = qualified && rangeRate >= rangeRateThresholdPercent;
        if (qualified) ++qualifiedCount;
        if (abnormal) abnormalSequences.append(sequence);

        QVariantMap point;
        point["sequence"] = sequence;
        point["historyIndex"] = historyIndex;
        point["title"] = record.value("title");
        point["pipeId"] = record.value("pipeId");
        point["meanThicknessMm"] = mean;
        point["thicknessRangeMm"] = range;
        point["rangeRatePercent"] = rangeRate;
        point["stdDevThicknessMm"] = stdDev;
        point["validRatePercent"] = validRate;
        point["qualified"] = qualified;
        point["abnormal"] = abnormal;
        m_batchTrend.append(point);
    }

    QVector<int> intervals;
    for (int i = 1; i < abnormalSequences.size(); ++i)
        intervals.append(abnormalSequences[i] - abnormalSequences[i - 1]);

    int detectedCycle = 0;
    double cycleMatchRatio = 0.0;
    bool cycleDetected = false;

    if (intervals.size() >= 2) {
        QVector<int> sortedIntervals = intervals;
        std::sort(sortedIntervals.begin(), sortedIntervals.end());
        const int mid = sortedIntervals.size() / 2;
        const double medianInterval = sortedIntervals.size() % 2 == 0
            ? 0.5 * (sortedIntervals[mid - 1] + sortedIntervals[mid])
            : sortedIntervals[mid];
        detectedCycle = qMax(1, qRound(medianInterval));

        int matchingIntervals = 0;
        for (int interval : intervals) {
            if (std::abs(interval - detectedCycle) <= cycleTolerancePipes)
                ++matchingIntervals;
        }
        cycleMatchRatio = static_cast<double>(matchingIntervals) / intervals.size();
        cycleDetected = cycleMatchRatio >= 0.67;
    }

    double plugIntervalMatchRatio = 0.0;
    double phaseMatchRatio = 0.0;
    int suspectPlugPosition = 0;
    bool plugSuspected = false;

    if (plugCount > 0 && !abnormalSequences.isEmpty()) {
        if (!intervals.isEmpty()) {
            int plugIntervalMatches = 0;
            for (int interval : intervals) {
                if (std::abs(interval - plugCount) <= cycleTolerancePipes)
                    ++plugIntervalMatches;
            }
            plugIntervalMatchRatio = static_cast<double>(plugIntervalMatches) / intervals.size();
        }

        QVector<int> phaseCounts(plugCount, 0);
        for (int sequence : abnormalSequences)
            ++phaseCounts[(sequence - 1) % plugCount];

        int bestPhase = 0;
        int bestPhaseCount = 0;
        for (int phase = 0; phase < phaseCounts.size(); ++phase) {
            if (phaseCounts[phase] > bestPhaseCount) {
                bestPhase = phase;
                bestPhaseCount = phaseCounts[phase];
            }
        }
        suspectPlugPosition = bestPhase + 1;
        phaseMatchRatio = static_cast<double>(bestPhaseCount) / abnormalSequences.size();

        plugSuspected = abnormalSequences.size() >= 3
            && plugIntervalMatchRatio >= 0.67
            && phaseMatchRatio >= 0.67;
    }

    QString diagnosis;
    if (qualifiedCount < 3) {
        diagnosis = tr("有效钢管记录不足，无法进行可靠的跨钢管周期诊断。请先检查测量有效率和历史记录完整性。");
    } else if (abnormalSequences.isEmpty()) {
        diagnosis = tr("当前批次未发现超过极差率阈值的钢管，暂未观察到论文所描述的固定周期极差率异常特征。");
    } else if (abnormalSequences.size() < 3) {
        diagnosis = tr("发现 %1 支极差率异常钢管，但异常次数不足以确认稳定周期。建议继续积累后续钢管数据。").arg(abnormalSequences.size());
    } else if (plugSuspected) {
        diagnosis = tr("极差率异常以固定间隔重复，且异常间隔与在线顶头数量基本一致；异常还集中在同一轮换相位。该特征与成组穿孔顶头中存在单支偏磨、偏心或圆度异常相符。建议结合现场顶头轮换顺序重点检查第 %1 个相位对应的顶头。").arg(suspectPlugPosition);
    } else if (cycleDetected && plugCount <= 0) {
        diagnosis = tr("检测到约每 %1 支钢管重复一次的极差率异常周期。若输入现场在线顶头数量，可进一步判断该周期是否与成组顶头轮换一致。").arg(detectedCycle);
    } else if (cycleDetected && plugCount > 0) {
        diagnosis = tr("检测到约每 %1 支钢管重复一次的极差率异常，但与当前输入的 %2 支在线顶头数量匹配不足。暂不能将异常直接归因于单支穿孔顶头，建议检查记录缺失、顶头轮换顺序及其他周期性工艺因素。")
            .arg(detectedCycle)
            .arg(plugCount);
    } else {
        diagnosis = tr("存在极差率异常钢管，但异常间隔不稳定，尚未形成可确认的固定周期。建议继续积累批次数据并检查异常是否与具体工模具轮换对应。");
    }

    QVariantList abnormalList;
    for (int sequence : abnormalSequences)
        abnormalList.append(sequence);

    m_batchResult["analyzedCount"] = maxRecords;
    m_batchResult["qualifiedCount"] = qualifiedCount;
    m_batchResult["abnormalCount"] = abnormalSequences.size();
    m_batchResult["rangeRateThresholdPercent"] = rangeRateThresholdPercent;
    m_batchResult["minValidRatePercent"] = minValidRatePercent;
    m_batchResult["plugCount"] = plugCount;
    m_batchResult["cycleTolerancePipes"] = cycleTolerancePipes;
    m_batchResult["detectedCyclePipes"] = detectedCycle;
    m_batchResult["cycleMatchPercent"] = cycleMatchRatio * 100.0;
    m_batchResult["plugIntervalMatchPercent"] = plugIntervalMatchRatio * 100.0;
    m_batchResult["suspectPlugPosition"] = suspectPlugPosition;
    m_batchResult["phaseMatchPercent"] = phaseMatchRatio * 100.0;
    m_batchResult["cycleDetected"] = cycleDetected;
    m_batchResult["plugSuspected"] = plugSuspected;
    m_batchResult["abnormalSequences"] = abnormalList;
    m_batchResult["diagnosis"] = diagnosis;
    m_batchResult["formulaNote"] = tr("当前软件极差率定义：100% × (最大壁厚－最小壁厚) / 基准壁厚；有公称壁厚时使用公称壁厚，否则使用该管平均壁厚。论文未给出极差率具体公式，现场应用前应按企业质量标准确认。");
    m_batchResult["sequenceNote"] = tr("批次序号按所选最近记录从旧到新重新编号，仅用于趋势和周期识别。");

    m_hasBatchAnalysis = true;
    emit batchAnalysisChanged();
    return true;
}
