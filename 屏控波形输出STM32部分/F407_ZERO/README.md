# F407_ZERO STM32 主控工程

本工程用于电赛预备的双路波形控制链路：

```text
串口屏 --USART1/DMA--> STM32F407 --USART2/TX--> FPGA --AD9767--> DataA/DataB
```

STM32 只做控制与通信中枢。串口屏发送 `AA 21 16` 双路配置帧后，STM32 负责校验参数，并把频率、幅值、占空比和相位换算为 FPGA 可直接使用的 DDS 参数，再下发 `A5 41 1C` 固定帧。旧 G 题的已知模型、学习建模、ADC/DAC 辅助输出和状态回传流程已经从主流程移除。

## 硬件资源

| 功能 | 外设/引脚 | 说明 |
|---|---|---|
| 串口屏 | USART1，PA9 TX / PA10 RX | 115200 8N1，RX 使用 `HAL_UARTEx_ReceiveToIdle_DMA` |
| FPGA 下行 | USART2，PA2 TX / PA3 RX | 115200 8N1，仅用 TX |
| 按键 | PE4 启动 / PE2 停止 | 启动重发最后一次有效配置，停止下发双路关闭帧 |
| LED | PF9 | 低有效心跳，收到有效输出配置后进入快速闪烁 |

## 应用层文件

| 文件 | 用途 |
|---|---|
| `Application/Src/hmi_protocol.c` | 解析并校验 HMI `AA 21 16 DATA CHECK 55` |
| `Application/Src/fpga_ctrl.c` | 构造 FPGA `A5 41 1C DATA CHECK 5A`，完成 DDS 参数换算 |
| `Application/Src/g_app.c` | 主循环调度、按键处理、HMI 字节环形缓冲、FPGA 下发 |
| `Application/Src/bsp_uart.c` | USART1 DMA 接收与 USART2 阻塞发送 |
| `Application/Src/bsp_board.c` | 板级按键和 LED 适配 |

Keil 工程源文件列表已经收敛到上述主流程文件；旧 `known_model`、`learn_engine`、`emulate_engine`、`adc/dac/tim` 等模块不再参与构建。

## 协议要点

HMI 输入帧：

```text
AA 21 16 DATA CHECK 55
```

`DATA` 固定 22 字节，包含 A/B 两路的波形、频率 Hz、幅值 mVpp、占空比 0.1% 单位，以及 B 相对 A 的相位角。STM32 严格检查帧头、帧尾、长度、校验和和参数范围，任意错误都不会更新 FPGA 输出。

FPGA 输出帧：

```text
A5 41 1C DATA CHECK 5A
```

`DATA` 固定 28 字节，包含 `proto_ver`、`flags`、两路 `wave/fword/amp_q13/duty_q32` 和 `phase_b_q32`。幅值按 `5000mVpp -> 8192` 线性映射；DDS 时钟按 125MHz 计算频率字。

详细字段见：

```text
../../屏控波形输出说明文档/1_硬件与协议文档/通信协议_双路波形模板.md
```

## 编译

Keil MDK 打开：

```text
MDK-ARM/F407_ZERO.uvprojx
```

目标为 `F407_ZERO`，正常目标是 0 Error。若使用 CubeMX 重新生成代码，需要手工复核 USART1 RX DMA2 Stream2 Channel4、USART1 全局中断和 `HAL_UARTEx_RxEventCallback` 是否保留。

## PC 端测试

`Application` 层可以用 host test 检查协议和打包逻辑：

```bash
gcc -std=c99 -O2 -I Application/Inc -o host_test/test_main.exe host_test/test_main.c Application/Src/hmi_protocol.c Application/Src/fpga_ctrl.c Application/Src/g_app.c
host_test/test_main.exe
```
