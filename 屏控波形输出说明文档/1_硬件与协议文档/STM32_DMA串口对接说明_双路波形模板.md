# STM32 DMA 串口对接说明

本次不修改 STM32 源码，但串口屏模板已经按 STM32 DMA 接收方式设计。STM32 必须把 USART1 配成 DMA 接收，不能依赖阻塞式接收。

## 串口配置

| 项目 | 值 |
|---|---|
| USART | USART1 |
| TX/RX | PA9 TX / PA10 RX |
| 波特率 | 115200 |
| 格式 | 8N1 |
| 接收 | DMA |
| 推荐方式 | `HAL_UARTEx_ReceiveToIdle_DMA` |

如果当前工程使用循环 DMA + IDLE 中断，也可以；核心要求是 DMA 缓冲区里收到的每个字节都要按顺序喂给 `AA CMD LEN DATA CHECK 55` 解析状态机。

## 推荐接收流程

1. 定义 DMA RX 缓冲区，例如 `uint8_t hmi_rx_dma[64]`。
2. 初始化 USART1 后调用 `HAL_UARTEx_ReceiveToIdle_DMA(&huart1, hmi_rx_dma, sizeof(hmi_rx_dma))`。
3. 在 `HAL_UARTEx_RxEventCallback` 中读取本次新增字节。
4. 对每个字节调用 HMI 协议解析器。
5. 解析出 `OUTPUT_CONFIG` 后，先校验完整参数，再更新两路波形配置。
6. 回调末尾重新启动 `HAL_UARTEx_ReceiveToIdle_DMA`。

## 数据结构建议

```c
typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE = 1,
    WAVE_TRIANGLE = 2,
} WaveType;

typedef struct {
    WaveType wave;
    uint32_t freq_hz;
    uint16_t amp_mVpp;
    uint16_t duty_pct10;
} WaveChannelConfig;

typedef struct {
    uint8_t proto_ver;
    uint8_t flags;
    WaveChannelConfig ch_a;
    WaveChannelConfig ch_b;
    int16_t phase_b_rel_a_deg;
} DualWaveOutputConfig;
```

## 频率显示规则

STM32 回写屏幕时负责把整数 Hz 格式化成用户可读字符串。显示单位使用题目要求的写法：`Hz`、`KHz`、`MHz`。

示例：

| 输入 Hz | 显示 |
|---:|---|
| 500 | `500Hz` |
| 1000 | `1KHz` |
| 1500 | `1.5KHz` |
| 500600 | `500.6KHz` |
| 1000000 | `1MHz` |
| 1100000 | `1.1MHz` |
| 1001000 | `1.001MHz` |

当前协议选择整数 Hz，所以 `1.0001KHz` 等于 `1000.1Hz`，不能被本版 `uint32 Hz` 精确表达。后续若必须支持 0.1Hz 或更细精度，需要把协议字段改成定点单位，例如 `0.1Hz` 或 `mHz`。

## 参考格式化逻辑

```c
static void format_freq(uint32_t hz, char *out, size_t cap)
{
    if (hz < 1000u) {
        snprintf(out, cap, "%luHz", (unsigned long)hz);
    } else if (hz < 1000000u) {
        uint32_t whole = hz / 1000u;
        uint32_t frac = hz % 1000u;
        if (frac == 0u) {
            snprintf(out, cap, "%luKHz", (unsigned long)whole);
        } else if ((frac % 100u) == 0u) {
            snprintf(out, cap, "%lu.%luKHz", (unsigned long)whole, (unsigned long)(frac / 100u));
        } else if ((frac % 10u) == 0u) {
            snprintf(out, cap, "%lu.%02luKHz", (unsigned long)whole, (unsigned long)(frac / 10u));
        } else {
            snprintf(out, cap, "%lu.%03luKHz", (unsigned long)whole, (unsigned long)frac);
        }
    } else {
        uint32_t whole = hz / 1000000u;
        uint32_t frac = hz % 1000000u;
        if (frac == 0u) {
            snprintf(out, cap, "%luMHz", (unsigned long)whole);
        } else if ((frac % 100000u) == 0u) {
            snprintf(out, cap, "%lu.%luMHz", (unsigned long)whole, (unsigned long)(frac / 100000u));
        } else if ((frac % 10000u) == 0u) {
            snprintf(out, cap, "%lu.%02luMHz", (unsigned long)whole, (unsigned long)(frac / 10000u));
        } else if ((frac % 1000u) == 0u) {
            snprintf(out, cap, "%lu.%03luMHz", (unsigned long)whole, (unsigned long)(frac / 1000u));
        } else {
            snprintf(out, cap, "%lu.%06luMHz", (unsigned long)whole, (unsigned long)frac);
        }
    }
}
```

## 回屏建议

STM32 发给屏幕的命令仍是 USART HMI 原生命令，每条指令后追加 `FF FF FF`：

```text
n_a_freq.val=1000 FF FF FF
n_b_freq.val=1000 FF FF FF
xstr 452,565,150,30,1,65535,6471,1,1,1,"B相对A:0deg" FF FF FF
```

输出后建议立即回写一次 A/B 两路最终值，保证屏幕显示的是 STM32 已接收并采用的配置。
