# 2026-10-14：厚壁 T2-only 独立单峰回退迁移至主程序与三组回归

## 本次迁移
- 测试源：`D:\lutproject\LSTAlgorithmTest\LSTAlgorithmTest\SecondEchoOnlyTracker.h`。
- 主程序：`D:\lutproject\lut_test\lut\src\algorithm\SecondEchoOnlyTracker.h`，已与测试源 `cmp` 完全相同。
- `ThicknessProcessor.cpp` 的 T2 Hilbert 包络生成链新增直接寻峰的原始包络视图（局部极大点的原始能量 SNR），在正常候选池 SNR 预筛、去重、Top-8 之前保留，仅厚壁 T2-only 使用，不污染常规候选。
- 末级兜底只在正常跟踪器未输出时执行：在预测 ±32 sample 内独立扫描实际包络局部峰，筛选 SNR 严格 >1.3 dB，使用与正常候选同样的包络能量 t50 计算 TOF。LOCK 中要求相对最后正式接受 TOF ≤16 sample；连续兜底峰相互偏差 ≤16 sample。冷重启无可信锚不准回退。
- 单峰兜底成功后仅更新临时预测 TOF（下一帧预测），不学习 NCC 包络模板，也不将未认证回退写入正式 TOF 历史。正常正式候选优先。
- 更新主程序 `tools/SecondEchoThickWallTrackerTests.cpp` 为与测试程序相同的专项边界测试（头文件引用调整）。

## 主程序三组数据实测

| 数据 | 总帧 | 有效帧 | 检出率 | 兜底输出帧数 | 最大相邻有效厚度跳动 | 相邻跳动 >0.2mm | 相邻跳动 >0.5mm |
|---|---:|---:|---:|---:|---:|---:|---:|
| `20mm` 静态 | 2851 | 2850 | 99.9649% | 7 | 0.169879mm | 0 | 0 |
| `20mm动态` | 1665 | 1623 | 97.4775% | 167 | 0.359310mm | 16 | 0 |
| `20mm动态2` | 1213 | 1171 | 96.5375% | 179 | 0.320275mm | 17 | 0 |

厚度跳动统计是按原始相邻采集帧均有效时的绝对厚度差，动态曲线总范围不应理解为静态精度。

## 与测试程序最新低阈值版本逐帧对照

参考文件：`D:\lutproject\_t2_directscan_snr1p3_{static,dynamic,dynamic2}.csv`。对应测试程序检出率：`2850/2851`、`1623/1665`、`1169/1213`。

| 数据 | 检出状态不一致帧数 | 双方有效但 TOF 不一致帧数 | 最大共同有效 TOF 差 |
|---|---:|---:|---:|
| 20mm 静态 | 0 | 1 | 0.519809 sample |
| 20mm动态 | 4 | 3 | 5.17245 sample |
| 20mm动态2 | 2 | 3 | 1.81162 sample |

**不能宣称主测试程序全帧零差异。** 跟踪器头文件已完全相同，状态差异仍来自上游独立候选形成/局部 Hilbert、T1处理时序或相应近门限浮点差别的可能性，尚需针对差异帧继续归因。主程序动态2比测试程序多检出 2 帧，但非严格匹配。

典型对照差异：静态 `Frame_2574` TOF；动态 `Frame_0478`、`Frame_0662`、`Frame_0663`、`Frame_1226` 有效状态；动态2 `Frame_0181`、`Frame_1172` 有效状态。**切勿单纯为了匹配硬编码帧号。**

## 编译、测试和运行性能
- `appLST`、`IndustrialAlgorithmFileTest`、`SecondEchoThickWallTrackerTests` 构建成功；主程序厚壁专项测试通过，测试程序两套测试也通过。
- 主程序离线回放 `ThicknessProcessor::process()` 平均时间约静态 **0.977 ms**、动态 **0.974 ms**、动态2 **0.905 ms**；不含 CSV 读取与 UI/采集硬件延迟，不代表整体 100Hz 端到端时延。
- Qt CMake 配置仍提示未找到 BDAQ SDK；本次已验证算法编译和文件回放，**没有开展采集卡实时现场测试**。
- 未提交 Git，本次改动位于现有主程序工作区，保留其他既存未提交修改。

## 复现

主程序测试（MinGW/Qt 6.5.3 DLL 已在 PATH）：

```sh
LST_MAIN_THICKNESS_STD_MM=20 LST_MAIN_ECHO_MODE=t2 LST_MAIN_T2_SUMMARY_ONLY=1 \
LST_MAIN_T2_EXPECTED_CSV=D:/lutproject/_t2_directscan_snr1p3_dynamic2.csv \
./lut_test/lut/build/IndustrialAlgorithmFileTest.exe D:/lutproject/20mm动态2 1213
```

输出 `T2_PARITY` 检测到上述有限差异并以非零退出码提示。
