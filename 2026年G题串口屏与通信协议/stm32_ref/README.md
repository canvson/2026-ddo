# stm32_ref

这里放的是串口屏资料包里的轻量参考代码，不包含板级 HAL 初始化。

> 本目录不参与 `2026年G题STM32部分/F407_ZERO` 的 Keil 构建，也不是实际固件的代码真源。生产固件只以 `F407_ZERO/Application` 为准。

本目录保留的原因有两个：

1. 提供不依赖 STM32 HAL 的协议移植示例，便于 FPGA、上位机或其它 MCU 复用。
2. 作为 `tools/protocol_selftest.c` 的编译依赖，验证资料包中的 FEATURE/STATUS 与 HMI 帧定义。

| 文件 | 用途 |
|---|---|
| `hmi_protocol.*` | 解析和生成串口屏 `AA CMD LEN DATA CHECK 55` 帧；只保留周期和页面切换。 |
| `fpga_packet.*` | 解析和生成 FPGA 链路 `A5 5A type seq len payload crc16` 帧。 |
| `measure_model.*` | 解码 60 字节 `FEATURE` 包和可选 `STATUS` 包。 |
| `hmi_screen.*` | 发送 USART HMI/TJC `page/fill/xstr/line` 指令，覆盖动态字段和画线。 |
| `main_loop_example.c` | 被动接收 FPGA 特征数据的轮询式主循环接入示例。 |

核心逻辑：FPGA 板上按键选择算法并持续上报 `FEATURE`；STM32 不再向 FPGA 下发模式、周期或测量控制。串口屏只发送 `SET_PERIOD` 和 `SET_VIEW`，周期选择仅影响 STM32 在波形页的横轴重画范围。

若参考实现与 `F407_ZERO/Application` 出现差异，应先按生产固件行为修正文档和参考代码，不能把本目录直接加入 Keil Target。

当前生产固件的波形真实性规则和自适应绘图位于 `F407_ZERO/Application/Src/hmi_screen.c`：多分量缺有效相位时显示 `PHASE REQUIRED`，极限高次谐波使用动态点数。`stm32_ref` 与 `tools/protocol_selftest.c` 只验证帧格式和字段解码，不证明生产固件绘图策略、HMI 队列容量或真实屏幕刷新性能。
