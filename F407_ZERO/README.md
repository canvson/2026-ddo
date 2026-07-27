# F407_ZERO — G 题「电路模型探究装置」STM32 主控工程

2025 年全国大学生电子设计竞赛 G 题（本科组）的探究装置主控固件。
STM32F407ZGT6 负责：串口屏交互、已知模型反算、FPGA DDS 码表下发、
未知 RLC 电路自主学习建模（发挥 1）、实时等效输出（发挥 2）。

配套 FPGA 工程（`25年G题FPGA部分`，Zynq-7010 + AD9767 DDS）**保持原样，
未做任何修改**；本工程内置了从其 Verilog 源码自动提取并核对过的全部
控制码表。

```text
串口屏(TJC 1024x600) <--USART1--> STM32F407 --USART2--> FPGA DDS(AD9767)
                                     |
                                     +-- PA1 ADC1  J_MEAS  主测量口
                                     +-- PB0 ADC2  J_REF   激励参考口
                                     +-- PA4 DAC1  J_OUT   波形输出口
                                     +-- PE3/PE4/PE2 学习/启动/停止键, PF9 LED
```

## 1. 功能与题目条目对应

| 题目条目 | 固件实现 |
|---|---|
| 基本(2) 信号源 | 波形设定页 `CALIB_OUTPUT`：FPGA Basic_two 码（100Hz~3kHz 步 100Hz、1MHz、2MHz，约 3.5Vpp）+ J_OUT 辅助输出（≤200kHz，幅度 0~5000mVpp 可设） |
| 基本(3)(4) 已知模型控制 | `START_BASIC`：按 H(s)=5/(1e-8s²+3e-4s+1) 反算 Vin，下发 FPGA Basic_four 双字节码，并在 J_OUT 同步输出同频同幅精密正弦（开环，无输出端反馈，符合题目说明） |
| 发挥(1) 学习建模 | 学习键触发：驱动 FPGA Develop_one 扫频 1k~50kHz（491 点），双 ADC 同步测幅比，四型最小二乘拟合分类（低通/高通/带通/带阻），屏显类型 + f0/Q + R/L/C 估计；实测 10~15s，预算上限 110s |
| 发挥(2) 等效输出 | `START_EMULATE`：测输入频率（精度~1e-5，吸附 200Hz 栅格）/波形/占空比 → 周期折叠 → 谐波(≤64 次)×H(jkω) → 软件 DDS 从 J_OUT 输出；启动 <1s（题目限 5s），每 2s 重锁定，双踪无漂移 |
| 说明(3)(4) 一键操作 | 屏幕设置+启动按钮；PE4 硬件一键启动、PE3 唯一学习键、PE2 停止；启动后全自动 |

## 2. 硬件资源

主频 84MHz（HSE 8MHz，PLL 8/4×84/2），APB1 定时器 84MHz。

| 功能 | 外设/引脚 | 说明 |
|---|---|---|
| 串口屏 | USART1，PA9(TX)/PA10(RX) | 115200 8N1；RX 中断只入环形缓冲，帧解析在主循环 |
| FPGA 下行 | USART2，PA2(TX)/PA3(RX) | 115200 8N1，仅用 TX（FPGA 无上行） |
| 主测量 J_MEAS | ADC1_IN1，PA1 | 学习=未知电路输出；等效=信号发生器输入 |
| 激励参考 J_REF | ADC2_IN8，PB0 | 学习=未知电路输入（FPGA 驱动抽头） |
| 采样触发 | TIM2 TRGO | 双 ADC 规则同步(模式2)，DMA2 Stream0，fs 2k~700kS/s 运行时可调 |
| 波形输出 J_OUT | DAC1_OUT1，PA4 | TIM6 TRGO 1.05MS/s，DMA1 Stream5 循环双缓冲 |
| 按键 | PE3 学习 / PE4 启动 / PE2 停止 | 上拉输入低有效，30ms 去抖 |
| LED | PF9 | 心跳 250ms 翻转（低有效），停闪=死机 |

中断优先级：DAC DMA(1) > ADC DMA(2) > USART(3)，保证等效输出永不断流。
静态 RAM 占用约 60KB（采集缓冲 32KB、DDS/合成表约 20KB）。

模拟前端（需外接小板，详见资料包《STM32接口说明_G题.md》）：
输入 470k/470k 偏置 + 运放跟随（输入阻抗 ≈235kΩ ≥ 100kΩ 要求）；
输出隔直 ×2 运放级（满幅约 6.2Vpp）。

## 3. 目录结构

```text
F407_ZERO/
├── Core/
│   ├── Inc/ main.h gpio.h usart.h adc.h dac.h dma.h tim.h stm32f4xx_*.h
│   └── Src/
│       ├── main.c              初始化顺序 + 主循环(GApp_Poll)
│       ├── adc.c               双 ADC 同步采集服务(Cap_* 接口实现)
│       ├── dac.c               DAC 循环流 + 半满/全满中断续填
│       ├── tim.c dma.c gpio.c usart.c
│       └── stm32f4xx_it.c / _hal_msp.c
├── Application/
│   ├── Inc/  board_pins.h(引脚与校准常数, 改接线只动这里)
│   │         signal_capture.h(采集服务契约) 及各模块头文件
│   └── Src/
│       ├── g_app.c             业务编排:模式/按键/错误码/UI 节流
│       ├── hmi_protocol.c      AA..55 帧解析(命令 0x30~0x36)
│       ├── hmi_screen.c        串口屏刷新(fill+xstr, GB2312)
│       ├── known_model.c       H(jw) 反算 Vin
│       ├── fpga_ctrl.c         FPGA 码表下发(清洗字节/1700Hz 兜底)
│       ├── fpga_ctrl_table.c   Basic_four 330 条码表(自动生成,勿手改)
│       ├── signal_meas.c       测频/Vpp/占空比/波形识别/谐波分解
│       ├── rlc_model.c         四型拟合分类 + f0/Q/RLC + 复响应
│       ├── learn_engine.c      发挥(1) 扫频状态机
│       ├── emulate_engine.c    发挥(2) 合成状态机
│       ├── wave_out.c          软件 DDS(4096 点表 + 32bit 相位累加)
│       └── bsp_uart.c bsp_board.c
├── MDK-ARM/F407_ZERO.uvprojx   Keil 工程(全部源文件已加入)
└── F407_ZERO.ioc               CubeMX 配置(与代码同步维护)
```

除 `Core/Src/adc.c、dac.c` 通过 `signal_capture.h / WaveOut_FillBlock`
挂接硬件外，Application 层全部为可移植 C99，可在 PC 上编译测试。

## 4. 编译与下载

1. Keil MDK 打开 `MDK-ARM/F407_ZERO.uvprojx`，直接 Build（目标 0 Error）。
2. ST-Link 接 SWD（PA13/PA14），下载运行。
3. 上电后：串口屏回到首页，PF9 心跳闪烁，J_OUT 输出直流中点电平。

如需用 CubeMX 重新生成（版本 6.14）：用户代码均在 `USER CODE` 区内，
重生成后请核对 ADC 双通道同步(模式2)与 DMA/NVIC 配置是否保留。

## 5. 操作流程（与 FPGA 按键状态配合）

FPGA 状态由其板上按键循环切换（LED 指示），STM32 只发字节：

| 测试项 | FPGA 状态 | 操作 |
|---|---|---|
| 基本(2) 信号源 | 0（LED 全灭） | 波形设定页设频率/幅度 → 输出；≤3kHz 与 1M/2MHz 看 FPGA 输出，其余看 J_OUT |
| 基本(3)(4) | 2（LED2 亮） | 基本页设频率与目标 Vpp → 启动（或 PE4 一键重启上次设置）；1700Hz 且目标 >1.0Vpp 时屏幕会提示改用 J_OUT |
| 发挥(1) 学习 | 先按 FPGA 复位，再切到 3（LED3 亮） | J_REF 接未知电路输入、J_MEAS 接输出 → 按 PE3 学习键 → 等进度 100% 读类型/参数 |
| 发挥(2) 等效 | 任意（不用 FPGA） | 拆学习线；发生器→未知电路输入并联 J_MEAS → 等效页选波形 → 启动 → 示波器双踪对比 J_OUT 与未知电路输出 |

停止：PE2 或屏幕停止（只停 STM32 输出；FPGA 无停止码，需要静音按其复位）。

## 6. 错误码

| 码 | 含义 | 处置 |
|---:|---|---|
| 0x1001 | 未知 HMI 命令 | 清错返回首页 |
| 0x1101~0x1105 | HMI 帧错误（长度/校验/帧尾/命令/参数越界） | 检查串口屏工程与线路 |
| 0x2101 | 学习无激励 | 检查 J_REF/J_MEAS 接线与 FPGA 状态3 |
| 0x2102 | 扫频未从 1kHz 开始 | 先按 FPGA 复位再学习 |
| 0x2103 | 学习超时(>110s) | 检查采集链路 |
| 0x2104 | 无法分类 | 检查未知电路连接 |
| 0x2201~0x2203 | 等效输入缺失 / 频率越界(<800Hz / >52kHz) | 检查发生器设置(题目范围 1k~50kHz) |

## 7. 校准（`Application/Inc/board_pins.h`）

| 常数 | 标定方法 |
|---|---|
| `CAL_ADC_MAIN_MV_PER_LSB` / `CAL_ADC_REF_MV_PER_LSB` | 给端口已知 Vpp 正弦，比对屏显读数 |
| `CAL_DAC_MV_PER_LSB`、`CAL_DAC_OUT_GAIN` | 示波器实测 J_OUT 幅度，按比例修正 |
| `CAL_LEARN_DRIVE_MVPP` | J_REF 未接线时的激励假设值，保持 2000 |

改动输出级增益后务必同步 `CAL_DAC_OUT_GAIN`，等效输出 10% 误差裕量主要取决于此。

## 8. PC 端单元测试

Application 层可在 PC 上用 gcc 全量仿真（含 5 种 RLC 拓扑学习、方波等效
输出与解析真值对比、FPGA 码表核对、协议帧样例）：

```bash
gcc -std=c99 -O2 -I Application/Inc -o test host_test/test_main.c \
    Application/Src/{hmi_protocol,known_model,fpga_ctrl,fpga_ctrl_table,\
signal_meas,rlc_model,wave_out,learn_engine,emulate_engine,g_app,hmi_screen}.c -lm
```

已验证结果：五种拓扑分类全部正确、f0 误差 <3%、等效方波 Vpp 误差 1.3%
（题目限 10%）、10kHz 输出 10 分钟漂移 0.06 个周期。

## 9. 已知限制（源于 FPGA 不可修改）

1. 3kHz~1MHz 区间无完整 100Hz 步进：J_OUT 可补到 200kHz，200kHz~1MHz 仅有 FPGA 的 1MHz/2MHz 两点。
2. FPGA Basic_four 表 1700Hz 行（1.1~2.0Vpp）频率字有误，固件已标记并用 J_OUT 兜底。
3. FPGA 扫频计数器仅复位清零，每次学习前必须先复位 FPGA。
4. FPGA Develop_two 状态输出悬空，禁止使用；等效输出全部由 J_OUT 承担。

详细协议与逐条需求核对见资料包：《STM32_FPGA通信协作_G题.md》《需求覆盖核对_G题.md》。
