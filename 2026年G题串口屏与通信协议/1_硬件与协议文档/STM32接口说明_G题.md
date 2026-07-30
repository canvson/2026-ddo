# STM32 接口说明

## 串口

| 链路 | 默认参数 | 说明 |
|---|---|---|
| STM32 <-> 串口屏 | `115200 8N1` | 屏幕只发送周期和页面切换；STM32 回写文本、波形线、频谱线。 |
| FPGA -> STM32 | `921600 8N1` | FPGA 持续发送 `FEATURE`，可选发送 `STATUS`。 |
| STM32 -> FPGA | `921600 8N1` | 仅保留可选 `PING`，用于链路可见性。 |

## 实际工程文件

`2026年G题STM32部分/F407_ZERO/Application` 中当前主路径：

| 文件 | 作用 |
|---|---|
| `hmi_protocol.*` | 解析 HMI `SET_PERIOD/SET_VIEW`。 |
| `fpga_packet.*` | 解析 `A5 5A ... crc16` 外层帧。 |
| `measure_model.*` | 缓存最新 `MeasureFeature` 和可选 FPGA 状态。 |
| `fpga_link.*` | 分发 `FEATURE/STATUS`，统计 CRC、长度、序号和超时。 |
| `hmi_screen.*` | 生成 USART HMI/TJC 指令，合成波形、绘制线谱；page2 分量值用字体 ID2 分两行显示。 |
| `g_app.*` | 顶层轮询：HMI RX、FPGA RX、统一运行状态、页面刷新、心跳 LED。 |
| `bsp_board.*` | 只上报本地 `1/3周期` 实体键，PE4/PE2 保留。 |

## 主循环行为

```text
GApp_Poll()
  1. 发送串口屏待发队列
  2. 解析 HMI 字节流
  3. 读取 FPGA 串口 DMA/环形缓冲
  4. FpgaLink 分发 FEATURE/STATUS
  5. 更新当前页状态栏
  6. 按最新 FEATURE 刷新当前页
```

HMI 的 `SET_PERIOD` 只改 `s_period_mode`，HMI 的 `SET_VIEW` 只改 `s_page`。这两个事件都不会转发到 FPGA。

## 绘图策略

| 页面 | 数据来源 | 绘制方式 |
|---|---|---|
| 参数页 | `MeasureFeature` | 文本覆盖模式、状态、Vpp、Urms、f1、分量峰值。 |
| 波形页 | `MeasureFeature.component[]` | 使用频率、峰值和有效相位合成曲线；按最高频率自适应 121~647 点，周期选择只改横轴跨度。 |
| 频谱页 | `MeasureFeature.component[]` | 在 0~500kHz 正频率轴上画 1~3 根线谱；右侧频率/幅值各占一行，不重复静态 C1/C2/C3。 |

## 降级规则

- `phase_valid=0`：仍接收该包并供参数/频谱使用；单分量允许 0 度时间参考，多分量波形禁止合成并显示 `PHASE REQUIRED`。
- `component_count` 不在 `1~3`：拒绝该包，保留上次有效数据。
- `mode>2` 或 `len!=60`：拒绝该包并计入链路异常统计。
- 超过 2 秒无新特征：显示 `HOLD`。
- 超过 5 秒无有效 FPGA 帧：显示 `COMM ERR`。
- STATUS 持续更新但 FEATURE 停止：链路仍在线，保持 `HOLD`。

正式固件不解析原始或抽取波形点包；FPGA 必须按当前 60 字节 `FEATURE` 接口提供多分量相位。
