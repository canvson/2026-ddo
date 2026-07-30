# 需求覆盖核对

## 总体覆盖

| 题目/问答要求 | 当前实现 |
|---|---|
| 周期信号参数显示 | `FEATURE.vpp_uv/urms_uv/f1_hz` 在 page0/page1 显示。 |
| 1 个或 3 个完整周期波形 | 串口屏发送 `SET_PERIOD`，STM32 用特征分量合成 1/3 周期波形。 |
| 频谱显示 | page2 固定 0~500kHz 正频率轴，STM32 绘制离散线谱。 |
| 幅值含义 | 分量幅值按峰值 mV 显示。 |
| Vpp 含义 | Vpp 显示整体合成波形峰峰值。 |
| `uJ` 抗干扰 | FPGA 在 `UB_J` 模式上报已抑制干扰后的 `UB` 特征，STM32 不显示 1MHz 以上干扰线。 |
| 虚拟触摸按键 | 保留页面切换和周期选择虚拟键。 |
| 异常处理 | 当前页状态栏短文本，不做独立异常页。 |

## UA

- FPGA 上报 `mode=0`。
- `component_count=1~3`。
- page0 显示参数和分量峰值。
- page1 用分量和相位合成波形；多分量时强制 `phase_valid=1`。
- page2 显示基波和谐波线谱。

## UB

- FPGA 上报 `mode=1`。
- 分量频率不超过 500kHz。
- page2 线谱横坐标按 `freq_hz/500000` 映射。

## UB_J

- FPGA 上报 `mode=2`。
- `flags.bit0=1` 表示干扰已抑制。
- `component[]` 为抑制后的 `UB` 分量。
- 屏幕不显示 `uJ` 干扰频线。

## 数据接口

- FPGA 主数据包为 `FEATURE=0x10`，固定 60 字节。
- STM32 可接收 `STATUS=0x04` 作为链路统计。
- STM32 上行只保留可选 `PING=0x85`。
- 串口屏上行只保留 `SET_PERIOD=0x31` 和 `SET_VIEW=0x32`。

## 自检

- [ ] HMI 5 个测试帧 checksum 正确。
- [ ] 合法 `FEATURE` 通过。
- [ ] CRC 错误被拒。
- [ ] `len!=60` 被拒。
- [ ] `mode>2` 被拒。
- [ ] `component_count=0/>3` 被拒。
- [ ] `component_count>1 && phase_valid=0` 时，参数/频谱仍可用，波形页显示 `PHASE REQUIRED` 且不画曲线。
- [ ] `f1=10kHz、f_max=500kHz、3 周期` 时使用 601 点，确认高次谐波无固定 121 点混叠。
- [ ] 当前链路只使用 `FEATURE/STATUS/PING`，没有未实现的原始波形点包。
