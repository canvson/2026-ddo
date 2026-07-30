# 串口屏验收清单

## 底图

- [ ] 只保留 `page0/page1/page2` 三页。
- [ ] page0 没有场景选择按钮，显示“FPGA模式”和当前状态。
- [ ] page1 只保留 `1周期/3周期` 和底部导航。
- [ ] page2 频谱底图只含网格和刻度，不预画任何频线。
- [ ] page2 的“分量与状态”标题无遮挡，与 C1 数值框之间有明确间距。
- [ ] 三张底图均为 `1024x600`。

## HMI 帧

- [ ] `AA 31 01 01 DD 55` 解析为 `SET_PERIOD 1`。
- [ ] `AA 31 01 03 DF 55` 解析为 `SET_PERIOD 3`。
- [ ] `AA 32 01 00 DD 55` 解析为 `SET_VIEW PARAM`。
- [ ] `AA 32 01 01 DE 55` 解析为 `SET_VIEW WAVE`。
- [ ] `AA 32 01 02 DF 55` 解析为 `SET_VIEW SPEC`。
- [ ] 其它旧控制帧不会改变 FPGA 状态，只在当前页提示帧错误或忽略。

## STM32 刷新

- [ ] 收到合法 `FEATURE` 后，page0 模式、状态、Vpp、Urms、f1、C1~C3 能刷新。
- [ ] `component_count=1/2/3` 时，未使用分量行显示为空或 `--`。
- [ ] 单分量 `phase_valid=0` 时可用 0 度时间参考显示。
- [ ] 多分量 `phase_valid=0` 时，参数/频谱继续刷新，波形页显示 `PHASE REQUIRED` 且不画绿色曲线。
- [ ] page1 切换 `1周期/3周期` 只改变横向显示窗口。
- [ ] `f1=10kHz、f_max=500kHz、3周期` 时高次谐波形态正确，无固定 121 点混叠，刷新不超时且无 HMI 队列溢出。
- [ ] page2 只画 1~3 根正频率轴线谱，`100k/200k/300k/400k/500k` 位置对齐网格。
- [ ] page2 动态分量命令使用字体 ID2；频率、幅值分两行，字符串不重复 `C1/C2/C3`。
- [ ] ID2 为已编译进 HMI 工程的 16 像素字库；ID3 仅预留时不影响验收。

## FPGA 联调

- [ ] `UA`：基波加 0~2 个谐波，参数、波形、线谱正常。
- [ ] `UB`：分量频率可到 500kHz，线谱位置正确。
- [ ] `UB_J`：显示抑制干扰后的 `UB` 结果，1MHz 以上干扰不出现在频谱页。
- [ ] 超过 2 秒无新特征时显示 `HOLD`。
- [ ] STATUS 持续到达但 FEATURE 停止时，page0/page1/page2 都保持 `HOLD`。
- [ ] 超过 5 秒无有效 FPGA 帧时显示 `COMM ERR`。
- [ ] 最近 CRC 错误且无后续合法帧恢复时显示 `CRC ERR`；5 秒无合法帧后切换为 `COMM ERR`。
- [ ] 新 FEATURE 恢复后，page0/page1/page2 状态一致恢复为 `LIVE`。

## 工具

- [ ] `python tools/generate_g_assets.py` 只生成三页底图。
- [ ] `python tools/build_g_hmi_frames.py` 只输出 `SET_PERIOD/SET_VIEW`。
- [ ] `tools/protocol_selftest.c` 编译运行通过。
- [ ] HMI 编辑器编译 `0错误、0警告`。
- [ ] 已生成可下载屏幕产物并在真实串口屏逐页验收；只有 `.HMI/.zi` 源文件不算完成。
