#pragma once

#include "PacketSpline.h"

// 工业算法侧的波包拟合接口。
// 不包含测试程序中的QML显示、拟合曲线导出和失败原因统计。
// 复用PacketSpline中的等效单高斯拟合能力，仅提供实时测厚所需结果。
namespace IndustrialPacketPeakFit {

using FitOptions = PacketPeakFit::FitOptions;
using Fit = PacketPeakFit::Fit;
using FitWorkspace = PacketPeakFit::FitWorkspace;

inline Fit fit(const std::vector<double>& signal,
               int left,
               int right,
               const char* mode,
               const FitOptions& options = {},
               FitWorkspace* workspace = nullptr)
{
    // 底层PacketPeakFit接口使用引用传递workspace。
    // 工业接口保持指针形式，兼容可选workspace管理。
    if (workspace) {
        return PacketPeakFit::fit(signal,
                                  left,
                                  right,
                                  mode,
                                  options,
                                  *workspace);
    }

    FitWorkspace localWorkspace;
    return PacketPeakFit::fit(signal,
                              left,
                              right,
                              mode,
                              options,
                              localWorkspace);
}

}
