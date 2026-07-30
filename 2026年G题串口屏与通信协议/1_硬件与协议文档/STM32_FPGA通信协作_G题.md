# STM32 与 FPGA 通信协作

## 分工

| 模块 | 职责 |
|---|---|
| FPGA | 连续采样、按板上按键选择 `UA/UB/UB_J` 算法、分离基波和谐波、抑制 `uJ` 干扰、发送 `FEATURE`。 |
| STM32 | 解析 `FEATURE`，缓存最新模式/状态/参数/分量和兼容相位字段，合成波形，绘制 0~500kHz 线谱，刷新串口屏。 |
| 串口屏 | 页面切换和 `1周期/3周期` 显示窗口选择。 |

## 数据流

```text
信号源 -> 外置 ADC -> FPGA 算法 -> FEATURE 包 -> STM32 -> 串口屏
                                     ^
                                     |
                              可选 PING/STATUS
```

屏幕切换周期只改变 STM32 的波形横轴范围，不回传 FPGA。FPGA 的算法模式只由 FPGA 板上按键决定，并通过 `FEATURE.mode` 上报。

## FEATURE 处理

STM32 收到合法 `FEATURE` 后：

1. 校验外层 CRC16/MODBUS。
2. 要求 `len=60`，`mode<=2`，`component_count=1~3`。
3. 更新 `MeasureFeature` 缓存。
4. 参数页显示 `mode/status/Vpp/Urms/f1/component[1..3]`。
5. 波形页按分量频率、峰值和相位合成稳定曲线。
6. 频谱页只画 1~3 根离散线谱，横轴固定 0~500kHz。

`phase_valid=0` 时不丢弃整包，参数页和频谱页仍可使用频率、幅值等字段；但相位字段会清零以避免误用。波形策略如下：

- 单分量：相位只移动时间原点，可按 0 度参考绘图。
- 多分量：必须 `phase_valid=1`；否则波形页清除旧曲线并显示 `PHASE REQUIRED`，不按全 0 相位伪造波形。
- 界面不显示相位数值，只在无法保证多分量波形真实性时显示上述状态提示。

## 波形合成

对第 `i` 个分量：

```text
y_i(t) = amp_peak_i * sin(2*pi*freq_i/f1*t + phase_i)
y(t) = sum(y_i(t))
```

`t` 横向覆盖 1 个或 3 个基波周期。纵向按本帧分量峰值和整体 Vpp 自动缩放，使波形稳定显示。

绘图点数不是固定 121 点，而是按最高分量频率自适应：

```text
intervals = clamp(ceil(4 * periods * max(freq_i) / f1), 120, 646)
points = intervals + 1
```

每个最高频周期至少取 4 个间隔，并受 646 像素绘图区宽度限制。题目合法极限 `f1=10kHz、fmax=500kHz、3 周期` 得到 600 个间隔、601 个点。为容纳最坏命令流，正式固件 HMI 非阻塞队列为 32768 字节。

当前 `FEATURE` 协议不携带实测波形点。若 FPGA 改传抽取点，需新增包类型、点数/缩放/时间基准定义和协议版本。

## 频谱绘制

频谱页底图只保留网格和 `0/100k/200k/300k/400k/500k` 刻度。STM32 根据每个分量：

```text
x = plot_left + freq_hz / 500000 * plot_width
height = amp_peak_mv / max_component_peak_mv * plot_height
```

只画正频率轴线谱，不画连续频谱填充，也不显示相位。

## UB_J 显示

`mode=UB_J` 表示输入信号含 `uJ` 干扰。FPGA 应上报已经抑制干扰后的 `UB` 结果：

- `flags.bit0=1` 表示干扰已抑制。
- `component[]` 不包含 1MHz 以上干扰频线。
- 参数页、波形页、频谱页都显示抑制后的 `UB` 数据。

## 超时与异常

| 条件 | 串口屏状态 |
|---|---|
| 未收到合法特征包 | `WAIT DATA` |
| 最近特征包超过 2 秒未更新 | `HOLD` |
| 链路超过 5 秒无有效包 | `COMM ERR` |
| 最近出现 CRC 错误、特征已不新鲜且无后续合法帧恢复 | `CRC ERR` |
| `FEATURE.status=3` | `OVER RANGE` |
| `FEATURE.status=4` | `ALGO ERR` |

`g_app` 统一解析一次状态，三个页面显示同一结果。STATUS 持续到达但 FEATURE 停止时保持 `HOLD`；只有连续 5 秒没有任何合法 FPGA 帧才显示 `COMM ERR`。异常只显示在当前页状态栏，不跳转页面。
