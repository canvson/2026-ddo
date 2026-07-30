# F407_ZERO - 2026 年 G 题 STM32 固件

本工程是“周期信号测量分析装置”的 STM32F407 实际固件。Keil Target 的应用代码唯一真源是 `Application/Inc` 和 `Application/Src`；串口屏资料包中的 `stm32_ref` 只是无 HAL 的移植参考和协议自测依赖，不参与本工程构建。

系统采用被动显示架构：

- FPGA 在板上选择测量模式，持续发送 60 字节 `FEATURE`，可选发送 `STATUS`。
- STM32 接收并校验 FPGA 帧，维护数据新鲜度，合成波形、绘制频谱并刷新串口屏。
- 串口屏只允许切换显示页面和 1/3 周期显示窗口，不控制 FPGA 测量流程。
- STM32 可每 500 ms 向 FPGA 发送一次可选 `PING`，不发送模式、开始或停止命令。

## 1. 硬件接口

```text
TJC/Nextion 屏 <-- USART1 PA9/PA10 115200 8N1 --> STM32F407
FPGA          <-- USART2 PA2/PA3  921600 8N1 --> STM32F407
                                      |
                                      +-- DMA1_Stream5: USART2_RX circular
                                      +-- PE3: 1/3 周期显示切换
                                      +-- PF9: heartbeat LED, active low
```

USART2 默认 921600 bps。若改为 2 Mbps，需同时修改 `board_pins.h` 中的 `FPGA_UART_BAUDRATE` 和 FPGA 串口参数。

## 2. FPGA 链路

所有多字节字段均为 little-endian。

```text
A5 5A type seq len_l len_h payload... crc_l crc_h
```

CRC 使用 CRC16/Modbus：初值 `0xFFFF`，多项式 `0xA001`，覆盖帧头至 payload，不包含 CRC 字段。

| type | 方向 | 名称 | payload |
|---:|---|---|---|
| `0x10` | FPGA -> STM32 | FEATURE | 固定 60 字节 |
| `0x04` | FPGA -> STM32 | STATUS | 固定 12 字节，可选 |
| `0x85` | STM32 -> FPGA | PING | 空，可选 |

### FEATURE payload（60 字节）

```text
offset  type       field
0       u32        frame_id
4       u8         mode               // 0=UA, 1=UB, 2=UB_J
5       u8         status             // 0=WAIT, 1=VALID, 2=HOLD,
                                      // 3=OVER_RANGE, 4=ALGO_ERROR
6       u8         component_count    // 1..3
7       u8         flags              // bit0=干扰已抑制，bit1=相位有效
8       i32        vpp_uV
12      i32        urms_uV
16      u32        f1_hz
20      u32        reserved
24      component[0]
36      component[1]
48      component[2]

component:
0       u32        freq_hz
4       i32        amp_peak_uV
8       i16        phase_deg10
10      u16        reserved
```

题目界面不显示相位数值，但多分量波形合成必须有相对相位：

- `component_count=1` 时，相位只影响时间原点，可用 0 度参考绘图。
- `component_count>1` 时，FPGA 必须置 `flags.bit1=1` 并填写各分量 `phase_deg10`。
- 多分量包缺少有效相位时，参数页和频谱页仍使用该包；波形页不伪造 0 相位曲线，而是清空曲线并显示 `PHASE REQUIRED`。

当前正式协议只传 `FEATURE/STATUS`，不传原始或抽取波形点。若以后改为实测波形点，必须新增明确的包类型、点格式和协议版本，不能复用当前 60 字节 `FEATURE` 的含义。

### STATUS payload（12 字节）

```text
u32 frame_id
u8  fpga_state
u8  fpga_error
u16 rx_crc_errors
u16 tx_drops
u16 reserved
```

`STATUS` 只证明 FPGA 链路仍有合法帧，不会替代 `FEATURE`，也不会把陈旧测量值标为实时。

## 3. 串口屏协议

屏幕上行事件格式：

```text
AA cmd len data... checksum 55
checksum = (AA + cmd + len + sum(data)) & 0xFF
```

只接受以下两个命令：

| cmd | 名称 | payload |
|---:|---|---|
| `0x31` | SET_PERIOD | `u8 period_mode`：`1` 或 `3` |
| `0x32` | SET_VIEW | `u8 view`：`0=参数页`，`1=波形页`，`2=频谱页` |

STM32 下行使用 TJC/Nextion 原生命令 `page/fill/xstr/line`。不存在 `SET_SCENE`、`START`、`STOP` 或主动测量流程。

## 4. 统一运行状态

`g_app` 只解析一次运行状态，再将同一枚举传给 page0、page1、page2，三个页面不会各自覆盖状态。

| 枚举 | 显示 | 条件 |
|---|---|---|
| `WAIT` | WAIT DATA | 尚无合法 FEATURE，或 FEATURE 状态为 WAIT |
| `LIVE` | LIVE | FEATURE 不超过 2 秒且状态有效 |
| `HOLD` | HOLD | 超过 2 秒无新 FEATURE，或 FEATURE 状态为 HOLD |
| `COMM_ERR` | COMM ERR | 超过 5 秒无任何合法 FPGA 帧 |
| `CRC_ERR` | CRC ERR | 最近出现坏 CRC、FEATURE 已不新鲜，且尚无后续合法帧恢复 |
| `OVER_RANGE` | OVER RANGE | FEATURE 状态为 OVER_RANGE |
| `ALGO_ERR` | ALGO ERR | FEATURE 状态为 ALGO_ERROR |

边界采用严格“超过”：

- FEATURE 年龄 `1999 ms` 仍为 `LIVE`，`2001 ms` 为 `HOLD`。
- 最后合法 FPGA 帧年龄 `4999 ms` 不判断链，`5001 ms` 为 `COMM ERR`。
- STATUS 持续到达但 FEATURE 停止时保持 `HOLD`；STATUS 同时会清除已恢复的 CRC 错误状态。
- 收到新合法 FEATURE 后，三个页面都恢复为 FEATURE 对应状态。

## 5. 页面与字体

- page0：显示 FPGA 模式、统一状态、Vpp、Urms、f1 和 C1~C3。
- page1：用 1~3 个分量合成 1/3 周期波形；多分量要求有效相位。绘图间隔数按最高分量频率自适应：

  ```text
  intervals = clamp(ceil(4 * periods * f_max / f1), 120, 646)
  points = intervals + 1
  ```

  合法极限 `f1=10kHz、f_max=500kHz、3 周期` 使用 600 个间隔、601 个点，避免固定 121 点对高次谐波产生显示混叠。周期选择只改变横轴显示窗口。
- page2：绘制 0~500 kHz 频谱线，右侧 C1/C2/C3 的动态值分别显示为“频率一行、幅值一行”。

字体约定：

| ID | 用途 |
|---:|---|
| `1` | 普通动态字段和状态 |
| `2` | page2 分量频率/幅值，要求 16 像素字库 |
| `3` | 预留 8 像素字库，本版评测主数据不使用 |

字模必须先加入 HMI 工程并编译进屏幕资源。STM32 只能在 `xstr` 中选择字体 ID、坐标、颜色和文字内容，不能通过串口临时创建或缩放字库。在 HMI 编辑器加入 ID2 并完成实屏检查前，只能确认 STM32 已正确发送 ID2 命令，不能宣称小字体已经完成实屏验收。

## 6. 模块结构

```text
Application/Inc, Application/Src
  fpga_packet.*    CRC、组包、有界流式解析和坏帧重同步
  measure_model.*  FEATURE/STATUS 解码及测量模型
  fpga_link.*      USART2 分发、seq 统计、2 秒/5 秒新鲜度和恢复
  calibration.*    默认增益/频偏和结构 CRC；当前没有 Flash 读写
  hmi_protocol.*   屏幕 AA..55 事件解析，仅 SET_PERIOD/SET_VIEW
  hmi_screen.*     三页面刷新、自适应波形/频谱绘制和 32768 字节非阻塞输出队列
  g_app.*          裸机调度、统一运行状态、HMI 溢出恢复和心跳
  bsp_uart.*       USART1 非阻塞 TX/RX、USART2 DMA 接收
  uart_ring_math.h DMA 保留窗口和覆盖判定的纯计算逻辑
  bsp_board.*      PE3/PF9 板级 IO
```

`Core/Src/main.c` 只初始化 GPIO、DMA、USART1 和 USART2。旧题的本地 ADC/DAC/RLC/等效输出业务不参与本 Target。

## 7. 验证

在 `F407_ZERO` 目录运行主机测试：

```bash
gcc -std=c99 -Wall -Wextra -I Application/Inc -o host_test/test_main.exe \
  host_test/test_main.c \
  Application/Src/fpga_packet.c \
  Application/Src/calibration.c \
  Application/Src/measure_model.c \
  Application/Src/fpga_link.c \
  Application/Src/hmi_protocol.c \
  Application/Src/hmi_screen.c \
  Application/Src/g_app.c -lm

./host_test/test_main.exe
```

测试覆盖协议解析、CRC/错位恢复、STATUS-only、统一页面状态、1999/2001/4999/5001 ms 边界、FEATURE 恢复、page2 字体 ID2 双行输出、HMI 队列与 DMA 回卷等；还明确覆盖：

- 多分量缺相位时只显示 `PHASE REQUIRED`，不输出绿色波形线。
- `10kHz` 基波、`500kHz` 最高分量、3 周期时输出 600 个自适应线段。
- 最坏波形命令流不溢出 32768 字节队列，且不超过 115200 8N1 的 2 秒理论字节预算。

FPGA 串口仿真器离线自测：

```text
python -B host_test/fpga_serial_sim.py --self-test
```

实板联调时显式指定连接 STM32 USART2 的串口：

```text
python -B host_test/fpga_serial_sim.py --port COM7 --baudrate 921600
```

Keil 工程：

```text
MDK-ARM/F407_ZERO.uvprojx
Target: F407_ZERO
Keil: D:\usual software\keil5\UV4\UV4.exe
```

验收要求为 Build `0 Error, 0 Warning`。

## 8. 上板检查

1. 串口屏连接 USART1，确认上电进入 page0，PF9 心跳闪烁。
2. FPGA 连接 USART2，连续发送合法 FEATURE，确认三个页面状态一致为 `LIVE`。
3. 停止 FEATURE 但继续发送 STATUS，确认 2 秒后三个页面均为 `HOLD`，不会进入 `COMM ERR`。
4. 停止所有合法 FPGA 帧，确认最后一帧 5 秒后进入 `COMM ERR`。
5. 恢复 FEATURE，确认三页均恢复实时数据。
6. 检查 page2 右侧静态 C1/C2/C3 不被动态文字重复，频率和幅值分两行显示。
7. 在 HMI 编辑器加入 ID2 16 像素字库后编译并实屏确认；ID3 可留待后续。
8. 发送 `component_count>1、phase_valid=0` 的 FEATURE，确认参数/频谱可更新，但 page1 只显示 `PHASE REQUIRED` 且没有伪造曲线。
9. 用 `f1=10kHz、f_max=500kHz` 的 3 周期用例确认高次谐波形态、刷新时间和实屏串口稳定性。
