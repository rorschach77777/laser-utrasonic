#include "PeakTracker.h"

#include <algorithm>
#include <cmath>

PeakTracker::PeakTracker() = default;

TrackingResult PeakTracker::update(double currentSample,
                                   const std::vector<double>& waveform,
                                   double fitR2,
                                   double candidateRatio)
{
    TrackingResult result;

    if (!std::isfinite(currentSample)) {
        ++m_misses;
        m_lost = true;
        if (m_misses > 3) {
            m_locked = false;
            m_positions.clear();
        }
        return result;
    }

    double predicted = currentSample;
    if (!m_positions.empty()) {
        predicted = m_positions.back();
        if (m_positions.size() >= 2)
            predicted += m_positions.back() - *(m_positions.rbegin() + 1);
    }

    result.predictedSample = predicted;
    result.residualSample = currentSample - predicted;

    // 多模板一致性：RECENT/EMA/SHAPE至少需要一个稳定相关，同时不能允许
    // 低质量拟合或第二候选过近时覆盖历史身份。
    const double recentCorr = templateCorrelation(waveform, m_recentTemplate);
    const double emaCorr = templateCorrelation(waveform, m_emaTemplate);
    const double shapeCorr = templateCorrelation(waveform, m_shapeTemplate);
    result.templateCorrelation = std::max({recentCorr, emaCorr, shapeCorr});
    result.templateAgreed = result.templateCorrelation >= 0.65;

    const bool qualityOk = fitR2 >= 0.50 && candidateRatio >= 1.5;

    // LOCK状态使用严格位置约束，避免热态双峰交换导致跳峰。
    const bool normalLock = std::abs(result.residualSample) <= 9.0
        && result.templateAgreed
        && qualityOk;

    // Soft recovery: 弱SNR状态只能由高质量模板和拟合结果恢复。
    const bool softRecovery = m_locked
        && std::abs(result.residualSample) <= 3.0
        && result.templateCorrelation >= 0.84
        && fitR2 >= 0.95
        && candidateRatio >= 1.5;

    // LOCK_GRACE_PRE_TRACKING_RECOVERY:
    // 在已经LOCK的情况下，如果tracking预测身份失败，但当前候选仍与历史模板一致，
    // 不立即进入LOST。该通道只处理TRACKING_MISMATCH/TRACKING_JUMP边界情况，
    // 不改变正常candidate/SNR判据。
    // 形态鲁棒身份恢复：波包突变时模板相关可能快速下降，不能再作为硬拒绝条件。
    // 使用位置连续性 + 拟合质量 + 候选唯一性判断身份是否仍可信。
    // 模板相关降级为附加质量指标。
    const bool identityContinuity = std::abs(result.residualSample) <= 8.0
        && fitR2 >= 0.90
        && candidateRatio >= 1.2;

    const bool shapeDegradedButRecoverable = result.templateCorrelation >= 0.35;

    const bool preTrackingRecovery = m_locked
        && !normalLock
        && !softRecovery
        && identityContinuity
        && shapeDegradedButRecoverable;

    if (!normalLock && !softRecovery && !preTrackingRecovery && m_locked) {
        ++m_misses;
        m_lost = true;
        return result;
    }

    result.softRecovered = softRecovery && !normalLock;
    result.preTrackingRecovered = preTrackingRecovery && !normalLock && !softRecovery;

    // LOST状态重捕获：连续两帧位置一致后恢复LOCK。
    if (m_lost) {
        if (std::abs(result.residualSample) <= 5.0)
            ++m_reacquireCount;
        else
            m_reacquireCount = 0;

        if (m_reacquireCount < 2) {
            result.accepted = false;
            return result;
        }
        m_lost = false;
    }

    result.accepted = true;
    m_misses = 0;

    m_positions.push_back(currentSample);
    while (m_positions.size() > 5)
        m_positions.pop_front();

    m_locked = m_positions.size() >= 3;
    result.locked = m_locked;

    updateTemplate(waveform);
    return result;
}

void PeakTracker::updateTemplate(const std::vector<double>& waveform)
{
    if (waveform.empty())
        return;

    if (m_recentTemplate.empty()) {
        m_recentTemplate = waveform;
        m_emaTemplate = waveform;
        m_shapeTemplate = waveform;
        return;
    }

    const size_t count = std::min(m_recentTemplate.size(), waveform.size());
    for (size_t i = 0; i < count; ++i) {
        m_recentTemplate[i] = waveform[i];
        m_emaTemplate[i] = 0.85 * m_emaTemplate[i] + 0.15 * waveform[i];
        m_shapeTemplate[i] = 0.70 * m_shapeTemplate[i] + 0.30 * waveform[i];
    }
}

double PeakTracker::templateCorrelation(const std::vector<double>& a,
                                       const std::vector<double>& b) const
{
    const size_t count = std::min(a.size(), b.size());
    if (count < 8)
        return 0.0;

    double ma = 0.0;
    double mb = 0.0;
    for (size_t i = 0; i < count; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= count;
    mb /= count;

    double num = 0.0;
    double da = 0.0;
    double db = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double va = a[i] - ma;
        const double vb = b[i] - mb;
        num += va * vb;
        da += va * va;
        db += vb * vb;
    }

    return (da > 1e-12 && db > 1e-12)
        ? num / std::sqrt(da * db)
        : 0.0;
}

void PeakTracker::reset()
{
    m_positions.clear();
    m_recentTemplate.clear();
    m_emaTemplate.clear();
    m_shapeTemplate.clear();
    m_locked = false;
    m_lost = false;
    m_misses = 0;
    m_reacquireCount = 0;
}
