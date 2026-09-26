#pragma once

#include "PacketSpline.h"

#include <vector>

// 工业实时算法工作区：由ThicknessProcessor持有并跨帧复用。
// 工作区只保存计算缓冲，不保存失败原因或任何用于界面绘制的拟合曲线。
struct AlgorithmWorkspace
{
    std::vector<double> rawDifferences;
    std::vector<double> rawDeviations;
    std::vector<double> rawMedianScratch;
    std::vector<double> rawLocalDeviations;
    std::vector<bool> rawContaminated;

    std::vector<double> noiseBackgroundDb;
    std::vector<double> noiseMedianScratch;
    std::vector<double> noiseDeviations;
    std::vector<double> noiseAcceptedDb;

    std::vector<double> energies;
    PacketPeakFit::FitWorkspace fit;

    void reserve(int samples)
    {
        if (samples <= 0)
            return;

        rawDifferences.reserve(samples);
        rawDeviations.reserve(samples);
        rawMedianScratch.reserve(samples);
        rawLocalDeviations.reserve(64);
        rawContaminated.reserve(samples);
        noiseBackgroundDb.reserve(samples);
        noiseMedianScratch.reserve(samples);
        noiseDeviations.reserve(samples);
        noiseAcceptedDb.reserve(samples);
        energies.reserve(samples);

        fit.residual.reserve(128);
        fit.positive.reserve(128);
        fit.footShape.reserve(128);
        fit.pulseCdf.reserve(128);
        fit.gaussianEnergy.reserve(128);
    }
};
