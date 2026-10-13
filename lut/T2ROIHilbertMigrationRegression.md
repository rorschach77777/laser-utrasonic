# 局部 ROI Hilbert T2-only 主程序迁移与一致性审计

日期：2026-10-11。
源程序：`D:\lutproject\LSTAlgorithmTest`；主程序：`D:\lutproject\lut_test\lut`。

## 迁移内容

- 主程序T2-only厚度正式输出的包络生成由旧全帧Hilbert改为两侧128 sample保护边界的局部ROI Hilbert，保留原先t50和候选TOP-K+模板跟踪、厚度换算。
- 新增 `src/algorithm/SecondEchoRoiHilbert.h`，缓存FFT反转索引、各层旋转因子与光谱工作区，每个ThicknessProcessor持有独立实例，无全局可写缓存。
- `SecondEchoOnlyTracker.h`、`SecondEchoSearchWindow.h` 同步为测试程序相同实现。T2重捕获/软发布/30帧长失锁重启等功能同步。
- 正式T1-only路径不变；T2-only禁止给辅助T1再施加Raw-phase输出微调（测试程序同样关闭），同时禁止拿未经T1正式认证的Pair T1补造成T2 ordinal锚点。
- 使用 `ThicknessAlgorithmParameters.t2RoiHilbertEnabled=true` 作为默认；`false` 回退旧整帧计算；`t2RoiMarginSamples=128`。校正标定仍为 `secondEchoT50BiasMm=0.0280164674111623`，与测试程序绝对厚度一致。
- 主程序回归工具增加 `LST_MAIN_T2_EXPECTED_CSV` 逐帧核验器（按真实文件名核对有效、TOF、厚度），`LST_MAIN_T2_SUMMARY_ONLY` 降低控制台输出，`LST_MAIN_T2_FULL_HILBERT` 可回退。

## 主程序Release构建与功能

- 完整主程序 `appLST.exe`、`lst_data.dll` 和 `IndustrialAlgorithmFileTest.exe` 编译成功。
- 工程目前 CTest **没有配置自动测试**；使用真实数据回放代替。CMake提示设备BDAQ SDK不可用，此配置仍能编译但不等于采集卡实测。
- `ThicknessProcessor::process()` 含原T1、高精度处理及T2追踪的回放平均耗时约1.16–1.30ms，主要测试P95约1.6–1.9ms；不含CSV磁盘读写/UI刷新/采集卡链路。

## 逐帧结果对照（测试程序正式ROI Hilbert CSV为标准）

|数据|总帧|测试T2有效|主程序T2有效|有效判定不一致|双方共同有效TOF差最大|双方共同有效厚度差最大|
|---|---:|---:|---:|---:|---:|---:|
|144047 静态|1193|1192|1192|0|约5.7e-14 sample|约1.8e-15mm|
|144612 静态|1044|1043|1043|0|约5.7e-14 sample|约1.8e-15mm|
|100431 动态|987|969|969|0|约5.7e-14 sample|约1.8e-15mm|
|101940 动态|1770|1760|1758|2|约5.7e-14 sample|约1.8e-15mm|
|100010 动态|2647|2106|2100|6|约1.14e-13 sample|约1.8e-15mm|
|150545 动态|1154|1014|1014|0|约5.7e-14 sample|约1.8e-15mm|
|150655 动态|230|10|10|0|0|0|
|合计|9025|8094|8086|**8**|—|—|

主程序与测试程序**在全部共同有效的帧上厚度和TOF数值完全达到浮点舍入一致**。但 8 帧的有效/无效状态尚不同，不能声称满足严格的逐帧零差异交付标准。

### 8帧剩余差异的实测归因
- `100010`：Frame_0918、2098、2187、2378、2386、2470。
- `101940`：Frame_1497、1498。
- 这批帧和后继帧处于测试程序T1 `BACKFILLED_RELOCKED` 延迟确认区间，测试程序离线Batch先完成T1后向回填，**再**启动T2全序列Tracker；主程序实时回放先按当前帧逐次推进T2，无法事先拥有未来T1回填确认。
- 主程序旧式临时Pair T1补锚会对100010 Frame_2094、2185等没有测试T1正式认证的帧额外放行，因此该旧通道已撤销。
- 检查主程序T1回填生成源发现上述回归阶段未出现与测试离线回填对应的核心backfill事件。要修复最后8帧，必须迁移/统一T1延迟确认及后续时序状态重放流程，不能靠ROI FFT长度调整或按帧号特判。

## 当前决策

- ROI FFT正式分支已迁入主程序，性能正常，绝大部分数据与测试程序一致。
- **验收结论：暂未通过严格全帧零差异标准，因此未提交Git。**
- 后续必须完成与测试程序相同的、可审计的T1 backfill后T2 replay机制，并重新执行全9025帧结果比对，再提交Git。否则实时T2发布与Batch后处理语义不同。
- 本报告为已完成工作和当前未完成项的客观记录，不代表对回填缺口的验收。

## 复现示例

```sh
LST_MAIN_ECHO_MODE=t2 LST_MAIN_T2_SUMMARY_ONLY=1 LST_MAIN_T2_EXPECTED_CSV=D:/lutproject/LSTAlgorithmTest/build/t2roi_formal_100010.csv ./build/IndustrialAlgorithmFileTest.exe D:/lutproject/FrameData_20260923_100010 2647
```
