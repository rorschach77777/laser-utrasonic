#pragma once

#include <deque>
#include <vector>
#include <limits>

// 工业在线测厚跨帧跟踪模块。
// 迁移LSTAlgorithmTest中的RECENT/EMA模板思想。
// 不包含测试程序状态显示和诊断字符串。

struct TrackingResult
{
    bool locked = false;
    bool accepted = false;
    bool softRecovered = false;
    bool preTrackingRecovered = false;
    bool templateAgreed = false;
    double predictedSample = std::numeric_limits<double>::quiet_NaN();
    double residualSample = std::numeric_limits<double>::quiet_NaN();
    double templateCorrelation = std::numeric_limits<double>::quiet_NaN();
};

class PeakTracker
{
public:
    PeakTracker();

    TrackingResult update(double currentSample,
                          const std::vector<double>& waveform,
                          double fitR2 = 1.0,
                          double candidateRatio = 999.0);

    void reset();

    bool locked() const { return m_locked; }

private:
    void updateTemplate(const std::vector<double>& waveform);
    double templateCorrelation(const std::vector<double>& a,
                               const std::vector<double>& b) const;

    std::deque<double> m_positions;
    std::vector<double> m_recentTemplate;
    std::vector<double> m_emaTemplate;
    std::vector<double> m_shapeTemplate;

    bool m_locked = false;
    bool m_lost = false;
    int m_misses = 0;
    int m_reacquireCount = 0;
};
