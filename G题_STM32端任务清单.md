# G 题 STM32 端任务清单（与当前工程一致）

更新日期：2026-07-30  
适用工程：`2026年G题STM32部分/F407_ZERO`

## 1. 当前边界

当前系统采用“FPGA 测量、STM32 被动显示”架构：

| 模块 | 当前职责 |
|---|---|
| 模拟前端/ADC | BNC 输入、端接、保护、偏置、增益、抗混叠和高速采样。工作区没有可验收实现。 |
| FPGA | 采样、滤波、FFT、基频/幅值/相位提取、`UA/UB/UB_J` 模式选择、持续发送 `FEATURE`。工作区没有 FPGA 工程。 |
| STM32F407 | 校验和缓存 `FEATURE/STATUS`，维护链路状态，合成波形、绘制线谱、刷新串口屏。 |
| 串口屏 | 3 页被动显示；只上报页面切换和 1/3 周期选择。 |

STM32 不执行以下动作：

- 不采集题目信号，不在本机做 FFT。
- 不向 FPGA 下发 `SET_MODE/START/STOP/SET_PERIOD`。
- 不接收 `PARAM/WAVE/SPEC` 等旧协议包。
- 不接收原始或抽取波形点。

如需改成“FPGA 传实测波形点”，必须先扩展协议版本和包类型；当前版本选择“多分量强制有效相位，再由 STM32 合成波形”。

## 2. 硬件接口

| 链路/IO | 实际配置 | 状态 |
|---|---|---|
| 串口屏 | USART1，PA9 TX / PA10 RX，`115200 8N1` | 已进入固件 |
| FPGA | USART2，PA2 TX / PA3 RX，`921600 8N1` | 已进入固件 |
| FPGA RX | DMA1 Stream5，4096 字节循环缓冲 | 已进入固件 |
| HMI TX | 4096 字节中断发送环 | 已进入固件 |
| HMI 绘图队列 | 32768 字节非阻塞队列 | 已进入固件 |
| 周期实体键 | PE3，低有效，1/3 周期切换 | 已进入固件 |
| 保留键 | PE4、PE2 | 不参与当前业务 |
| 运行 LED | PF9，低有效 | 已进入固件 |

接线要求：

```text
HMI TX -> PA10
HMI RX <- PA9
FPGA TX -> PA3
FPGA RX <- PA2
所有模块 GND 共地
```

## 3. FPGA 正式协议

外层帧：

```text
A5 5A type seq len_l len_h payload... crc_l crc_h
```

- 多字节字段：little-endian。
- CRC：CRC16/MODBUS，初值 `0xFFFF`，反向多项式 `0xA001`。
- CRC 覆盖帧头、type、seq、len 和 payload，不含 CRC 字段。
- 最大 payload：128 字节。

| type | 方向 | payload |
|---:|---|---|
| `0x10 FEATURE` | FPGA -> STM32 | 固定 60 字节 |
| `0x04 STATUS` | FPGA -> STM32 | 固定 12 字节，可选 |
| `0x85 PING` | STM32 -> FPGA | 空，可选，约 500 ms 一次 |

### FEATURE（60 字节）

| 偏移 | 字段 | 类型 | 约束 |
|---:|---|---|---|
| 0 | `frame_id` | u32 | FPGA 帧号 |
| 4 | `mode` | u8 | `0=UA, 1=UB, 2=UB_J` |
| 5 | `status` | u8 | `0..4` |
| 6 | `component_count` | u8 | `1..3` |
| 7 | `flags` | u8 | bit0 干扰已抑制；bit1 相位有效 |
| 8 | `vpp_uV` | i32 | 整体合成波形峰峰值 |
| 12 | `urms_uV` | i32 | 真有效值 |
| 16 | `f1_hz` | u32 | 基频 |
| 20 | `reserved` | u32 | 0 |
| 24/36/48 | `component[0..2]` | 各 12 字节 | 频率、峰值、相位 |

分量格式：

```text
u32 freq_hz
i32 amp_peak_uV
i16 phase_deg10
u16 reserved
```

非法长度、`mode>2`、`component_count` 不在 `1..3` 的包会被拒绝。

### 相位真实性强制规则

- 单分量：相位只改变时间原点，`phase_valid=0` 时允许以 0 度参考绘图。
- 多分量：FPGA 必须置 `phase_valid=1`，并给出各分量相对相位。
- 多分量缺相位：包仍可更新参数和频谱；波形页清除旧曲线、显示 `PHASE REQUIRED`，不得按全 0 相位伪造波形。
- HMI 不显示相位数值。

## 4. 串口屏协议

上行事件：

```text
AA cmd len data... checksum 55
checksum = (AA + cmd + len + sum(data)) & 0xFF
```

| 命令 | DATA | 作用 |
|---|---|---|
| `0x31 SET_PERIOD` | `1` 或 `3` | 只改 STM32 波形显示窗口 |
| `0x32 SET_VIEW` | `0/1/2` | 参数/波形/频谱页 |

固定测试帧：

```text
AA 31 01 01 DD 55  // 1 周期
AA 31 01 03 DF 55  // 3 周期
AA 32 01 00 DD 55  // 参数页
AA 32 01 01 DE 55  // 波形页
AA 32 01 02 DF 55  // 频谱页
```

STM32 下行使用 TJC/Nextion 的 `page/fill/xstr/line` 指令，每条命令以 `FF FF FF` 结束。

## 5. 三页面任务

### page0 参数页

- 显示 FPGA 模式、统一运行状态、Vpp、Urms、f1。
- 显示 C1~C3 的分量频率和峰值幅度。
- 未使用分量显示 `--`。
- `UB_J` 且 bit0 有效时提示干扰已抑制。

### page1 波形页

合成模型：

```text
y(t) = sum(Ai * sin(2*pi*fi*t + phase_i))
```

横向显示 1 或 3 个基波周期。点数按最高分量频率自适应：

```text
intervals = clamp(ceil(4 * periods * f_max / f1), 120, 646)
points = intervals + 1
```

关键用例：

- 常规低次谐波至少 121 点。
- `f1=10kHz、f_max=500kHz、3 周期` 使用 600 个间隔、601 点。
- 多分量缺有效相位时不绘图，显示 `PHASE REQUIRED`。
- 纵轴按分量峰值和整体 Vpp 自适应，避免越界。

### page2 频谱页

- 横轴固定 `0..500kHz`。
- 只画 1~3 根正频率离散线谱。
- 纵轴按本帧最大分量峰值归一化。
- 右侧频率/幅值用字体 ID2 分两行显示，不重复底图的 C1/C2/C3。

## 6. 状态与容错

| 显示 | 条件 |
|---|---|
| `WAIT DATA` | 尚无合法 FEATURE，或状态为 WAIT |
| `LIVE` | FEATURE 年龄不超过 2 秒且有效 |
| `HOLD` | FEATURE 超过 2 秒未更新，或 FPGA 上报 HOLD |
| `COMM ERR` | 超过 5 秒没有合法 FEATURE/STATUS |
| `CRC ERR` | 最近 CRC 错误、FEATURE 已陈旧且未被合法帧恢复 |
| `OVER RANGE` | FEATURE status=3 |
| `ALGO ERR` | FEATURE status=4 |

三个页面使用同一状态解析结果。STATUS 只能保持链路在线，不能刷新 FEATURE 年龄。

## 7. 当前代码任务状态

| 任务 | 状态 | 证据/备注 |
|---|---|---|
| CRC、组包、流式重同步 | 已完成 | `fpga_packet.*` |
| FEATURE/STATUS 解码 | 已完成 | `measure_model.*` |
| seq、超时、恢复、PING | 已完成 | `fpga_link.*` |
| USART2 DMA 回卷/覆盖检测 | 已完成 | `bsp_uart.c`、`uart_ring_math.h` |
| HMI 协议只保留周期/页面 | 已完成 | `hmi_protocol.*` |
| 三页文本/频谱绘制 | 已完成 | `hmi_screen.*` |
| 高次谐波自适应波形 | 已完成 | 121~647 点 |
| 多分量有效相位门控 | 已完成 | 缺相位显示 `PHASE REQUIRED` |
| 主机回归测试 | 已完成 | 含 10000 帧混合流、最坏波形用例 |
| Keil 无警告构建 | 需每版复验 | Target `F407_ZERO` |
| 电压/频率实机校准 | 未完成 | 当前 gain=1、offset=0 |
| 校准 Flash 保存 | 未实现 | 目前只计算结构 CRC，无读写 Flash |
| HMI 编辑器编译/下载 | 未提供验收证据 | 有 `.HMI/.zi` 源文件，无已验证 `.tft` |
| FPGA/AFE 功能 | 工作区缺失 | 无法由 STM32 工程代替验收 |

## 8. 每次交付前必须执行

### 自动测试

在 `F407_ZERO` 目录：

```bash
gcc -std=c99 -Wall -Wextra -Werror -I Application/Inc \
  -o host_test/test_main.exe \
  host_test/test_main.c \
  Application/Src/fpga_packet.c \
  Application/Src/calibration.c \
  Application/Src/measure_model.c \
  Application/Src/fpga_link.c \
  Application/Src/hmi_protocol.c \
  Application/Src/hmi_screen.c \
  Application/Src/g_app.c -lm

host_test/test_main.exe
python -B host_test/fpga_serial_sim.py --self-test
```

Keil：

```text
工程：MDK-ARM/F407_ZERO.uvprojx
Target：F407_ZERO
要求：0 Error, 0 Warning
```

### 实板验收

- [ ] USART1/USART2 引脚、波特率、共地正确。
- [ ] 上电进入 page0，PF9 心跳正常。
- [ ] 五个 HMI 测试帧均正确解析。
- [ ] FEATURE 在三页刷新，状态一致。
- [ ] 2 秒 HOLD、5 秒 COMM ERR 边界实测正确。
- [ ] CRC 错帧、长度错包、DMA 覆盖可恢复。
- [ ] 单分量无相位能显示。
- [ ] 多分量有相位的波形与示波器/离线重构一致。
- [ ] 多分量无相位只显示 `PHASE REQUIRED`。
- [ ] `10kHz/500kHz/3 周期` 高次谐波实屏无混叠、无丢命令，整项在 2 秒内。
- [ ] page2 0~500kHz 线谱位置和 ID2 字体正确。
- [ ] HMI 编辑器 0 错误、0 警告，下载产物和真实屏幕均通过。
- [ ] 用标准源完成 Vpp、Urms、f1、谱峰幅值全范围校准。

## 9. STM32 端交付物

- Keil 工程和可追溯构建日志。
- `Application/Inc`、`Application/Src` 唯一生产代码。
- 主机测试结果、协议自测结果。
- FPGA 串口联调日志和最坏场景记录。
- HMI 源工程、字体、可下载产物、实屏照片/视频。
- 电压、频率、频谱幅值校准表。
- 与当前代码一致的协议、接线和验收文档。

只有“自动测试 + Keil 构建 + 实板串口 + HMI 实屏 + 测量精度”全部完成，STM32+串口屏部分才可称为验收级。
