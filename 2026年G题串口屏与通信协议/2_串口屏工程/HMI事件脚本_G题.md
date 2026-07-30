# HMI 事件脚本

串口屏只负责页面切换和 `1周期/3周期` 显示窗口选择。所有测量模式、参数、波形和频谱线均由 FPGA/STM32 数据链路决定。

## 1. 公共约定

HMI 到 STM32 的帧格式：

```text
AA CMD LEN DATA CHECK 55
CHECK = (AA + CMD + LEN + sum(DATA)) & 0xFF
```

有效命令：

| 命令 | HEX | DATA |
|---|---|---|
| `SET_PERIOD` | `0x31` | `1` 或 `3` |
| `SET_VIEW` | `0x32` | `0=PARAM`，`1=WAVE`，`2=SPEC` |

TJC/USART HMI 推荐用 `printh` 发送 HEX，例如：

```text
printh AA 32 01 01 DE 55
```

## 2. 热区清单

| 页面 | 控件名 | 坐标 | 事件 |
|---|---|---:|---|
| page0 | `b_nav_wave` | `452,536,120,44` | 切波形页 |
| page0 | `b_nav_spec` | `584,536,120,44` | 切频谱页 |
| page1 | `b_period_1` | `760,116,104,48` | 选择 1 周期 |
| page1 | `b_period_3` | `878,116,84,48` | 选择 3 周期 |
| page1 | `b_nav_param` | `320,536,120,44` | 切参数页 |
| page1 | `b_nav_spec` | `584,536,120,44` | 切频谱页 |
| page2 | `b_nav_param` | `320,536,120,44` | 切参数页 |
| page2 | `b_nav_wave` | `452,536,120,44` | 切波形页 |

所有热区建议设为透明按钮，底图负责外观。没有单独异常页。

## 3. 页面切换脚本

### page0 -> page1

```text
printh AA 32 01 01 DE 55
page page1
```

### page0 -> page2

```text
printh AA 32 01 02 DF 55
page page2
```

### page1 -> page0

```text
printh AA 32 01 00 DD 55
page page0
```

### page1 -> page2

```text
printh AA 32 01 02 DF 55
page page2
```

### page2 -> page0

```text
printh AA 32 01 00 DD 55
page page0
```

### page2 -> page1

```text
printh AA 32 01 01 DE 55
page page1
```

## 4. 周期按键脚本

### page1 `b_period_1`

```text
printh AA 31 01 01 DD 55
fill 760,116,104,48,11564
xstr 760,116,104,48,1,65535,11564,1,1,1,"1周期"
fill 878,116,84,48,8617
xstr 878,116,84,48,1,65535,8617,1,1,1,"3周期"
```

### page1 `b_period_3`

```text
printh AA 31 01 03 DF 55
fill 760,116,104,48,8617
xstr 760,116,104,48,1,65535,8617,1,1,1,"1周期"
fill 878,116,84,48,11564
xstr 878,116,84,48,1,65535,11564,1,1,1,"3周期"
```

STM32 收到周期选择后会重画波形页；FPGA 不需要知道这个选择。
