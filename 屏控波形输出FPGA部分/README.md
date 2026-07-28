# FPGA 双路 DDS 工程

本工程保留原 Vivado 工程、MMCM 和三种波形 ROM IP，主逻辑已收敛为：

```text
UART RX -> A5 41 1C 帧解析 -> 双路 DDS 寄存器 -> DataA/DataB
```

FPGA 不再依赖板载按键切换状态，也不向 STM32 回传状态。`sources_1/new` 只保留两个核心 Verilog 文件：

| 文件 | 用途 |
|---|---|
| `top.v` | 顶层、帧解析、双路 DDS、DAC 数据输出 |
| `uart_rx.v` | 115200 8N1 UART 接收 |

## 输出行为

| 端口 | 用途 |
|---|---|
| `DataA/ClkA/WRTA` | A 路 AD9767 输出 |
| `DataB/ClkB/WRTB` | B 路 AD9767 输出 |
| `Led_1` | A 路使能指示，低有效 |
| `Led_2` | B 路使能指示，低有效 |
| `Led_3` | 已接收有效配置且 MMCM locked，低有效 |
| `Led_uart` | 每收到 1 个 UART 字节翻转 |

未收到有效配置时两路输出 DAC 中点码 `8192`。配置帧校验失败、长度错误、协议版本错误或波形编号越界时，FPGA 忽略该帧并保持当前输出。

## STM32 下发帧

FPGA 接收固定帧：

```text
A5 41 1C DATA CHECK 5A
```

`DATA` 为 28 字节，字段定义见：

```text
../屏控波形输出说明文档/1_硬件与协议文档/通信协议_双路波形模板.md
```

方波占空比由相位累加器直接比较 `duty_q32` 实现。B 路使用 `phase_acc_b - phase_b_q32`，保持 HMI 的“正值表示 B 滞后 A”约定。正弦和三角波使用现有 ROM IP，幅值用 `amp_q13` 以中点 `8192` 为中心缩放。
