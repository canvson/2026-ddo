# 2026 G 题通信协议

## 1. 总体逻辑

FPGA 持续采样并通过板上按键选择 `UA/UB/UB_J` 算法。STM32 不下发测量模式，也不改变 FPGA 采集状态；STM32 只接收 FPGA 的最新特征数据并刷新串口屏。

串口屏只给 STM32 发页面和显示窗口选择。显示状态包括 `WAIT DATA`、`LIVE`、`HOLD`、`COMM ERR`、`CRC ERR`、`OVER RANGE`、`ALGO ERR`，均留在当前页状态栏，不跳独立异常页。

## 2. HMI -> STM32

帧格式：

```text
AA CMD LEN DATA CHECK 55
CHECK = (AA + CMD + LEN + sum(DATA)) & 0xFF
```

| CMD | 名称 | DATA | 说明 |
|---|---|---|---|
| `0x31` | `SET_PERIOD` | `period:u8` | `1` 或 `3`，只影响 STM32 波形页横轴重画。 |
| `0x32` | `SET_VIEW` | `view:u8` | `0=PARAM`，`1=WAVE`，`2=SPEC`。 |

测试帧：

| 操作 | HEX |
|---|---|
| 选择 1 周期 | `AA 31 01 01 DD 55` |
| 选择 3 周期 | `AA 31 01 03 DF 55` |
| 切到参数页 | `AA 32 01 00 DD 55` |
| 切到波形页 | `AA 32 01 01 DE 55` |
| 切到频谱页 | `AA 32 01 02 DF 55` |

## 3. FPGA <-> STM32 外层帧

帧格式：

```text
A5 5A type seq len_le payload crc16_le
```

CRC 为 `CRC16/MODBUS`，初值 `0xFFFF`，多项式反向 `0xA001`，CRC 覆盖 `A5 5A type seq len payload`。

| type | 方向 | 名称 | 说明 |
|---|---|---|---|
| `0x10` | FPGA -> STM32 | `FEATURE` | 固定 60 字节特征包，主数据通道。 |
| `0x04` | FPGA -> STM32 | `STATUS` | 可选链路/错误统计包，12 字节。 |
| `0x85` | STM32 -> FPGA | `PING` | 可选心跳，payload 为空。 |

STM32 不再向 FPGA 发送模式、显示周期或测量触发类命令。

## 4. FEATURE payload，固定 60 字节

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `frame_id` | `u32` | FPGA 递增帧号。 |
| 4 | `mode` | `u8` | `0=UA`，`1=UB`，`2=UB_J`。 |
| 5 | `status` | `u8` | `0=WAIT`，`1=VALID`，`2=HOLD`，`3=OVER_RANGE`，`4=LINK_OR_ALGO_ERROR`。 |
| 6 | `component_count` | `u8` | `1~3`。 |
| 7 | `flags` | `u8` | bit0=`interference_suppressed`，bit1=`phase_valid`。 |
| 8 | `vpp_uv` | `i32` | 整体合成波形峰峰值，单位 uV。 |
| 12 | `urms_uv` | `i32` | 真有效值，单位 uV。 |
| 16 | `f1_hz` | `u32` | 基频，单位 Hz。 |
| 20 | `reserved` | `u32` | 固定 0。 |
| 24 | `component[0]` | 12 字节 | 分量 1。 |
| 36 | `component[1]` | 12 字节 | 分量 2。 |
| 48 | `component[2]` | 12 字节 | 分量 3。 |

每个 `component`：

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `freq_hz` | `u32` | 分量频率，单位 Hz。 |
| 4 | `amp_peak_uv` | `i32` | 峰值幅度，单位 uV。 |
| 8 | `phase_deg10` | `i16` | 相对相位，0.1 度；多分量波形必须在 `phase_valid=1` 时填写。界面不显示相位数值。 |
| 10 | `reserved` | `u16` | 固定 0。 |

相位/波形约束：

- `component_count=1` 时，缺相位不改变波形形状，STM32 可用 0 度时间参考。
- `component_count>1` 时，`phase_valid=1` 是波形合成的必要条件。
- 多分量缺相位的 FEATURE 仍可更新参数和频谱；波形页显示 `PHASE REQUIRED`，不合成全 0 相位曲线。
- 当前协议没有 `WAVE` 或原始采样点包。若改传实测波形点，必须定义新 type 和协议版本。

## 5. STATUS payload，可选 12 字节

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `frame_id` | `u32` | 状态帧号或当前特征帧号。 |
| 4 | `fpga_state` | `u8` | FPGA 内部状态。 |
| 5 | `fpga_error` | `u8` | FPGA 错误码。 |
| 6 | `rx_crc_errors` | `u16` | FPGA 侧接收 CRC 计数。 |
| 8 | `tx_drops` | `u16` | FPGA 侧发送丢包计数。 |
| 10 | `reserved` | `u16` | 固定 0。 |

## 6. 新鲜度与统一状态

- 最近合法 FEATURE 不超过 2 秒：按 FEATURE 状态显示实时、超量程或算法异常。
- 超过 2 秒无新 FEATURE：三个页面统一显示 `HOLD`。
- STATUS 持续到达不会刷新 FEATURE 年龄，因此仍保持 `HOLD`，但链路保持在线。
- 超过 5 秒无任何合法 FEATURE/STATUS：三个页面统一显示 `COMM ERR`。
- 新合法 FEATURE 到达后，三个页面恢复为同一实时状态。
