# HMI 事件脚本 双路波形模板

本文只针对 `page0`。原则是：除 `b32`“应用并输出”外，任何按钮事件都只修改 HMI 本地变量和本地显示，不发送业务串口帧。

## 1. Program.s 全局初始化

目标工程 `屏控波形输出串口屏部分/双路波形控制.HMI` 的 `Program.s` 已按下面内容修正。打开 HMI 编辑器后请再次确认没有默认 `printh` 输出。

```text
int a_wave=0
int b_wave=0
int a_freq=1000
int b_freq=1000
int a_amp=1000
int b_amp=1000
int a_duty=50
int b_duty=50
int b_phase=0
int phase_send=0
int chk=0
int tmp=0
int step=0
int c_white=65535
int c_panel=6471
int c_blue=11263
int c_cyan=7671
int c_pink=63519
int c_black=0
baud=115200
dim=100
recmod=0
bkcmd=0
page 0
```

变量约定：

| 变量 | 含义 |
|---|---|
| `a_wave/b_wave` | 0 正弦，1 方波，2 三角 |
| `a_freq/b_freq` | A/B 频率，单位 Hz，范围 `1~20000000` |
| `a_amp/b_amp` | A/B 幅值，单位 mVpp，范围 `0~5000` |
| `a_duty/b_duty` | A/B 占空比，单位百分比，范围 `10~90` |
| `b_phase` | B 相对 A 的相位，单位 deg，范围 `-180~180` |
| `phase_send` | 负相位转补码后的临时发送值 |
| `chk/tmp/step` | 校验和、拆字节、步进临时变量 |

## 2. page0 后初始化事件

在 `page0` 的“后初始化事件”中粘贴。它只刷新本地显示，不发串口数据。

```text
n_a_freq.val=a_freq
n_a_amp.val=a_amp
n_a_duty.val=a_duty
n_b_freq.val=b_freq
n_b_amp.val=b_amp
n_b_duty.val=b_duty
n_phase.val=b_phase
t0.txt="调频"
t1.txt="调频"
t2.txt="B相对A"
```

然后继续粘贴本文第 3 节的 “A 路波形高亮片段” 和 “B 路波形高亮片段”。

## 3. 波形互斥高亮片段

每次选择波形后都要整段重绘同一路的 3 个按钮，这样才能保证同一路同一时刻只有一种波形高亮。

### 3.1 A 路波形高亮片段

```text
if(a_wave==0)
{
  fill 128,110,139,48,c_cyan
  xstr 128,110,139,48,1,c_white,c_cyan,1,1,1,"正弦"
}
else
{
  fill 128,110,139,48,c_panel
  xstr 128,110,139,48,1,c_white,c_panel,1,1,1,"正弦"
}

if(a_wave==1)
{
  fill 282,110,139,48,c_cyan
  xstr 282,110,139,48,1,c_white,c_cyan,1,1,1,"方波"
}
else
{
  fill 282,110,139,48,c_panel
  xstr 282,110,139,48,1,c_white,c_panel,1,1,1,"方波"
}

if(a_wave==2)
{
  fill 435,110,139,48,c_cyan
  xstr 435,110,139,48,1,c_white,c_cyan,1,1,1,"三角"
}
else
{
  fill 435,110,139,48,c_panel
  xstr 435,110,139,48,1,c_white,c_panel,1,1,1,"三角"
}
```

### 3.2 B 路波形高亮片段

```text
if(b_wave==0)
{
  fill 128,326,139,48,c_pink
  xstr 128,326,139,48,1,c_white,c_pink,1,1,1,"正弦"
}
else
{
  fill 128,326,139,48,c_panel
  xstr 128,326,139,48,1,c_white,c_panel,1,1,1,"正弦"
}

if(b_wave==1)
{
  fill 282,326,139,48,c_pink
  xstr 282,326,139,48,1,c_white,c_pink,1,1,1,"方波"
}
else
{
  fill 282,326,139,48,c_panel
  xstr 282,326,139,48,1,c_white,c_panel,1,1,1,"方波"
}

if(b_wave==2)
{
  fill 435,326,139,48,c_pink
  xstr 435,326,139,48,1,c_white,c_pink,1,1,1,"三角"
}
else
{
  fill 435,326,139,48,c_panel
  xstr 435,326,139,48,1,c_white,c_panel,1,1,1,"三角"
}
```

## 4. 波形按钮事件

以下脚本放到对应透明按钮的“弹起事件”。

`a_sine`：

```text
a_wave=0
```

然后粘贴第 3.1 节 A 路波形高亮片段。

`a_square`：

```text
a_wave=1
```

然后粘贴第 3.1 节 A 路波形高亮片段。

`a_triangle`：

```text
a_wave=2
```

然后粘贴第 3.1 节 A 路波形高亮片段。

`b_sine`：

```text
b_wave=0
```

然后粘贴第 3.2 节 B 路波形高亮片段。

`b_square`：

```text
b_wave=1
```

然后粘贴第 3.2 节 B 路波形高亮片段。

`b_triangle`：

```text
b_wave=2
```

然后粘贴第 3.2 节 B 路波形高亮片段。

## 5. 频率步进按钮事件

频率范围固定为 `1~20000000Hz`。所有频率按钮都使用“先按步长向下对齐，再加/减”的规则：

```text
tmp=当前频率%step
当前频率=当前频率-tmp+step
```

所以 `1Hz +100Hz => 100Hz`，`1001Hz +1KHz => 2000Hz`，不会出现 `101Hz/1001Hz` 这类残留尾数。

### 5.1 A 路频率按钮

`b0`，A 频率 `-100Hz`：

```text
step=100
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp-step
if(n_a_freq.val<1)
{
  n_a_freq.val=1
}
a_freq=n_a_freq.val
```

`b1`，A 频率 `+100Hz`：

```text
step=100
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp+step
if(n_a_freq.val>20000000)
{
  n_a_freq.val=20000000
}
a_freq=n_a_freq.val
```

`b10`，A 频率 `-1KHz`：

```text
step=1000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp-step
if(n_a_freq.val<1)
{
  n_a_freq.val=1
}
a_freq=n_a_freq.val
```

`b12`，A 频率 `+1KHz`：

```text
step=1000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp+step
if(n_a_freq.val>20000000)
{
  n_a_freq.val=20000000
}
a_freq=n_a_freq.val
```

`b9`，A 频率 `-10KHz`：

```text
step=10000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp-step
if(n_a_freq.val<1)
{
  n_a_freq.val=1
}
a_freq=n_a_freq.val
```

`b11`，A 频率 `+10KHz`：

```text
step=10000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp+step
if(n_a_freq.val>20000000)
{
  n_a_freq.val=20000000
}
a_freq=n_a_freq.val
```

`b6`，A 频率 `-100KHz`：

```text
step=100000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp-step
if(n_a_freq.val<1)
{
  n_a_freq.val=1
}
a_freq=n_a_freq.val
```

`b14`，A 频率 `+100KHz`：

```text
step=100000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp+step
if(n_a_freq.val>20000000)
{
  n_a_freq.val=20000000
}
a_freq=n_a_freq.val
```

`b7`，A 频率 `-1MHz`：

```text
step=1000000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp-step
if(n_a_freq.val<1)
{
  n_a_freq.val=1
}
a_freq=n_a_freq.val
```

`b13`，A 频率 `+1MHz`：

```text
step=1000000
tmp=n_a_freq.val%step
n_a_freq.val=n_a_freq.val-tmp+step
if(n_a_freq.val>20000000)
{
  n_a_freq.val=20000000
}
a_freq=n_a_freq.val
```

### 5.2 B 路频率按钮

`b15`，B 频率 `-100Hz`：

```text
step=100
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp-step
if(n_b_freq.val<1)
{
  n_b_freq.val=1
}
b_freq=n_b_freq.val
```

`b8`，B 频率 `+100Hz`：

```text
step=100
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp+step
if(n_b_freq.val>20000000)
{
  n_b_freq.val=20000000
}
b_freq=n_b_freq.val
```

`b23`，B 频率 `-1KHz`：

```text
step=1000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp-step
if(n_b_freq.val<1)
{
  n_b_freq.val=1
}
b_freq=n_b_freq.val
```

`b25`，B 频率 `+1KHz`：

```text
step=1000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp+step
if(n_b_freq.val>20000000)
{
  n_b_freq.val=20000000
}
b_freq=n_b_freq.val
```

`b22`，B 频率 `-10KHz`：

```text
step=10000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp-step
if(n_b_freq.val<1)
{
  n_b_freq.val=1
}
b_freq=n_b_freq.val
```

`b24`，B 频率 `+10KHz`：

```text
step=10000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp+step
if(n_b_freq.val>20000000)
{
  n_b_freq.val=20000000
}
b_freq=n_b_freq.val
```

`b21`，B 频率 `-100KHz`：

```text
step=100000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp-step
if(n_b_freq.val<1)
{
  n_b_freq.val=1
}
b_freq=n_b_freq.val
```

`b27`，B 频率 `+100KHz`：

```text
step=100000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp+step
if(n_b_freq.val>20000000)
{
  n_b_freq.val=20000000
}
b_freq=n_b_freq.val
```

`b20`，B 频率 `-1MHz`：

```text
step=1000000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp-step
if(n_b_freq.val<1)
{
  n_b_freq.val=1
}
b_freq=n_b_freq.val
```

`b26`，B 频率 `+1MHz`：

```text
step=1000000
tmp=n_b_freq.val%step
n_b_freq.val=n_b_freq.val-tmp+step
if(n_b_freq.val>20000000)
{
  n_b_freq.val=20000000
}
b_freq=n_b_freq.val
```

## 6. 幅值、占空比、相位按钮事件

### 6.1 A 路幅值

`b3`，A 幅值 `-100mVpp`：

```text
n_a_amp.val=n_a_amp.val-100
if(n_a_amp.val<0)
{
  n_a_amp.val=0
}
a_amp=n_a_amp.val
```

`b2`，A 幅值 `+100mVpp`：

```text
n_a_amp.val=n_a_amp.val+100
if(n_a_amp.val>5000)
{
  n_a_amp.val=5000
}
a_amp=n_a_amp.val
```

### 6.2 B 路幅值

`b17`，B 幅值 `-100mVpp`：

```text
n_b_amp.val=n_b_amp.val-100
if(n_b_amp.val<0)
{
  n_b_amp.val=0
}
b_amp=n_b_amp.val
```

`b16`，B 幅值 `+100mVpp`：

```text
n_b_amp.val=n_b_amp.val+100
if(n_b_amp.val>5000)
{
  n_b_amp.val=5000
}
b_amp=n_b_amp.val
```

### 6.3 A 路占空比

`b5`，A 占空比 `-5%`：

```text
tmp=n_a_duty.val%5
n_a_duty.val=n_a_duty.val-tmp-5
if(n_a_duty.val<10)
{
  n_a_duty.val=10
}
a_duty=n_a_duty.val
```

`b4`，A 占空比 `+5%`：

```text
tmp=n_a_duty.val%5
n_a_duty.val=n_a_duty.val-tmp+5
if(n_a_duty.val>90)
{
  n_a_duty.val=90
}
a_duty=n_a_duty.val
```

### 6.4 B 路占空比

`b19`，B 占空比 `-5%`：

```text
tmp=n_b_duty.val%5
n_b_duty.val=n_b_duty.val-tmp-5
if(n_b_duty.val<10)
{
  n_b_duty.val=10
}
b_duty=n_b_duty.val
```

`b18`，B 占空比 `+5%`：

```text
tmp=n_b_duty.val%5
n_b_duty.val=n_b_duty.val-tmp+5
if(n_b_duty.val>90)
{
  n_b_duty.val=90
}
b_duty=n_b_duty.val
```

### 6.5 B 相对 A 相位

`b28`，相位 `-5deg`：

```text
n_phase.val=n_phase.val-5
if(n_phase.val<-180)
{
  n_phase.val=-180
}
b_phase=n_phase.val
```

`b31`，相位 `+5deg`：

```text
n_phase.val=n_phase.val+5
if(n_phase.val>180)
{
  n_phase.val=180
}
b_phase=n_phase.val
```

`b29`，相位 `-10deg`：

```text
n_phase.val=n_phase.val-10
if(n_phase.val<-180)
{
  n_phase.val=-180
}
b_phase=n_phase.val
```

`b30`，相位 `+10deg`：

```text
n_phase.val=n_phase.val+10
if(n_phase.val>180)
{
  n_phase.val=180
}
b_phase=n_phase.val
```

## 7. 输出前统一限幅片段

`b32` 输出前先粘贴本片段，把用户手动输入的数字控件同步到变量并限幅。这样串口发出去的一定是边界内参数。

```text
a_freq=n_a_freq.val
a_amp=n_a_amp.val
a_duty=n_a_duty.val
b_freq=n_b_freq.val
b_amp=n_b_amp.val
b_duty=n_b_duty.val
b_phase=n_phase.val

if(a_wave<0)
{
  a_wave=0
}
if(a_wave>2)
{
  a_wave=2
}
if(b_wave<0)
{
  b_wave=0
}
if(b_wave>2)
{
  b_wave=2
}

if(a_freq<1)
{
  a_freq=1
}
if(a_freq>20000000)
{
  a_freq=20000000
}
if(b_freq<1)
{
  b_freq=1
}
if(b_freq>20000000)
{
  b_freq=20000000
}

if(a_amp<0)
{
  a_amp=0
}
if(a_amp>5000)
{
  a_amp=5000
}
if(b_amp<0)
{
  b_amp=0
}
if(b_amp>5000)
{
  b_amp=5000
}

if(a_duty<10)
{
  a_duty=10
}
if(a_duty>90)
{
  a_duty=90
}
tmp=a_duty%5
a_duty=a_duty-tmp
if(a_duty<10)
{
  a_duty=10
}

if(b_duty<10)
{
  b_duty=10
}
if(b_duty>90)
{
  b_duty=90
}
tmp=b_duty%5
b_duty=b_duty-tmp
if(b_duty<10)
{
  b_duty=10
}

if(b_phase<-180)
{
  b_phase=-180
}
if(b_phase>180)
{
  b_phase=180
}

n_a_freq.val=a_freq
n_a_amp.val=a_amp
n_a_duty.val=a_duty
n_b_freq.val=b_freq
n_b_amp.val=b_amp
n_b_duty.val=b_duty
n_phase.val=b_phase
```

## 8. b32 输出按钮完整脚本

`b32` 的“弹起事件”按顺序粘贴：先粘贴第 7 节“输出前统一限幅片段”，再粘贴下面的发帧脚本。这里是整个模板唯一允许出现 `printh AA` 和 `prints` 发送业务帧的位置。

```text
chk=0xAA+0x21+0x16+1+3+a_wave

tmp=a_freq
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

tmp=a_amp
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

tmp=a_duty*10
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

chk=chk+b_wave

tmp=b_freq
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

tmp=b_amp
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

tmp=b_duty*10
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

phase_send=b_phase
if(phase_send<0)
{
  phase_send=65536+phase_send
}
tmp=phase_send
chk=chk+tmp%256
tmp=tmp/256
chk=chk+tmp%256

printh AA 21 16
printh 01 03
prints a_wave,1
prints a_freq,4
prints a_amp,2
tmp=a_duty*10
prints tmp,2
prints b_wave,1
prints b_freq,4
prints b_amp,2
tmp=b_duty*10
prints tmp,2
prints phase_send,2
prints chk,1
printh 55
```

发出的帧格式为：

```text
AA 21 16 DATA CHECK 55
```

`DATA` 固定 22 字节，小端序：

```text
01 03 wave_a freq_a[4] amp_a[2] duty_a_pct10[2] wave_b freq_b[4] amp_b[2] duty_b_pct10[2] phase[2]
```

## 9. 可选返回按钮

如果保留底部“返回”区域，可新增透明按钮 `b_back`，坐标 `20,519,243,64`。它只跳回当前页或预留为空，不能发业务串口帧。

```text
page page0
```

## 10. 粘贴后自查

- `Program.s` 中有 `baud=115200`、`recmod=0`、`bkcmd=0`。
- `Program.s` 中没有上电 `printh`。
- 除 `b32` 外，所有按钮事件中都没有 `printh AA` 和 `prints`。
- A 路 3 个波形按钮只改 `a_wave` 并重绘 A 路。
- B 路 3 个波形按钮只改 `b_wave` 并重绘 B 路。
- 频率按钮全部使用对齐步进脚本。
- `b32` 输出负相位前使用 `phase_send=65536+b_phase` 转成 16 位补码。
