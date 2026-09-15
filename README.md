# 定位式水浸检测系统 — 需求分析与代码实现详解

> 目标读者:接手本项目的嵌入式工程师(熟悉 STM32 标准外设库,不了解本项目)
> 对应代码:`D:\shangguigu\All_Project\07_\code_pro\water weak detection`
> 需求文档:`D:\shangguigu\All_Project\07_\Resource\漏水检测资料\漏水检测需求文档.md`
> 原理图:`D:\shangguigu\All_Project\07_\原理图.pdf`
> 文档日期:2026-09-14

---

## 目录

1. [项目概述](#1-项目概述)
2. [需求分析](#2-需求分析)
3. [硬件架构与引脚映射](#3-硬件架构与引脚映射)
4. [代码总体架构](#4-代码总体架构)
5. [Com 层逐文件详解](#5-com-层逐文件详解)
6. [Int 层逐文件详解](#6-int-层逐文件详解)
7. [App 层逐文件详解](#7-app-层逐文件详解)
8. [User 层:五个主程序](#8-user-层五个主程序)
9. [编译配置](#9-编译配置)
10. [开发过程修复记录](#10-开发过程修复记录)
11. [已知问题与未验证项](#11-已知问题与未验证项)
12. [标定指南](#12-标定指南)
13. [项目开发流程图](#13-项目开发流程图)

---

# 1. 项目概述

本系统用于监测一根四芯检测线缆(红、黄、绿、黑)的**断线故障**和**水浸故障**,并在水浸时**定位故障点到控制器的距离**。

核心思路:MCU 通过 GPIO 控制模拟开关(74HC4066 通断 + HEF4052 选通道),在不同开关组合下用 ADC 测量线缆上不同节点的电压(V1~V10 共 10 个),据此完成三种判定:

| 功能     | 判据                                           | 输出                     |
| -------- | ---------------------------------------------- | ------------------------ |
| 断线监测 | 供电线与返回线电压应相等;返回线读到 0 说明断了 | 报警 + 指出红/黄哪条回路 |
| 水浸监测 | 干燥时 V6≈3.3V、V5≈0;进水时两者都被拉到中间    | 报警                     |
| 水浸定位 | 用 V5/V6 和 V9/V10 两组分压代入公式算 L1       | 距离(米)                 |

MCU 为 STM32F103C8T6(64KB Flash / 20KB RAM,LQFP48),标准外设库 V3.5,Keil MDK + armcc V5.06。

---

# 2. 需求分析

需求文档把开发分成两个阶段,外加阶段三(硬件升级,后期补充)。

## 2.1 控制引脚定义(需求文档《一》)

| 引脚 | 功能                  | 代码落点                                       |
| ---- | --------------------- | ---------------------------------------------- |
| PB4  | 红线供电(=1 通电)     | `RED_PWR_PIN`,`Int_Wire.c: Wire_SetRedPower()` |
| PB3  | 黄线供电              | `YEL_PWR_PIN`,`Wire_SetYellowPower()`          |
| PA15 | 绿线接取样电阻 R9/R11 | `GRN_R9_PIN`,`Wire_SetGreenToR9()`             |
| PB5  | 黑线接取样电阻 R9/R11 | `BLK_R9_PIN`,`Wire_SetBlackToR9()`             |
| PB6  | 4052 地址线 S1        | `SW_S1_PIN`,`Wire_SelectPath()`                |
| PB7  | 4052 地址线 S2        | `SW_S2_PIN`,`Wire_SelectPath()`                |

**PA15/PB3/PB4 的 JTAG 陷阱**:这三个脚复位后默认是 JTAG 功能(JTDI/JTDO/JNTRST),不是普通 GPIO。必须在一切 GPIO 操作之前调用 `Board_Init()` 关闭 JTAG(保留 SWD),否则 `GPIO_Init`/`GPIO_WriteBit` 全部"正常返回"但引脚电平纹丝不动 —— 代码看着对、硬件毫无反应、编译零警告,是最难查的一类故障。`Com_Board.c` 里只有这一个动作,却必须第一个调用,原因就在这。

## 2.2 模拟开关组合(需求文档《ADC通道配置》)

PB6/PB7 是 HEF4052(双 4 选 1 模拟开关)的两位地址线,同时切换两个 bank:

| PB6(S1) | PB7(S2) | ADC0 测量线 | ADC1 测量线 | 代码枚举                      |
| ------- | ------- | ----------- | ----------- | ----------------------------- |
| 0       | 0       | 黄线        | 黑线        | `WIRE_PATH_YELLOW_BLACK`(=0)  |
| 1       | 0       | 绿线        | 黄线        | `WIRE_PATH_GREEN_YELLOW`(=1)  |
| 0       | 1       | 红线        | 绿线        | `WIRE_PATH_RED_GREEN`(=2)     |
| 1       | 1       | R9 上端     | R9 下端     | `WIRE_PATH_R9_TOP_BOTTOM`(=3) |

注意通道号 bit0 → S1(PB6)、bit1 → S2(PB7),`Wire_SelectPath()` 按 `path & 0x01` 和 `path & 0x02` 拆位写入。

**命名陷阱**:原理图网络名 "ADC0"/"ADC1" 指的是 4052 输出的两路模拟信号,与 STM32 的 ADC1 外设、ADC 通道号完全是两码事:

```
原理图网络 ADC0 → PA1 → STM32 ADC1 外设的通道 1 (ADC_Channel_1)
原理图网络 ADC1 → PA2 → STM32 ADC1 外设的通道 2 (ADC_Channel_2)
```

两路模拟信号挂在**同一个** ADC1 外设上,靠切换通道分别采集。`Int_Adc.c` 开头的注释专门写了这个陷阱。

## 2.3 六组测量组合与 V1~V10(需求文档《四、电压测量总表》)

| 阶段       | 目标  | 红供电PB4 | 黄供电PB3 | 绿接R9 PA15 | 黑接R9 PB5 | 4052通道 | 读数                    |
| ---------- | ----- | --------- | --------- | ----------- | ---------- | -------- | ----------------------- |
| 断线监测1  | V1,V2 | 1         | 1         | 0           | 0          | 0        | ADC1=V1(黑) ADC0=V2(黄) |
| 断线监测2  | V3,V4 | 1         | 1         | 0           | 0          | 2        | ADC1=V3(绿) ADC0=V4(红) |
| 水浸监测   | V5,V6 | 1         | 0         | 0           | 1          | 1        | ADC1=V5(黄) ADC0=V6(绿) |
| 水浸定位1  | V7,V8 | 0         | 1         | 1           | 0          | 1        | ADC1=V7(黄) ADC0=V8(绿) |
| 水浸定位2a | V9    | 1         | 0         | 0           | 1          | 2        | ADC0=V9(红)             |
| 水浸定位2b | V10   | 1         | 0         | 0           | 1          | 0        | ADC1=V10(黑)            |

这六行在 `App_Measure.c` 的 `Measure_ReadBreak()/ReadLeak()/ReadLocate()` 逐行落地,每组都是 `Wire_Apply(红,黄,绿,黑,通道)` → 读两路 ADC。

**为什么定位2 要分两次**:4052 两个 bank 共用地址线,没有哪个通道能同时把 ADC0 接红线、ADC1 接黑线,所以 V9 和 V10 只能切两次通道分别采。

## 2.4 三个判定逻辑(需求文档《三、核心功能逻辑》)

### 断线监测

物理原理:红黄两线在远端与绿黑短接构成回路。回路通时,供电线和返回线上电压相等;某根断了,返回线浮空,读数掉到 0 附近。

- 正常:`V1 = V2` 且 `V3 = V4`
- 断线:`V1 ≠ V2` 或 `V3 ≠ V4`(若 `V1=0` 或 `V3=0` 则确认断线)

代码(`Measure_CheckBreak()`)在文档判据上补了两点:

1. "相等"用容差 `MEAS_EQUAL_TOL_MV`(默认 50mV)实现,实际电路不可能严格相等;
2. 文档的"V1=0 确认断线"实现为**第二个或条件**——它能覆盖一种边界情况:如果供电线本身断了,V1/V2 可能都接近 0,差值反而在容差内,只看差值会漏判。

### 水浸监测

物理原理:红线加 3.3V,黑线经取样电阻到地构成回路;黄、绿两线悬空,只通过线缆间绝缘电阻与红黑耦合。干燥时绝缘电阻极大 → 绿线被拉到接近 3.3V、黄线接近 0;进水时水形成有限电阻,把两边电位拉到中间。

- 无水浸:`V6 = 3.3V` 且 `V5 = 0`
- 有水浸:`V5`、`V6` 均在 0~3.3V 之间

代码(`Measure_CheckLeak()`)取反实现:`V6 < MEAS_FULL_MV || V5 > MEAS_ZERO_MV` 即判水浸。两个条件取**或**不取与——漏水点靠近某一端时可能只有一侧电压变化明显,取与会漏判。

### 水浸定位(核心公式)

```
VA2B2 = V5 - V6
VA1B1 = V9 - V10

L1 = 1/(2r) × [ rL + 4000×(VA1B1/(VA1B1-3.3)) − 4000×(VA2B2/(VA2B2-3.3)) ]
```

- `r`:线缆单位长度电阻(Ω/m),必须实测
- `L`:线缆总长度(m),必须实测
- 常数 `4000`:对应原理图 R11 = 3.9kΩ 取整。是否应改成 3900 是阶段一要回答的问题之一——有可能公式推导时把 4052 通道电阻和 2k 保护电阻也算进去了,4000 反而更准。**不要提前"优化"**。

代码(`Measure_CalcL1()`)照抄公式,补了文档没写的边界保护:分母接近 0 防除零;结果要求 `0 ≤ L1 ≤ L` 否则判无效(`pos_valid=0`)。

**这个边界保护正是当前"定位算不出"的直接原因**——见[第 11 节](#11-已知问题与未验证项)。

## 2.5 两个开发阶段(需求文档《二、工作阶段》)

**阶段一(原理验证)**:按序测量 V1~V10,485 实时输出,验证定位公式。要回答四个问题:

1. 六组开关组合下 V1~V10 实测值是否符合预期极性和量级;
2. 公式常数取 4000 还是 R11 实际值 3900;
3. 线缆定位精度要求的 r 实际值是多少;
4. 4066/4052 切换后多久稳定(代码取 `WIRE_SETTLE_MS = 15ms`,文档要求 >10ms)。

**阶段二(正式运行)**:常态循环监测(断线+水浸),异常时屏幕显示报警/485 输出/蜂鸣器继电器联动;屏幕菜单可配置参数(线缆长度、蜂鸣器开关、报警阈值等),参数掉电保存。

**阶段三(硬件升级,后期追加)**:外置 SGM58031 16bit I2C ADC 替代内部 12bit ADC 提精度;OLED 中文字库;历史记录;采样间隔/去抖次数等更多参数。

## 2.6 附加要求(需求文档《五》)

- 485 接口持续输出监测数据(电压值、故障状态、定位结果);
- 故障时触发蜂鸣器、继电器(如切断电源);
- 通过屏幕菜单调整 r、L 等参数;
- 切换状态时延时确保电压稳定(>10ms)。

---

# 3. 硬件架构与引脚映射

## 3.1 引脚总表(按 LQFP48 物理脚位)

| MCU 引脚  | 网络        | 功能                                         | 代码定义处           |
| --------- | ----------- | -------------------------------------------- | -------------------- |
| PA1       | ADC0        | 4052 输出→内部 ADC 通道1                     | `ADC_NET0_PIN`       |
| PA2       | ADC1        | 4052 输出→内部 ADC 通道2                     | `ADC_NET1_PIN`       |
| PA3       | ALERT       | SGM58031 转换完成中断(阶段三用,暂未接)       | `ADC_ALERT_PIN`      |
| PA4       | BELL        | 蜂鸣器驱动(经 R10→Q1)                        | `BELL_PIN`           |
| PA5       | RELAY       | 继电器驱动(经 R12→Q2)                        | `RELAY_PIN`          |
| PA8       | (按键OK)    | 按键,低电平有效                              | `Int_Key.c` 本地定义 |
| PA9/PA10  | UART1       | 调试串口 115200-8-N-1                        | `UART_TX/RX_PIN`     |
| PA15      | GRN_R9      | 绿线接取样电阻                               | `GRN_R9_PIN`         |
| PB0       | (按键DOWN)  | 按键(⚠与 OLED SCL 归属存疑,见 11.4)          | `Int_Key.c` 本地定义 |
| PB1       | SCL         | OLED 软件 I2C 时钟                           | `OLED_SCL_PIN`       |
| PB2       | SDA         | OLED 软件 I2C 数据                           | `OLED_SDA_PIN`       |
| PB3       | YEL_PWR     | 黄线供电                                     | `YEL_PWR_PIN`        |
| PB4       | RED_PWR     | 红线供电                                     | `RED_PWR_PIN`        |
| PB5       | BLK_R9      | 黑线接取样电阻                               | `BLK_R9_PIN`         |
| PB6/PB7   | S1/S2       | 4052 地址线                                  | `SW_S1/S2_PIN`       |
| PB10/PB11 | RS485 TX/RX | USART3,接 U2 = MAX13487(自动方向)            | `RS485_TX/RX_PIN`    |
| PB12      | —           | **未使用**(MAX13487 自动换向,不需要 DE 引脚) | —                    |

## 3.2 模拟通路框图

```
                     +---- 74HC4066(4对4通道开关) ----+
 红线 ──[PB4 控制]──┤                                  ├──→ 4052 bank X ──→ ADC0(PA1)
 黄线 ──[PB3 控制]──┤                                  │
 绿线 ──[PA15 控制]─┤  (通/断由 GPIO 决定)             ├──→ 4052 bank Y ──→ ADC1(PA2)
 黑线 ──[PB5 控制]──┤                                  │
                     +----------------------------------+
                        取样电阻 R11(≈3.9k)到地,接在"接R9"那根线上

 另一路(阶段三):同两网络经 R14/R15(0Ω)接到 U5 SGM58031 的 AIN0/AIN1(I2C2)
```

4066 决定"哪根线被加电/被接取样电阻",4052 决定"ADC 看哪两根线"。

## 3.3 执行机构

- **蜂鸣器**:MLT-8530 无源电磁式,共振 2700Hz,线圈 16Ω,经 R10(2k)→Q1(9013)驱动,PA4 高电平有效。⚠R9(10k)疑似串在 BUZZER1 供电路径上导致声音极小——硬件存疑项,见 11.3。
- **继电器**:PA5 经 R12(2k)→Q2,高电平吸合。R16(10k)基极下拉防复位期间误动作。

---

# 4. 代码总体架构

三层架构,依赖只能向下:

```
┌─────────────────────────────────────────────────┐
│ User 层   main.c / main_stage2.c / main_unified.c│
│           main_with_oled.c(当前烧录)/ main_with_menu.c │
├─────────────────────────────────────────────────┤
│ App 层    App_Measure  采集与判定(V1~V10、三判定)│
│           App_State    状态机(去抖、报警联动)    │
│           App_Config   参数 + Flash 存储 + CRC    │
│           App_Display  OLED 业务显示              │
│           App_Menu     按键菜单(未启用)          │
├─────────────────────────────────────────────────┤
│ Int 层    Int_Wire     4066/4052 通路控制        │
│           Int_Adc      内部 12bit ADC            │
│           Int_Sgm58031 外置 16bit ADC(阶段三)    │
│           Int_Buzzer   蜂鸣器(TIM3 软件方波)     │
│           Int_Relay    继电器                     │
│           Int_Rs485    RS485(USART3)             │
│           Int_Oled     OLED(SSD1306 软件I2C)     │
│           Int_Font_CN  16×16 中文字库             │
│           Int_Key      3 按键扫描(未启用)        │
├─────────────────────────────────────────────────┤
│ Com 层    Com_Board    引脚登记 + JTAG 关闭       │
│           Com_Delay    SysTick 1ms 时基           │
│           Com_Uart     调试串口 + printf 重定向   │
├─────────────────────────────────────────────────┤
│ Start/Library  CMSIS + 标准外设库(未改动)        │
└─────────────────────────────────────────────────┘
```

初始化顺序(以当前 `main_with_oled.c` 为准,**顺序不能乱**):

```
Board_Init()   ← 必须最先:关 JTAG,否则 PA15/PB3/PB4 全是死脚
Delay_Init()   ← SysTick 时基,Wire_Apply/OLED 都依赖
Uart_Init()    ← 第一个 printf 之前
Buzzer_Init() / Relay_Init() / Rs485_Init()
Config_Init()  ← 从 Flash 读参数(内部会同步给测量层)
Measure_Init() ← 内部调 Wire_Init + Adc_Init(阶段二)
State_Init()   ← 状态机清零 + 蜂鸣器/继电器置静音
Display_Init() ← 内部调 Oled_Init,显示开机画面
```

---

# 5. Com 层逐文件详解

## 5.1 Com_Board.h / Com_Board.c —— 板级引脚登记

**职责**:登记全板引脚宏;提供 `Board_Init()`。

`Com_Board.c` 全文只有一个函数,但它是全工程的"地基":

```c
void Board_Init(void)
{
    /* AFIO 时钟必须先开,否则 GPIO_PinRemapConfig 写不进去 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* 关闭 JTAG,保留 SWD */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
}
```

逐行:

- **第 1 行**:AFIO 时钟。重映射寄存器挂在外设总线上,时钟没开时 `GPIO_PinRemapConfig()` 会静默失效——又一个"代码对但没反应"的坑。
- **第 2 行**:`SWJ_JTAGDisable` 只关 JTAG(释放 PA15/PB3/PB4),保留 SWD(PA13/PA14)。如果误用 `SWJ_Disable` 把 SWD 也关了,下载器连不上,只能靠 BOOT0 进串口 bootloader 救砖。

`Com_Board.h` 是全工程的引脚字典,每个宏都带"网络名→引脚号→物理封装脚位"的注释,例如 BELL 一节注明 `pin 14 = PA4 网络 BELL`、继电器注明驱动链 `PA5 → R12(2k) → Q2 基极`。改板/查线时先看这个文件。

## 5.2 Com_Delay.h / Com_Delay.c —— SysTick 时基

**职责**:1ms 毫秒时基、毫秒/微秒延时、超时判断。全工程所有"时间"都从这来。

```c
static volatile uint32_t s_tick_ms = 0;   // volatile:中断写、主循环读
static volatile uint8_t  s_running  = 0;
```

- `volatile` 是必须的:`s_tick_ms` 在中断里自增、在主循环里读,编译器若把它缓存到寄存器,`Delay_ms()` 的等待循环会死转。

```c
void Delay_Init(void)
{
    s_tick_ms = 0;
    if (SysTick_Config(SystemCoreClock / 1000U) == 0U)  // 72000 个时钟=1ms
    {
        s_running = 1;
    }
}
```

- `SysTick_Config()` 来自 CMSIS,配置重装载值并**同时使能中断和计数**。72MHz / 1000 = 72000 个时钟周期正好 1ms。

```c
void SysTick_Handler(void)
{
    s_tick_ms++;

    /* 蜂鸣器节拍推进放在这里,不放主循环。
     * 原因:带 OLED 的主循环一轮要 600ms 以上(一轮完整采集约 550ms,
     * 加上刷屏和 Delay_ms),而 LEAK 节拍是 200ms 响 / 200ms 停。
     * 主循环调用 Buzzer_Poll() 追不上 200ms 的拍子…… */
    Buzzer_Poll();
}

```

- `SysTick_Handler` 定义在这里(不在 `stm32f10x_it.c`),启动文件里的弱定义被它覆盖。
- **`Buzzer_Poll()` 挂在这里是 2026-09-14 修的关键 bug**:原来它在主循环里被调,主循环一轮 682ms,LEAK 节拍 200ms,每次进 Poll 都发现"早该切换",`Buzzer_Start()` 刚开 TIM3 下一轮就关——发声时间被压缩到听不见。挂到 1ms 中断后节拍精度与主循环彻底解耦。`Buzzer_Poll()` 只做几个整数比较和 GPIO/TIM 操作,1ms 一次的开销可忽略。

```c
uint8_t Delay_IsTimeout(uint32_t start, uint32_t ms)
{
    return ((uint32_t)(s_tick_ms - start) >= ms) ? 1U : 0U;
}

```

- **无符号减法天然处理回绕**:`s_tick_ms` 是 32 位,约 49.7 天回绕一次。回绕瞬间 `s_tick_ms - start` 因无符号溢出而"跨零连续",比较依然正确。所有周期性判断都应走这个函数,不要自己写 `tick > start + ms`(那种写法在回绕时会死锁)。

```c
void Delay_us(uint32_t us)
{
    volatile uint32_t n = us * 12U;
    while (n--) { __NOP(); }
}

```

- 粗略软件延时:72MHz 下每轮循环约 6 周期,故 ×12。只用于"等硬件稳定"这类不要求精度的场合(如软件 I2C 位延时)。`volatile` 防止空循环被优化掉。

## 5.3 Com_Uart.h / Com_Uart.c —— 调试串口

**职责**:UART1(PA9=TX, PA10=RX)115200-8-N-1,提供 printf 重定向。

阶段一用它代替 RS485 输出 V1~V10:485 要接转换器才能看数据,UART1 在 P3 排针直接引出,插 USB-TTL 就能看,少一个环节少一个故障点。

引脚宏走 `Com_Board.h` 的 `DBG_UART*` 组(不是直接写 USART1):`DBG_UART = USART1`、`DBG_UART_TX_PIN = GPIO_Pin_9`、`DBG_UART_RX_PIN = GPIO_Pin_10`、`DBG_UART_BAUD = 115200`。换调试口只改宏。

发送与重定向:

```c
void Uart_SendByte(uint8_t b)
{
    USART_SendData(DBG_UART, b);                                    /* 先写 DR */
    while (USART_GetFlagStatus(DBG_UART, USART_FLAG_TXE) == RESET)  /* 再等 DR 空 */
    {
        /* 等发送数据寄存器空 */
    }
}

void Uart_SendString(const char *s)
{
    while (*s) { Uart_SendByte((uint8_t)*s++); }
}

int fputc(int ch, FILE *f)
{
    (void)f;
    Uart_SendByte((uint8_t)ch);
    return ch;
}

```

逐点说明:

- **顺序是"先发后等",不是"先等后发"**。`USART_SendData` 写 DR;随后等 TXE(DR 已空、内容进了移位寄存器)才返回。这样返回时 DR 一定可用,连续调用不会覆盖未发出的字节。等 TC(整帧移出)会更慢且没必要——最后一个字节交给硬件发完即可,除非紧接着复位 MCU。
- **MicroLIB 前提**:`Com_Uart.c` 的注释写明"工程 Options → Target 已勾选 Use MicroLIB,这里实现 fputc 即可";若取消 MicroLIB 要改用 `__io_putchar` + 完整 retarget。
- **代价**:MicroLIB 的 printf **不支持 `%f`**。所以全工程电压都用"整数毫伏 + 手动拆小数位"打印(`%d.%03d` 配 `mv/1000`、`mv%1000`)。
- `Uart_Init()` 用 `USART_Mode_Tx | USART_Mode_Rx` 同时开收,但工程里**没有配置 USART1 接收中断**(注释:"只用 TX/RX 不用中断"),即 USART1 只用于输出,不解析上位机下行命令。

**编码注意**:源文件是 UTF-8,串口助手按 GBK 解码时中文必乱码;且 armcc 按 GBK 解析源文件时,单个汉字字面量可能被截断导致 `error #8: missing closing quote`。**调试打印一律用英文**,这是全工程的既定约定。

## 5.4 stm32f10x_it.c —— 中断向量文件(标准库空模板)

位于 `User\` 目录,是标准库自带的模板文件,**本项目基本没动它**。但要理解它"少了什么",因为中断处理函数分散在别的文件里:

| 中断              | 定义位置           | 说明                                |
| ----------------- | ------------------ | ----------------------------------- |
| `SysTick_Handler` | `Com\Com_Delay.c`  | 1ms 时基 + `Buzzer_Poll()` 节拍推进 |
| `TIM3_IRQHandler` | `Int\Int_Buzzer.c` | 5400Hz 翻转 PA4,生成 2702.7Hz 方波  |

**关键点:这两个都定义在各自驱动里,不在 `stm32f10x_it.c`。** 启动文件 `startup_stm32f10x_md.s` 里所有 IRQHandler 都是**弱定义**(`[WEAK]`),指向同一个死循环;驱动里写同名强函数即可覆盖,不会重复定义。反过来,如果谁"顺手"在 `stm32f10x_it.c` 里也写一个 `TIM3_IRQHandler`,就会**链接期符号重定义**。

`stm32f10x_it.c` 里实际有效的内容:

- `NMI_Handler` / `SVC_Handler` / `DebugMon_Handler` / `PendSV_Handler`:空函数;
- `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler`:`while(1){}` 死循环——**这是默认的"故障即挂起"行为**,现场遇到板子"运行中突然不动了"而串口无输出,第一嫌疑就是撞进了这里。调试时可在 HardFault_Handler 里打断点或加 `printf`,正式产品建议改成记录现场后复位;
- 底部 `/*void PPP_IRQHandler(void){}*/` 是被注释掉的外设中断模板,本身未启用。

**本项目没有使用的中断**:USART1/USART3 收发全部走阻塞轮询(无 RXNE 中断)、ADC 无 EOC 中断、I2C 无事件中断(OLED 是软件 I2C)。所以整份文件只有上面那几个内核异常处理是活的。

---

# 6. Int 层逐文件详解

## 6.1 Int_Wire.h / Int_Wire.c —— 模拟通路控制

**职责**:把"我要测哪两根线"翻译成 6 个 GPIO 的置位,并等待稳定。

```c
typedef enum {
    WIRE_PATH_YELLOW_BLACK = 0,   /* 通道0: ADC0=黄 ADC1=黑 */
    WIRE_PATH_GREEN_YELLOW,       /* 通道1: ADC0=绿 ADC1=黄 */
    WIRE_PATH_RED_GREEN,         /* 通道2: ADC0=红 ADC1=绿 */
    WIRE_PATH_R9_TOP_BOTTOM,     /* 通道3: ADC0=R9上 ADC1=R9下 */
} WirePath_t;

```

核心函数 `Wire_Apply()`:

```c
void Wire_Apply(uint8_t red, uint8_t yellow, uint8_t green, uint8_t black,
                WirePath_t path)
{
    Wire_AllOff();            /* 1. 先断全部 4066 */
    Wire_SelectPath(path);    /* 2. 再选 4052 通道 */
    Wire_SetSwitches(red, yellow, green, black);  /* 3. 最后合 4066 */
    Delay_ms(WIRE_SETTLE_MS); /* 4. 等稳定(15ms) */
}

```

这个顺序有三层设计:

1. **先断开**:上一项测量的通路若还接着,和新通路会短暂并联,在线缆上形成意外回路。断一下代价小,省掉一类难查的串扰。
2. **先选通道再供电**:4052 切换瞬间输出有短暂浮空/毛刺,让它发生在线缆还没加电时,采样点更干净。
3. **等 15ms**:切换瞬间线缆分布电容和取样电阻构成 RC 充放电,不等够会采到过渡值。`WIRE_SETTLE_MS = 15` 是需求文档">10ms"的保守取值,也是阶段一要标定的四个数之一。

`Wire_Init()` 把 PB3~PB7(GPIOB 五根)一次配完、PA15(GPIOA 一根)单独配,全部推挽输出、初始全 0(所有通路断开)。注释强调:上电后 4052 停在通道 0、线缆无电压——安全的静止态,**长期给检测线加电会加速电极电化学腐蚀**,所以 `Measure_ReadAll()` 采完也会 `Wire_AllOff()`。

## 6.2 Int_Adc.h / Int_Adc.c —— 内部 12bit ADC

**职责**:ADC1 外设初始化 + 双通道(网络 ADC0=通道1 / 网络 ADC1=通道2)轮询采集。

初始化三个关键决策:

1. **ADC 时钟 = PCLK2/6 = 12MHz**。F103 的 ADC 时钟上限 14MHz,/6 是最接近的合法分频;取 /2 得 36MHz 会超规格,读数不可信。
2. **采样时间 239.5 周期(最长档)**。线缆是高阻源(串 2k 保护电阻 + 4066/4052 通道电阻,源阻抗约 6.6kΩ),ADC 内部 12.5pF 保持电容要靠外电路充电,手册规定该量级需要 239.5 周期。12MHz 下一次转换约 21µs,采样节奏是毫秒级,完全来得及。**取太短会导致"电压总是差一点"且随源阻抗漂**。
3. **上电必校准**:`ADC_ResetCalibration → 等自清 → ADC_StartCalibration → 等自清`,顺序不能颠倒。不校准读数有几十 LSB 的固定偏差。

采集链:

```c
static uint16_t Adc_ReadOnce(uint8_t channel)     /* 单次:配通道→软件触发→忙等EOC→读DR */
static uint16_t Adc_ReadFiltered(uint8_t channel) /* 9次采样,去最大最小,取平均 */

```

- 为什么用轮询不用 DMA:一轮测量只采 10 个点,每点前还要等 15ms 开关稳定——瓶颈在等待不在传输,DMA 省下的几微秒毫无意义。
- **去极值平均**的针对性:噪声主要来自继电器/蜂鸣器动作时的电源扰动,是脉冲状的,正好被"去掉一个最大一个最小"吃掉。累加用 `uint32_t`:9 × 4095 = 36855,`uint16_t` 会溢出。
- `ADC_RegularChannelConfig()` 每次重设,因为两个通道轮换占用规则组第 1 个位置。
- 读 DR 自动清 EOC,不用手动清标志。

对外只有两个函数:`Adc_ReadNet0()`、`Adc_ReadNet1()`,返回 0~4095 原始码值。

## 6.3 Int_Buzzer.h / Int_Buzzer.c —— 蜂鸣器

**职责**:MLT-8530 无源蜂鸣器驱动,以 2700Hz 方波驱动 PA4,提供多种"响-停"节拍。

### 为什么必须给方波

MLT-8530 是**无源电磁式**:里面只有线圈和振膜,没有振荡电路。BELL 恒定拉高只会让振膜吸合一次,"嗒"一声就停。必须以共振频率 2700Hz、50% 占空比翻转,振膜才按共振频率往复发声。手册参数:共振 2700Hz、线圈 16Ω±3、额定 3.6Vo-p(工作 2.5~4.5)、95mA@5Vo-p 方波、声压 min 80dB@10cm。

### 为什么用定时器中断翻转而不是硬件 PWM

PA4 的复用功能只有 SPI1_NSS / USART2_CK / ADC_IN4,**不挂任何 TIM 通道**,重映射也解决不了(TIM3 全重映射到 PC6-9,TIM2 各档到 PA0-3/PA15/PB3/PB10-11,都不含 PA4)。所以让 TIM3 以 2 倍频率中断,每次中断翻转一次电平。

### 频率推导(Int_Buzzer.c 开头注释原文逻辑)

```
目标方波 2700Hz → 每半周期翻转一次 → 中断频率 = 2 × 2700 = 5400Hz
TIM3 挂 APB1:APB1=36MHz,但 APB 预分频≠1 时定时器时钟 = APB × 2 = 72MHz
预分频 71(÷72)得 1MHz 计数时钟,1 计数 = 1µs
ARR = 1000000/5400 − 1 = 184.18 → 184
实际中断 1000000/185 = 5405.4Hz → 方波 2702.7Hz,偏差 +0.1%

```

### 节拍状态机

```c
static const BuzzerBeat_t s_beat[] =
{
    {   0,    0 },     /* OFF   : 静音                        */
    { 100,    0 },     /* BEEP  : 响一声就停(单次,s_beep_done) */
    { 200,  200 },     /* LEAK  : 急促,听着就紧张             */
    { 500, 1500 },     /* BREAK : 缓慢,和水浸区分开           */
    {   0,    0 }      /* ON    : 长鸣,特殊处理               */
};

```

索引与 `BuzzerPattern_t` 枚举严格对应,改枚举这张表要跟着改。断线和水浸用不同节奏,值班的人**不用看屏幕就能分辨**。

`Buzzer_SetPattern()`:同节奏不重置节拍;新节奏立即从"响"开始,不等上一拍结束。LEAK/BREAK 模式下设完立即 `Buzzer_Start()`,所以只要报警状态真的进去了,即使 Poll 一次都没跑也会长鸣——这是排查蜂鸣器问题时的有用事实。

`Buzzer_Start()/Buzzer_Stop()`:开关 TIM3 及其更新中断;Stop 时必须把 PA4 拉低,**停在高电平会让 16Ω 线圈持续跑约 300mA,不发声、白耗电、线圈发热**——"看起来没问题但摸上去烫手"的隐性故障。

`TIM3_IRQHandler` 每 5400 次执行一次,只做一件事:读回 ODR(输出寄存器,比读引脚电平 IDR 更快更可靠)并取反。

`Buzzer_Poll()`:节拍推进。**必须在 ≤100ms 的间隔里被调**(现挂在 SysTick 1ms 中断里,见 5.2)。

`Buzzer_BlockingBeep(ms)`:阻塞鸣叫,自检专用。不依赖主循环、不依赖状态机——**它是蜂鸣器故障的分界线**:这一声响 = 硬件/极性/TIM3/GPIO 全好,后面不响只能是逻辑问题;这一声不响 = 直接查硬件。

## 6.4 Int_Relay.h / Int_Relay.c —— 继电器

PA5 → R12(2k) → Q2 基极,Q2 集电极拉继电器线圈负端,高电平有效。R16(10k) 基极下拉保证 MCU 复位期间引脚浮空时不误吸合。全文就是 Init(配推挽输出+初始断开)+ On + Off 三个函数,无状态。

需求文档语义是"故障时触发继电器(如切断电源)",具体接常开还是常闭触点由现场接线决定。

## 6.5 Int_Rs485.h / Int_Rs485.c —— RS485

**收发芯片是 U2 = MAX13487,自动方向控制**,`Com_Board.h:117-118` 明确写着"MAX13487 内部自动收发切换,不需要 DE/RE 方向控制引脚,省一个 GPIO"。所以本工程**没有 DE 方向引脚、没有 PB12**,`Int_Rs485.c` 里也没有任何方向切换代码。

引脚(全部走 `Com_Board.h` 的 `RS485_*` 宏):

```c
#define RS485_UART      USART3
#define RS485_TX_PIN    GPIO_Pin_10   /* PB10 = USART3_TX */
#define RS485_RX_PIN    GPIO_Pin_11   /* PB11 = USART3_RX */
#define RS485_BAUD      9600

```

`Rs485_Init()`:PB10 复用推挽、PB11 浮空输入、9600-8-N-1、`USART_Cmd(USART3, ENABLE)`。因为芯片自动换向,初始化里**不需要配置任何方向控制 GPIO**。

发送函数(三个,逐层封装):

```c
void Rs485_SendByte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET) { }   /* 先等 DR 空 */
    USART_SendData(USART3, byte);                                       /* 再写入 */
}

void Rs485_SendString(const char *str)          /* 以 '\0' 结尾的字符串 */
{
    while (*str) { Rs485_SendByte((uint8_t)(*str++)); }
}

void Rs485_SendData(const uint8_t *data, uint16_t len)   /* 定长缓冲,JSON 帧走这个 */
{
    uint16_t i;
    for (i = 0; i < len; i++) { Rs485_SendByte(data[i]); }
}

```

- **等 TXE 不等 TC**:TXE 置位说明 DR 空了、可以塞下一个字节。因为 MAX13487 自己管方向,不存在"DE 拉低截断尾字节"的问题,所以不需要等 TC。这一点和常见的"手动 DE 的 485 必须等 TC"场景不同,别照搬那条经验。
- **注意与 `Com_Uart.c` 的顺序差异**:`Uart_SendByte` 是"先写 DR 再等 TXE",`Rs485_SendByte` 是"先等 TXE 再写 DR"。两者都正确——前者减少一次无谓等待,后者更直观;不要以为是笔误。
- 三个函数都是阻塞轮询,无中断、无 DMA。报警帧是几十字节 @9600bps,约几十毫秒,在主循环里同步发完可以接受。

**引脚冲突警告**:PB10/PB11 同时是 I2C2 的 SCL/SDA。阶段二 RS485 占用这两个脚,阶段三 SGM58031 要用 I2C2——**两者不能共存**,见 11.5。

## 6.6 Int_Oled.h / Int_Oled.c —— OLED(SSD1306 128×64,软件 I2C)

**引脚**:PB1=SCL、PB2=SDA,`GPIO_Mode_Out_OD` 开漏输出,依赖模块板载上拉。

**软件 I2C 时序**(`I2C_Start/Stop/WriteByte`):标准的位操作序列,SCL 低电平期间摆 SDA、SCL 高电平期间保持;ACK 相位释放 SDA 走完时钟(不检查从机应答——`OLED_SDA_READ` 宏定义了但未用,属于待改进项,见 11.6)。

**SSD1306 初始化序列**(`Oled_Init()`)26 条命令,关键几条:

| 命令        | 含义                      | 漏掉会怎样                                               |
| ----------- | ------------------------- | -------------------------------------------------------- |
| 0x8D, 0x14  | **开启内部电荷泵**        | **屏幕完全不亮**(最经典的"代码对但黑屏"原因)——本项目没漏 |
| 0xAF        | 开显示                    | 黑屏                                                     |
| 0xA8, 0x3F  | 复用比 1/64               | 只亮上半屏或乱                                           |
| 0xDA, 0x12  | COM 引脚配置(128×64 标配) | 行序错乱                                                 |
| 0x20, 0x10  | 页地址模式                | 刷新错位                                                 |
| 0xA1 / 0xC8 | 段重映射 / COM 扫描方向   | 镜像/上下颠倒                                            |
| 0x81, 0xFF  | 对比度                    | 太暗                                                     |

**显存与刷新**:`s_gram[8][128]`(8 页 × 128 列),所有绘图函数只写显存,`Oled_Refresh()` 一次性刷到屏幕。刷新时逐页:发 0xB0+page(页地址)、0x00/0x10(列地址低/高),然后**一整个 I2C 事务连续发 128 字节**(0x40 数据模式,SSD1306 列指针自动递增)。

> **2026-09-14 修复**:原来 `Oled_Refresh()` 每个字节单独发一个完整 I2C 事务(Start→地址→0x40→1字节→Stop),刷一屏 1024 个事务;而且曾有一版**每个字节连写两次**,导致列指针每轮走 2 格只填 1 格、右半屏是上一帧残留。修复后整页发送,事务数 1024→**8**,速度提升两个量级,右半屏残影消失。

**字库**:`Int_Oled.c` 内嵌 8×16 ASCII 字模表(0x20~0x7E,`FONT_INDEX(ch) = ch - 0x20` 索引),提供 `Oled_ShowChar/ShowString/ShowNum/ShowVoltage/ShowChinese`。中文 16×16 走 `Int_Font_CN` 查表(见 6.8)。

## 6.7 Int_Key.h / Int_Key.c —— 3 按键扫描(当前未启用)

**引脚**:UP=PA0、DOWN=PB0、OK=PA8,低电平有效(按下接地,内部上拉输入)。

三个时间常数:

```c
#define KEY_DEBOUNCE_TIME   20      // 去抖 20ms
#define KEY_LONG_PRESS_TIME 1000    // 长按 1 秒
#define KEY_REPEAT_TIME     200     // 按住不放,每 200ms 重复一次事件

```

状态机每键一个 `KeyState_t`(当前状态/上次状态/按压时长/重复计时/长按已触发/允许重复),`Key_Scan()` 周期调用(设计间隔 10ms),事件经 `Key_PostEvent()` 存入单槽变量(**只保留最新事件,未读的旧事件被覆盖**——菜单够用,但快速连按可能丢键)。

**两个已知问题**(未修):

- `press_time += 10` 把 10ms 调用间隔硬编码进了状态机,主循环调用间隔一变,长按/连发时间全不准。应改用 `Delay_GetTick()` 时间戳。
- 引脚 PA0/PB0 在原理图上的归属存疑(PA0 标注为未连接),且 PB0 可能与 OLED SCL 冲突——见 11.4。当前板子未焊按键,此文件未启用,风险未暴露。

## 6.8 Int_Font_CN.h / Int_Font_CN.c —— 16×16 中文字库

64 个常用汉字的点阵数据(每字 32 字节,共 2KB),UTF-8 编码查表:

```c
typedef struct {
    const char *utf8;     /* UTF-8 编码(3 字节) */
    const uint8_t *data;  /* 点阵数据(32 字节) */
} FontCN_t;

static const FontCN_t s_font_table[] = {
    {"\xE6\xB0\xB4", font_shui},    /* 水 */
    {"\xE6\xB5\xB8", font_jin},     /* 浸 */
    …
    {"\xE8\xBF\x94", font_fan},     /* 返 */
};
#define FONT_CN_COUNT (sizeof(s_font_table) / sizeof(s_font_table[0]))

const uint8_t* FontCN_GetData(const char *utf8)
{
    if (utf8 == NULL) return NULL;
    for (uint16_t i = 0; i < FONT_CN_COUNT; i++)
    {
        if (memcmp(utf8, s_font_table[i].utf8, 3) == 0)
            return s_font_table[i].data;
    }
    return NULL;   /* 未找到 */
}

```

查表用 `memcmp` 比 3 字节做线性扫描(64 项,不需要哈希/二分)。**用 `\xHH` 转义而非直接写汉字**是刻意的——绕开源文件编码在 armcc 下被 GBK 误解析的风险(同 7.1 的 fault_flags 问题)。

`Oled_ShowChinese()` 解析 UTF-8 字符串:遇高位字节(汉字)查表画 16×16(上 8 行填 page N 的 16 列、下 8 行填 page N+1),遇 ASCII 跳过(不显示),`p += 3` 前进一个汉字。

### 字库缺陷(2026-09-14 脚本逐字节核对)

用脚本解析全部 64 个点阵数组后按字节比对,结果:**6 组字节级完全相同**,共 15 个汉字塌缩成 6 个字形:

| 组   | 汉字                   | 点阵变量                                                 |
| ---- | ---------------------- | -------------------------------------------------------- |
| 1    | **断、离、阻、次、返** | `font_duan`、`font_li`、`font_zu`、`font_ci`、`font_fan` |
| 2    | 度、差                 | `font_du`、`font_cha2`                                   |
| 3    | 容、启                 | `font_rong`、`font_qi2`                                  |
| 4    | 点、隔                 | `font_dian2`、`font_ge`                                  |
| 5    | 幅、录                 | `font_fu`、`font_lu2`                                    |
| 6    | 史、存                 | `font_shi`、`font_cun`                                   |

另有 4 对**只差 1 字节**(视觉上几乎同形,同样可疑):`查` vs 组 1、`红` vs `继`、`定` vs `史`/`存`、`设` vs `记`。

第 1 组最严重——5 个字共用一个字形,而它们正好是"断线""距离""阻值""次数""返回"的用字,几乎覆盖了全部菜单和状态文案。

界面当前是英文,不影响使用;要启用中文界面需用 PCtoLCD2002 重新生成字模。**格式要求**:阴码、**逐列式**、纵向 8 点、高位在前——因为绘制代码是 `fontData[0..15]` 填 page N 的 16 列、`fontData[16..31]` 填 page N+1,是逐列布局。注意 `Int_Font_CN.h` 的头部注释写的是"逐行式",与代码实际的逐列布局矛盾,**以代码为准**,别被那句话带跑。

## 6.9 Int_Sgm58031.h / Int_Sgm58031.c —— 外置 16bit ADC(阶段三,当前未启用)

SGM58031(I2C 地址 0x48,ADDR 接地),16 位差分,内部 PGA 1x/2x/4x/8x(对应满量程 ±6.144/±4.096/±2.048/±1.024V)。

- 硬件 I2C2(PB10/PB11)100kHz,标准外设库 I2C 事件等待(`I2C2_WaitEvent` 带 10000 次超时);
- `Sgm58031_ReadDiff(pga)`:构造配置寄存器(OS 单次启动位 | MUX=AIN0-AIN1 差分 | PGA | 单次模式 | 128SPS | 禁比较器)→ 写入启动转换 → `Delay_ms(10)` 等完成 → 读转换寄存器(先发寄存器地址、重复起始、读两字节,高在前);
- `Sgm58031_AdcToMv(adc, pga)`:`(int32_t)adc × full_scale_mv / 32768`,纯整数运算。

**启用方式**:`App_Measure.c` 顶部 `#define USE_EXTERNAL_ADC 0` 改 1。当前保持 0(用内部 ADC,SGM58031 模块未接线)。

**硬约束**:I2C2 与 RS485 的 USART3 共用 PB10/PB11,两者互斥(见 11.5)。

---

# 7. App 层逐文件详解

## 7.1 App_Measure.h / App_Measure.c —— 采集与判定(核心)

### 数据结构

```c
typedef struct {
    int32_t v1;  /* 黑线电压,断线监测1 */   … int32_t v10; /* 黑线电压,水浸定位2 */
} MeasVolt_t;    /* 全部 int32_t 毫伏,无浮点 */

typedef struct {
    uint8_t  break_detected;  /* 1 = 断线 */
    uint8_t  leak_detected;   /* 1 = 水浸 */
    uint8_t  pos_valid;       /* 1 = 定位有效 */
    int32_t  pos_mm;          /* 定位距离,毫米 */
    const char *break_line;   /* 断线线路名(调试打印用) */
    uint8_t  fault_flags;     /* FaultFlag_t 组合(YEL/RED),显示层用,
                                 避免比较中文字符串 */
} MeasResult_t;

typedef enum {
    FAULT_NONE      = 0,
    FAULT_BREAK_RED = 0x01,   /* V3 != V4,红线回路断 */
    FAULT_BREAK_YEL = 0x02,   /* V1 != V2,黄线回路断 */
} FaultFlag_t;

```

`fault_flags` 是 2026-09-13 加的:显示层原来拿 `strstr(break_line, "黄")` 判断哪条线断,UTF-8 源文件 + armcc GBK 解析会把中文截断,直接编译失败。改成数值标志后彻底绕开编码问题。

### 可配置阈值与默认值(App_Measure.h)

| 宏                  | 默认值     | 含义                           | 标定状态                                 |
| ------------------- | ---------- | ------------------------------ | ---------------------------------------- |
| `MEAS_EQUAL_TOL_MV` | 50         | "V1==V2"容差                   | 待标定(实测偏紧,湿纸巾测试误报 YEL 断线) |
| `MEAS_ZERO_MV`      | (见头文件) | 零点门限(V5≈0 / V1≈0 判断)     | 待标定                                   |
| `MEAS_FULL_MV`      | (见头文件) | 满幅门限(V6≈3.3V 判断)         | 待标定                                   |
| `MEAS_R_SAMPLE_OHM` | 4000       | 公式常数(原理图 R11=3.9k 取整) | 待验证 4000 vs 3900                      |
| `MEAS_DEFAULT_R`    | 0.1 Ω/m    | 线缆单位电阻默认值             | **未标定**                               |
| `MEAS_DEFAULT_L`    | 100 m      | 线缆长度默认值                 | **未标定**                               |

### 采集函数(与需求文档逐行对应)

```c
void Measure_ReadBreak(MeasVolt_t *v)      /* 采 V1~V4 */
{
    /* 断线1:4052 通道0 → ADC0=黄(V2) ADC1=黑(V1) */
    Wire_Apply(1, 1, 0, 0, WIRE_PATH_YELLOW_BLACK);
    v->v2 = Meas_ReadNet0Mv();
    v->v1 = Meas_ReadNet1Mv();

    /* 断线2:4052 通道2 → ADC0=红(V4) ADC1=绿(V3) */
    Wire_Apply(1, 1, 0, 0, WIRE_PATH_RED_GREEN);
    v->v4 = Meas_ReadNet0Mv();
    v->v3 = Meas_ReadNet1Mv();
}

```

注意断线1/断线2 的 4066 开关状态完全相同(红黄都供电、绿黑都不接取样),只有 4052 通道不同——理论上可只 Apply 一次、切通道再采,省 15ms。**刻意不优化**,理由写在注释里:让代码和文档逐行对应,排错时直接比对;等阶段一验证通过、要提扫描速度时再合并。

`Measure_ReadLeak()` / `Measure_ReadLocate()` 同构,对应需求文档水浸监测和定位1/2a/2b 三行。`Measure_ReadAll()` 顺序调三个分项后 `Wire_AllOff()`(线缆回静止态,防电极长期通电腐蚀),总耗时 ≈ 6×15ms + 12×9×21µs ≈ **92ms**。

### 码值转毫伏(纯整数)

```c
static int32_t Adc_RawToMillivolt(uint16_t raw)
{
    return (int32_t)(((uint32_t)raw * (uint32_t)MEAS_VCC_MV + 2047U) / 4095U);
}

```

- 先乘后除:4095×3300 = 13,513,500,`uint32_t` 装得下,不溢出;
- **+2047 是四舍五入**(半个除数),不加会系统性偏低半个 LSB;
- 全程无 float:F103 无硬件浮点,且 MicroLIB printf 不支持 %f。

### 阶段二/三切换

```c
#ifndef USE_EXTERNAL_ADC
#define USE_EXTERNAL_ADC    0   /* 0=内部ADC(阶段二), 1=SGM58031(阶段三) */
#endif

```

`Meas_ReadNet0Mv()/ReadNet1Mv()` 内部 `#if` 切换两套读取路径;`Measure_Init()` 同样二选一初始化 `Adc_Init()` 或 `Sgm58031_Init()`。

### 判定函数

`Measure_CheckBreak()` / `Measure_CheckLeak()` 见 2.4 节分析。`Measure_Judge()` 是阶段二入口:清空结果 → CheckBreak(记 flags+line)→ CheckLeak → 若水浸则 CalcL1(记 pos_valid/pos_mm)。

## 7.2 App_State.h / App_State.c —— 状态机

### 状态与参数

```c
typedef enum {
    STATE_NORMAL = 0, STATE_BREAK_ALARM, STATE_LEAK_ALARM
} SystemState_t;

#define STATE_DEBOUNCE_COUNT    3    /* 进故障需连续 3 次一致 */
#define STATE_RECOVER_COUNT     5    /* 恢复需连续 5 次(更严格,防抖动反复) */
#define STATE_SCAN_INTERVAL_MS  500U /* State_Poll 自采模式下的节拍 */

```

### 两个入口

- `State_Poll()`:自带节拍控制(500ms)和采集,适合无显示的裸跑主循环;
- `State_Update(const MeasResult_t *result)`:接受外部已采好的结果。**主循环自己采集时必须用这个**——用 Poll 会导致每轮重复采集(多花 92ms)。2026-09-14 为配合 OLED 主程序拆出来的。

### 状态迁移(2026-09-14 修正版)

```
STATE_NORMAL:
    if (leak_detected)   → 去抖满 3 次 → EnterLeakAlarm(pos)
    else if (break_detected) → 去抖满 3 次 → EnterBreakAlarm(line)
    else → 去抖清零

STATE_BREAK_ALARM: !break 持续 5 次 → EnterNormal
STATE_LEAK_ALARM:  !leak  持续 5 次 → EnterNormal
                    持续期间 pos_valid 且位置变化 >100mm → 更新定位并重发 RS485

```

三个 2026-09-14 修正(都有实测证据支撑,详见第 10 节):

1. **水浸优先于断线**。原版 break 在前——未标定时 EQUAL_TOL 偏紧容易误判断线,一旦进了 BREAK_ALARM 整个报警期**完全不看水浸**,真实进水会被无声吞掉。进水比断线危险,优先级必须反过来。
2. **报警不再要求 pos_valid**。原版 `if (cnt >= N && result.pos_valid)` —— 未标定 r/L 时 pos_valid 恒为 0,导致"泡在水里但蜂鸣器一声不响"(实测复现)。现在定位算不出只影响显示/上报内容,不影响该不该报警。
3. **去抖计数饱和**:`DEBOUNCE_INC()` 限制在 250 以内。原版 `uint8_t` 无限自增,加到 255 回绕到 0,每 255 轮凭空出现一个 3 轮报警窗口。

### 报警联动

`EnterLeakAlarm()`:置状态 → `Buzzer_SetPattern(BUZZER_PATTERN_LEAK)` → `Relay_On()` → printf 定位 → RS485 发 `{"state":"leak","pos_m":x.xxx}`。`EnterBreakAlarm()`/`EnterNormal()` 同构。RS485 报警期间每 10 秒重发状态帧。

## 7.3 App_Config.h / App_Config.c —— 参数与 Flash 存储

### Flash 布局

```c
#define CONFIG_FLASH_ADDR   0x0800FC00U   /* 64KB 芯片的第 63 页(最后一页) */
#define CONFIG_MAGIC        0x57544430U   /* "WTD0" 水浸检测 v0 */

```

放最后一页的理由:程序从 0x08000000 向上长,参数放离程序最远的页,程序涨到 63KB 之前不会被覆盖(当前程序约 14KB)。

### Config_t 结构(App_Config.h)

```c
typedef struct {
    uint32_t magic;               /* 魔数,识别"这页被正确写过" */
    int32_t  r_mOhm_per_m;        /* 线缆电阻,毫欧/米(整数,避免浮点) */
    int32_t  length_mm;           /* 线缆长度,毫米 */
    int32_t  equal_tol_mv;        /* 相等容差 */
    int32_t  zero_mv;             /* 零点门限 */
    int32_t  full_mv;             /* 满幅门限 */
    uint8_t  buzzer_enable;
    uint8_t  relay_enable;
    uint16_t sampling_interval_ms; /* 采样间隔 50~1000 */
    uint8_t  debounce_count;      /* 去抖次数 1~10 */
    uint8_t  reserved[3];         /* 对齐留白 */
    uint16_t crc16;               /* 覆盖前面所有字段的校验 */
} Config_t;

```

### 三重防错机制

1. **magic**:全新芯片 Flash 全 0xFF,光靠 CRC 可能"碰巧通过";magic 不对说明这页从没被正确写过,整体回退默认值。**宁可丢参数也不用错参数**——擦写掉电留下的半新半旧数据可能让容差变成荒谬值(比如 30000mV,永远判不出断线)。
2. **范围校验**(`Config_Validate`):每个字段检查合理区间(r、L 为正,间隔 50~1000,去抖 1~10 等)。
3. **CRC16-CCITT**(多项式 0x1021,初值 0xFFFF):软件实现,30 来个字节比 STM32 硬件 CRC(固定多项式的 CRC32)省事,也便于上位机复算。

### 读写流程

- `Config_Init()`:Load 失败(校验不过)→ 置默认值;然后 `Measure_SetCableParam()` 把 r/L 同步给测量层。
- `Config_Save()`:重算 CRC → 再校验一遍(防改坏)→ `FLASH_Unlock` → `FLASH_ErasePage`(整页擦)→ **按半字(16bit)** `FLASH_ProgramHalfWord` 逐个写入 → `FLASH_Lock`。任一步失败立即锁回并打印失败地址。
- ⚠ 写 Flash 期间 CPU 停取指几十毫毫秒(同 bank 擦写阻塞总线),不要在报警节拍中间存参数。

## 7.4 App_Display.h / App_Display.c —— OLED 业务显示

4 行 16 列布局(8×16 字体):

```
行0: 状态      "OK: Normal" / "ALARM: Break" / "ALARM: Leak!"
行1: 定位/详情 "Pos:12.345m" / "Pos:-- calib r/L" / "Break: YEL" / "No fault"
行2-3: 电压    两页轮换: V1-V4 页 / V5,V6,V9,VA(V10)页

```

关键实现细节(均为 2026-09-13/14 修正后的版本):

```c
static void FmtMv(char *dst, uint32_t cap, int32_t mv)
{
    int32_t neg = (mv < 0);
    int32_t a   = neg ? -mv : mv;
    snprintf(dst, cap, "%s%d.%02d", neg ? "-" : "",
             (int)(a / 1000), (int)((a % 1000) / 10));
}

```

这个辅助函数解决两个真实 bug:

1. **负数**:原版 `%d.%02d` 直接拆,传 −500mV 算出整数 0、小数 −50,拼出 `0.-50`——负号丢了格式也坏了。ADC 通道失配时 v1~v10 出负值是正常的,不是异常输入。
2. **宽度**:原版 `"V1:%d.%02d V2:%d.%02d"` 展开最多 18 列,超出一行 16 列,snprintf 截尾后 **V2 的值一直显示不全**。先各自格式化成短串再拼,总宽可控(V10 缩写 VA 省一列)。

`Display_UpdateAll()`:行0 状态;行1 按"水浸→定位(无效则提示标定 r/L)/断线→按 fault_flags 显示 YEL/RED/BOTH/无故障→No fault"的优先级;行2-3 电压页码 `s_voltage_page` 自动轮换。

**断线详情不用中文字符串比较**(见 7.1 的 fault_flags 说明),`strstr(break_line,"黄")` 那版直接编译失败,已删。

## 7.5 App_Menu.h / App_Menu.c —— 按键菜单(当前未启用)

六态状态机:

```c
typedef enum {
    MENU_STATE_MONITOR = 0,   /* 监测主界面,长按 OK 进菜单 */
    MENU_STATE_MAIN,          /* 主菜单:查看参数/修改参数/系统设置/返回 */
    MENU_STATE_VIEW_PARAM,    /* 查看参数 */
    MENU_STATE_EDIT_PARAM,    /* 修改参数(r/L/容差/零点/满幅/保存) */
    MENU_STATE_SYSTEM_SET,    /* 系统设置(蜂鸣器/继电器/采样间隔/去抖/历史/返回) */
    MENU_STATE_VIEW_HISTORY,  /* 历史记录浏览 */
} MenuState_e;

```

历史记录:RAM 循环缓冲区,10 条 `HistoryRecord_t`(类型/时间戳/位置/16 字节详情),`Menu_AddHistory()` 头指针循环覆盖,UP/DOWN 翻页浏览。**掉电即失**(未存 Flash)——记录的是报警事件,重启后丢历史属已知取舍。

编辑参数用临时缓存(`s_edit_*`),J-long 长按取消不保存;系统设置里的采样间隔/去抖次数是**实时保存**(每次 UP/DOWN 都 Config_Save)——Flash 擦写寿命 1 万次量级,长期这样用会磨掉那一页,菜单版启用前建议改成"退出时统一保存"。

**当前板子没焊按键,此模块未参与编译验证之外的实测**,且有两个遗留问题:SYS_MENU 的采样/去抖菜单项保存逻辑曾修复过一处插入错位的 else-if;按键事件是单槽覆盖式(见 6.7)。

---

# 8. User 层:五个主程序

| 文件               | 阶段         | 内容                                                      | 状态                                            |
| ------------------ | ------------ | --------------------------------------------------------- | ----------------------------------------------- |
| `main.c`           | 阶段一       | 循环采 V1~V10,串口打全表 + 判定 + 定位;开机蜂鸣自检 100ms | 实测通过                                        |
| `main_stage2.c`    | 阶段二(无屏) | 状态机 + RS485 + 蜂鸣器/继电器,无 OLED                    | 未实测                                          |
| `main_unified.c`   | 一/二合一    | `#define STAGE_TWO` 编译期切换                            | 未实测                                          |
| `main_with_oled.c` | 阶段二(带屏) | **当前烧录版本**                                          | 2026-09-14 实测:采集/判定/报警联动/屏幕全部工作 |
| `main_with_menu.c` | 阶段二(完整) | OLED + 3 键菜单                                           | 未实测(无按键)                                  |

⚠ **Keil 工程同一时刻只能挂一个 main**(两个 main 会链接期重定义)。当前工程挂的是 `main_with_oled.c`,其余四个文件留在目录里备查、不参与编译。

## 8.1 main.c —— 阶段一原理验证

初始化:`Board_Init → Delay_Init → Uart_Init → Measure_Init → Buzzer_Init`(注意没有 Config_Init、没有 OLED、没有继电器)。

主循环:每轮打印轮次和时间戳 → `Measure_ReadAll()` → `Measure_PrintVolt()`(V1~V10 全表,mV 和 V 两种形式并列,便于和万用表逐位核对)→ `Measure_PrintResult()`(断线/水浸判定 + 定位)→ `Delay_ms(1000)`。一轮约 1.1 秒,便于人眼跟读串口。

`PrintBanner()` 打印四个待标定量的当前值(r、L、`MEAS_R_SAMPLE_OHM`、三个判定阈值、`WIRE_SETTLE_MS`),并在标题写"★标定前结果无意义"——这正是阶段一要回答的四个问题。

小数位打印用了个技巧:`(int)((Measure_GetR() - (float)(int)Measure_GetR()) * 1000.0f)`,即取整数部分做差再乘 1000。**能用浮点运算,只是不能用 printf 的 `%f`**(MicroLIB 限制)。

## 8.2 main_stage2.c —— 阶段二无屏版

初始化:`Board_Init → Delay_Init → Uart_Init → Buzzer_Init → Relay_Init → Rs485_Init → Measure_Init → State_Init`。

主循环极简:

```c
while (1)
{
    State_Poll();     /* 自带 500ms 节拍;未到点则立即返回,相当于空转 */
    Buzzer_Poll();    /* 节拍推进 */
}

```

三点注意:

1. **没调 `Config_Init()`** —— 所以 r/L 用的是 `App_Measure.c` 里的 `MEAS_DEFAULT_R/L` 默认值,**不读 Flash 参数**。对照 `main_unified.c` 的阶段二分支(调了 `Config_Init()`),两者不一致。要让无屏版用上 Flash 参数,得补这一行。
2. **`State_Poll()` 内部自己采集**(`Measure_ReadAll` + `Measure_Judge`),所以主循环没有重复采集的问题。但它同时意味着这个版本**无法把测量结果交给别的模块**——要显示就得改造成 `State_Update()`。
3. **`Buzzer_Poll()` 是冗余调用**。节拍推进现已挂在 `SysTick_Handler`(见 5.2),这里再调一次不影响正确性(同一时间戳下的判断是幂等的),但属于历史遗留。同理适用 `main_unified.c`。

## 8.3 main_unified.c —— 编译期二选一

用 `#if STAGE_TWO` 把两套 main 包在同一个文件里,靠 Keil 的 Preprocessor Symbols 加 `STAGE_TWO` 宏切换(默认 0 = 阶段一)。两个分支的代码分别等价于 `main.c` 和 `main_stage2.c`,差别只有一处:阶段二分支**多调了 `Config_Init()`**。

这个文件的价值是"一份代码两个阶段",代价是两个分支都无法单独编译验证(改动一个分支时另一个分支的语法错误只在切换到它时才暴露)。实际开发中更常用的是直接换工程里挂的 main 文件。

## 8.4 main_with_oled.c —— 当前烧录版本

初始化顺序(完整,含 Flash 参数与屏幕):

```c
Board_Init();   /* 必须最先:关 JTAG,否则 PA15/PB3/PB4 全是死脚 */
Delay_Init();   /* SysTick 时基,Wire_Apply 和 OLED 都依赖 */
Uart_Init();
Buzzer_Init(); Relay_Init(); Rs485_Init();
Config_Init();  /* 从 Flash 读参数,内部会 Measure_SetCableParam 同步 r/L */
Measure_Init(); /* 内含 Wire_Init + Adc_Init */
State_Init();
Display_Init(); /* 内含 Oled_Init,显示开机画面 */

```

主循环:

```c
while (1)
{
    Measure_ReadAll(&g_volt);        /* ~92ms,六组开关切换 */
    Measure_Judge(&g_volt, &g_result);
    State_Update(&g_result);          /* 不用 State_Poll,避免重复采集 */
    /* Buzzer_Poll() 已挂在 SysTick,这里刻意不调 */

    if (Delay_GetTick() - tick_last_display >= 500)
    {
        tick_last_display = Delay_GetTick();
        Display_UpdateAll(&g_result, &g_volt, st);
        Display_Refresh();
        printf("[%s] ...", 状态 + 断线 flags + 定位);   /* 英文,见编码约定 */
    }
    Delay_ms(100);
}

```

- **用 `State_Update()` 而非 `State_Poll()`**:后者会自己再采一遍 V1~V10,每轮多花 92ms。
- **串口输出用英文**:源文件 UTF-8 + armcc GBK 解析会截断中文字面量导致编译错误,串口助手按 GBK 解码也会乱码。调试信息是给开发者看的,英文躲开两个问题;OLED 上的中文走字库,与之无关。
- **断线详情用 `fault_flags` 数值位判断**,不用 `strstr(break_line,"黄")`——后者曾直接导致编译失败(#8 missing closing quote)。
- **定位无效时打印 `---(need r/L calibration)`**:未标定 r/L 时 `pos_valid` 恒为 0,但**报警照常给**(见 7.2 修正 2)。

开机横幅打印从 Flash 读到的参数——**这是验证掉电保存最省事的方法**:改参数 → 重启 → 横幅应显示新值,不用接串口调试器逐项确认。随后 `Buzzer_BlockingBeep(100)` 上电自检:**这一声是蜂鸣器的分界线**,响=硬件/极性/TIM3/GPIO 全好,不响=直接查硬件(先查 R9,见 11.3),别在软件里找。

## 8.5 main_with_menu.c —— 完整版

与 8.4 的差别:多初始化 `Int_Key`(`Key_Init`)和 `App_Menu`(`Menu_Init`),主循环里每 10ms `Key_Scan()`、调 `Menu_Process()`/`Menu_Display()`,并且**只在 `Menu_IsMonitoring()` 为真时才采集和刷监测界面**——进菜单后暂停测量,避免刷屏和菜单抢显示。

因为板子未焊按键,此版本未实测(见 11.2)。

---

# 9. 编译配置

- 工具链:Keil MDK,armcc V5.06 update 7(build 960)
- 预定义宏:`USE_STDPERIPH_DRIVER, STM32F10X_MD`(中容量)
- **必须勾选 Use MicroLIB**(printf→fputc 重定向的前提)
- 优化级别:当前 O0(最低档)。⚠ `Int_Oled.c` 的软件 I2C 延时若依赖空循环计数,调高优化级别可能被删循环——目前 I2C 位延时走 `Delay_us`(volatile 计数)路径,风险较低,但仍建议升级优化后回归测试 OLED
- 命令行编译:`UV4.exe -b project.uvprojx -j0 -t "Target 1" -o build.log`
- 典型体积:Code≈14.4KB / RO≈1.8KB / RW≈88B / ZI≈2.1KB(含 2KB 字库和显存),64KB Flash 余量充足

---

# 10. 开发过程修复记录

按发现时间排序,均有实测或编译证据:

| #    | 问题                                | 根因                                                         | 修复                                        |
| ---- | ----------------------------------- | ------------------------------------------------------------ | ------------------------------------------- |
| 1    | OLED 完全不亮、蜂鸣器不响(串口正常) | Keil 工程没把 Int_Oled/Int_Key/Int_Font_CN/Int_Sgm58031/App_Display/App_Menu 六个文件加进编译;且工程挂的是阶段一 main.c | 六文件入工程;User 组换成 main_with_oled.c   |
| 2    | App_Display.c 编译 19 错            | 类型名 `SystemState_e`(实际 `SystemState_t`);字段 `V1..V9`(实际小写 `v1..v9`);中文 `strstr("黄")` 在 armcc GBK 下截断引号 | 全部改名;显示改用 fault_flags 数值判断      |
| 3    | App_Menu.c 编译 3 错                | 误插入的悬空 `else if`;`MENU_STATE_HISTORY` 与枚举 `MENU_STATE_VIEW_HISTORY` 不一致;隐式声明 `Delay_GetTick` | 修语法、统一枚举名、补 include              |
| 4    | 水浸了蜂鸣器不响(串口能看到 leak)   | 状态机要求 `pos_valid` 才进报警;未标定 r/L 时 pos_valid 恒 0 | 报警与定位解耦(7.2 修正 2)                  |
| 5    | 蜂鸣器报警"响"但听不见              | Buzzer_Poll 在主循环调,循环一轮 682ms 追不上 200ms 节拍      | Poll 挂到 SysTick 1ms 中断(5.2)             |
| 6    | 串口打出 `定位: 0.-01 m`            | 无效定位传 −1 哨兵值,`%d.%03d` 拆负数格式坏                  | 显示层 FmtMv 取绝对值+负号;状态层传有效标志 |
| 7    | 屏幕电压行 V2/V4 显示不全           | 格式化串 18 列 > 屏幕 16 列,snprintf 截尾                    | FmtMv 短串拼接(7.4)                         |
| 8    | 屏幕右半边残影                      | Oled_Refresh 每字节写两次,列指针走 2 格填 1 格               | 删重复行                                    |
| 9    | 刷屏极慢                            | 每字节一个完整 I2C 事务(1024/屏)                             | 整页发送,8 事务/屏                          |
| 10   | 断线报警屏蔽水浸                    | NORMAL 分支 break 优先                                       | 水浸优先(7.2 修正 1)                        |
| 11   | 去抖计数回绕风险                    | uint8_t 无限自增                                             | DEBOUNCE_INC 饱和(7.2 修正 3)               |

---

# 11. 已知问题与未验证项

## 11.1 定位功能未标定(核心缺口)

`Measure_CalcL1()` 要求结果落在 `0 ≤ L1 ≤ L`。代入默认 r=0.1、L=100,该条件等价于两个**几千量级**的数之差落在 ±10 窗口内——实际永不成立,pos_valid 恒 0。

**解法只有实测标定**(见第 12 节),代码无解。当前固件的行为是:水浸照常报警(蜂鸣器/继电器/RS485/屏幕),定位显示 `Pos:-- calib r/L`。

## 11.2 菜单与掉电保存未实测

板子没焊按键(PA0/PB0/PA8 空接),菜单进不去,参数只能靠默认值或改代码。Flash 读写在 Config_Init 有覆盖(横幅打印证实读取路径工作),但"改参数→掉电→读回"的完整链路未验证。

## 11.3 蜂鸣器声音极小(硬件,未修)

现象:报警节奏正确(响-停循环),但音量极小。推测根因:原理图上 BUZZER1 正端经 R9(10kΩ)上拉到 5V——10k 串在 16Ω 线圈供电路径上,线圈只分到约 8mV(应为 3.6Vo-p/95mA 才有 80dB)。

**此推断来自 Int_Buzzer.h 注释,未经 Altium 核实**(注释原文即要求"请在 Altium 里核对 R9 到底串在哪条支路")。

验证:让蜂鸣器长鸣,万用表直流档量蜂鸣器两引脚电压——几十 mV 即证实 R9 在供电路径上。修法:短接 R9(5Vo-p 超压 0.5V,短促报警可接受)或换成 10~15Ω(压到额定 3.6V 附近,电流约 160~190mA)。

## 11.4 按键引脚归属存疑(未启用,风险未暴露)

`Int_Key.c` 用 PA0(UP)/PB0(DOWN)/PA8(OK),但这三个脚在 `Com_Board.h` 引脚字典里**没有登记**,且 Com_Board.h 注明 PA0 在原理图上是打叉未连接。另有项目记忆记录"OLED SCL 在 PB0"(与当前代码 PB1 不符,相差一格)。启用菜单前必须:① Altium 核对三个按键脚位;② 万用表量 OLED SCL 焊盘通到 MCU pin18(PB0)还是 pin19(PB1)——若真是 PB0,`Key_Init()` 会把 SCL 改成上拉输入,直接把屏幕搞哑。

## 11.5 RS485 与 I2C2 引脚互斥

USART3(PB10/PB11)和 I2C2(SGM58031)共用两个脚。阶段二(RS485)与阶段三(外置 ADC)**不能同时启用**。当前固件 USE_EXTERNAL_ADC=0 走 RS485。将来切阶段三时要牺牲 485 或改用其他串口/软件方案。

## 11.6 其他待办

- I2C 不检查 ACK(`OLED_SDA_READ` 定义未用):地址错/屏没插/上拉缺失全是同一个症状"函数正常返回但黑屏"。建议 WriteByte 返回 ACK 位、Init 时检查并 printf 报错。
- Int_Font_CN 字形重复:6 组共 15 个汉字塌缩成 6 个字形(见 6.8 的逐字节核对结果),启用中文界面前必须重做字模。
- I2C 不检查 ACK(`OLED_SDA_READ` 定义未用):地址错/屏没插/上拉缺失全是同一个症状"函数正常返回但黑屏"。建议 WriteByte 返回 ACK 位、Init 时检查并 printf 报错。
- `main_stage2.c` 未调 `Config_Init()`,用的是默认 r/L 而非 Flash 参数;`main_unified.c` 的阶段二分支调了。两版不一致(见 8.2)。
- `main_stage2.c` / `main_unified.c` 主循环里的 `Buzzer_Poll()` 已成为冗余调用(节拍推进现在挂在 SysTick,见 5.2)。幂等无害,清理时注意别把 SysTick 里那个也删了。
- `HardFault_Handler` 是标准库默认的 `while(1)` 死循环。现场遇到"运行中突然不动、串口无输出"先怀疑它;建议改成打印现场或记录后复位(见 5.4)。
- Int_Key 的 10ms 硬编码(见 6.7)。
- 断线容差 50mV 偏紧:湿纸巾测试同时误报 YEL 断线,需实测标定。
- 蜂鸣器 TIM3 中断优先级设 2 但工程未调 `NVIC_PriorityGroupConfig`(默认 Group 0 下抢占位为 0,该优先级实际被忽略——不影响功能,注释意图未生效)。
- App_Menu 系统设置项每次按键都 Config_Save,Flash 擦写寿命有限,启用菜单版前改成退出统一保存。

---

# 12. 标定指南

阶段一遗留的四个标定项,全部需要实测:

## 12.1 线缆电阻 r(最重要,定位的前提)

1. 取一段已知长度 L₀ 的检测线缆;
2. 远端短接红+绿(构成回路);
3. 万用表量控制器端红线—绿线直流电阻 R₀(去回两趟)→ `r = R₀ / (2L₀)` Ω/m;
4. 写入:改 `MEAS_DEFAULT_R`(源码)或标定后经菜单设置(需焊按键)。

不知道整卷线长度时的降级方案:量任意两根线两端的回路电阻得到 `r×L` 乘积,可让公式里的 rL 项先正确,pos_valid 有机会翻 1(精度打折)。

### 参考值:13 Ω/m(来自线缆产品规格，需实测核对)

`Resource\漏水检测资料\定位测漏检测线.docx`(九纯健 JCJLSXLDZW 定位漏水感应线产品说明书)的技术参数表:

| 参数         | 值                                  |
| ------------ | ----------------------------------- |
| **线缆电阻** | **13 Ω/m**(误差 0.5% 内)            |
| 线芯数量     | 4(对应红黄绿黑)                     |
| 产品尺寸     | φ6mm                                |
| 常用规格     | 7.5M/10M/15M/20M(可定制其他米数)    |
| 报警泄漏量   | 沿线任何位置最大 30mm               |
| 检测内容     | 水、去离子水+药剂冷却液、65% 乙二醇 |

⚠ **这是资料夹里参考产品的参数,未必等于你手上那根线缆的实际值** —— 务必按上面 4 步自己量一遍。但它量级上说得通:13Ω/m × 100m = 1300Ω,与取样电阻 R11 = 4000Ω 可比,分压才随位置变化;而代码默认的 `MEAS_DEFAULT_R = 0.1f` 对应 100m 仅 10Ω,相对 4000Ω 近乎短路,**电压根本不随漏水位置变,定位无解**。所以默认值差 130 倍这件事,是定位功能失效的根因之一。

### 关于"7.5M 是不是最低长度"

**不是技术要求的下限,只是厂商目录里最小的标准规格**(原文:"7.5M/10M/15M/20M(可定制其他米数)")。两份需求文档都没有规定最低长度。

但**越短越难定位**,原因是代码的接受判据:

```c
/* App_Measure.c: Measure_CalcL1() 末段 */
if ((l1_m < 0.0f) || (l1_m > s_cable_len_m)) { return 0U; }

```

展开后等价于 `-r·L ≤ term1 - term2 ≤ r·L`。**L 越小窗口越窄**,而 `term1`/`term2` 是几千量级 —— 窗口窄到一定程度就恒不成立,`pos_valid` 永远是 0,屏幕一直显示"需要标定"。

量级估算(按 r = 13Ω/m、内部 12bit ADC 0.806mV/LSB、噪声 2~3 LSB):

| 缆长  | 总缆阻 | 占 R11 比例 | 随位置变化的电压 | 位置动态范围                        |
| ----- | ------ | ----------- | ---------------- | ----------------------------------- |
| 40 cm | 5.2 Ω  | 0.13%       | ≈ 4 mV           | **≈ 5 LSB,淹没在噪声里,定位不可行** |
| 7.5 m | 97 Ω   | 2.4%        | ≈ 80 mV          | ≈ 100 LSB                           |
| 50 m  | 650 Ω  | 14%         | ≈ 460 mV         | ≈ 570 LSB                           |

**40cm 级别只做"有/无报警"是可行的**(水浸与断线都是阈值判断,不需要分辨率),做定位不可行。要让短缆能定位,得把 R11 从 3.9kΩ 换到几十欧姆量级,但回路电流会从约 0.8mA 涨到几十 mA —— 属于硬件重新设计,不是调参。

## 12.2 相等容差

干燥状态下连续观察串口 V1−V2、V3−V4 的波动幅度,取最大波动的 2~3 倍作为 `MEAS_EQUAL_TOL_MV`(当前 50mV 实测偏紧)。

## 12.3 零点/满幅门限

观察干燥时 V5(应≈0)和 V6(应≈3300mV)的实际读数:零点门限 = V5 实测 + 余量;满幅门限 = V6 实测 − 余量。余量取 100~300mV。

## 12.4 开关稳定时间

示波器(或对比法)测 Wire_Apply 后电压进入 ±1% 的时间;当前 15ms,若实测 5ms 即稳定,主循环可提速约 60ms/轮。

## 12.5 公式常数 4000 vs 3900

r、L 标定完成后,在已知位置(如线缆中点)做水浸,对比 L1 计算值与实际值;若系统偏差,尝试把 `MEAS_R_SAMPLE_OHM` 改 3900 重算。

---

## 附:实测数据样例(2026-09-14,未标定状态)

```
[OK]                      ← 干燥,正常轮询
[LEAK] break: YEL  leak @ ---(need r/L calibration)
[报警] 水浸定位: 0.-01 m  ← 湿纸巾捂线:水浸判定正确;
                            "break: YEL" 为容差偏紧的误报(11.6);
                            "0.-01" 为负数格式 bug,已修(10 #6)

```

修复后预期:`[LEAK] leak @ ---(need r/L calibration)` + 蜂鸣器 200/200ms 急促节拍 + 屏幕 `ALARM: Leak! / Pos:-- calib r/L`。

---

# 13. 项目开发流程图

按实际开发顺序排列。本项目的经验是:**顺序不能跳**,尤其 ③ 和 ④ —— 跳过标定直接上正式程序,定位功能永远算不出结果,而且会误以为是代码 bug。

## 13.1 总览

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│ ①需求与硬件  │ → │ ②工程搭建    │ → │ ③阶段一      │ → │ ④标定        │ → │ ⑤阶段二/三   │
│   分析       │   │   骨架       │   │   原理验证   │   │   (卡在这)   │   │   正式运行   │
└─────────────┘   └─────────────┘   └─────────────┘   └─────────────┘   └─────────────┘

```

## 13.2 ① 需求与硬件分析

```
        开始
         │
         ▼
┌────────────────────────────┐
│ 读需求文档                  │
│ · 控制引脚表(PB3~PB7,PA15) │
│ · 六组开关组合 → V1~V10    │
│ · 断线/水浸判定判据         │
│ · 定位公式 L1              │
└────────────┬───────────────┘
             ▼
┌────────────────────────────┐
│ 读原理图                    │
│ · 引脚 → 网络映射           │──┐
│ · 4066/4052 拓扑           │  │ 发现矛盾?
│ · 取样电阻 R11、保护电阻    │  │ (如 OLED 引脚归属)
└────────────┬───────────────┘  │ → 万用表量通断定死
             ▼                  │   ★别靠读 PDF 猜
┌────────────────────────────┐  │
│ 关键决策定案                │←─┘
│ · ADC 路径:内部 12bit 起步  │
│ · RS485 芯片:MAX13487 自动方向│
│ · 调试口:UART1 代替 485     │
└────────────┬───────────────┘
             ▼
        进入 ②

```

## 13.3 ② 工程搭建(Com → Int → App，自底向上)

```
┌─────────────────────────────────────────────────────┐
│ Com 层(最先,无依赖)                                  │
│  Com_Board  → 关 JTAG,引脚字典                       │
│  Com_Delay  → SysTick 1ms 时基                       │
│  Com_Uart   → printf 重定向(勾 MicroLIB)             │
│      │                                              │
│      ▼  里程碑 1:上电能打印                          │
├─────────────────────────────────────────────────────┤
│ Int 层(每写一个,串口/仪表验证一个)                  │
│  Int_Wire   → 万用表量 PB3~PB7 电平翻转 ★JTAG 必须先关│
│  Int_Adc    → 量 3.3V/GND,读数对不对                 │
│  Int_Buzzer → BlockingBeep 一声 = 硬件分界线         │
│  Int_Relay  → 听吸合声                              │
│  Int_Rs485  → USB-485 看 JSON 帧                    │
│  Int_Oled   → 初始化序列 + 电荷泵,屏亮               │
│      │                                              │
│      ▼  里程碑 2:每个外设单独可驱动                  │
├─────────────────────────────────────────────────────┤
│ App 层                                               │
│  App_Measure → V1~V10 采集 + 三判定(核心)            │
│  App_State   → 状态机 + 去抖 + 报警联动               │
│  App_Config  → Flash + CRC16 + magic                │
│  App_Display / App_Menu(后置)                       │
└────────────┬────────────────────────────────────────┘
             ▼
        进入 ③

⚠ 本项目踩过的坑:Int 层写完**没同步加进 Keil 工程**,
  代码存在但固件里根本没有。核对编译清单要看**构建日志**
  (Program Size 和 compiling 行),不能只看文件在不在目录里。

```

## 13.4 ③ 阶段一:原理验证(main.c)

```
        开始
         │
         ▼
┌────────────────────────┐
│ 空载采集 V1~V10         │
│ 串口每轮打全表          │
└───────────┬────────────┘
            ▼
    ┌────────────────┐         否
    │ 极性/量级符合   │─────────────────┐
    │ 预期?          │                 │
    └───────┬────────┘                 ▼
            │是              ┌──────────────────┐
            ▼                │ 查接线/开关逻辑   │
┌────────────────────────┐  │ (对照六组合表)    │
│ 制造故障验证判定:       │  └────────┬─────────┘
│ · 断黄线→断线报警?      │───────────┘
│ · 断红线→断线报警?      │
│ · 湿地/泡水→水浸报警?   │
└───────────┬────────────┘
            ▼
    ┌────────────────┐         否
    │ 判定全对?       │────→ 调容差/门限 → 重测
    └───────┬────────┘
            │是
            ▼
        进入 ④  ← ★本项目当前停在这里

```

## 13.5 ④ 标定(硬件依赖，无法绕过)

```
┌─────────────────────────────────────────┐
│ 4 个数必须实测:                          │
│                                         │
│ r ──────→ 万用表量已知长度线缆回路电阻   │
│           r = R/(2×L₀)                  │
│           ★厂商目录 13Ω/m,必须自己量核对 │
│ L ──────→ 卷尺量实际线缆长度             │
│ 容差 ────→ 观察干燥时 V1-V2 波动,取 2~3 倍│
│ 稳定延时 → 示波器看 Wire_Apply 后稳定时间 │
└───────────────┬─────────────────────────┘
                ▼
        ┌───────────────┐
        │ 线缆够长吗?    │
        │ (r×L 与 R11    │
        │  可比,≥7.5m 级)│
        └───┬───────┬───┘
          是│     否│(如 40cm)
            ▼       ▼
        正常标定   定位不可行:①放弃定位只做
        定位       有/无报警 ②换小 R11 重设计
            │      硬件(回路电流会 ×75)
            ▼
    ┌──────────────────┐
    │ 已知点泡水验证    │
    │ L1 计算值 vs 实际 │────→ 系统偏差?
    └────────┬─────────┘         │是
             │否(±内)            ▼
             ▼            试 MEAS_R_SAMPLE_OHM
        进入 ⑤            4000→3900 重算

```

## 13.6 ⑤ 阶段二/三:正式运行

```
┌───────────────────────────────────────┐
│ 换主程序: main_with_oled.c            │
│ · 状态机 + 报警联动(蜂鸣/继电器/RS485) │
│ · OLED 实时显示                        │
│ · Flash 参数掉电保存                   │
└──────────────┬────────────────────────┘
               ▼
    ┌────────────────────┐
    │ 全场景回归:         │
    │ 干燥 → OK          │
    │ 泡水 → 报警+定位+蜂鸣│
    │ 断线 → 报警+线路指示 │
    │ 擦干 → 恢复         │
    │ 改参数 → 断电 → 保持 │
    └──────────┬─────────┘
               ▼
    ┌────────────────────┐   否
    │ 通过?               │────────┐
    └──────────┬─────────┘        │
               │是                ▼
               ▼         修 bug → 重烧 → 重测
    ╔════════════════════════════╗
    ║ 焊按键(PA0/PB0/PA8 ★先量  ║
    ║ 引脚归属!) → 菜单版        ║
    ║ 历史记录 / 参数菜单 / 中文字库║
    ║ (字库需重做,15 字塌缩)      ║
    ╚══════════┬═════════════════╝
               ▼
    ╔════════════════════════════╗
    ║ 阶段三(可选):SGM58031      ║
    ║ 16bit 外置 ADC 提精度       ║
    ║ ⚠与 RS485 共用 PB10/11,互斥║
    ║ USE_EXTERNAL_ADC=1 切换     ║
    ╚══════════┬═════════════════╝
               ▼
           项目交付

```

## 13.7 当前进度

```
①───②───③───④───⑤
          ▲   ▲
          │   └─ 卡点:r/L 未实测;且实测线缆仅 40cm,定位不可行
          └─ 已完成:判定逻辑验证通过、报警联动修通、屏幕点亮

```

**下一步只有一个动作能推进项目**:找一根 ≥7.5m 的检测线(或按 13.5 的"否"分支决定放弃定位)。菜单、字库属 ⑤ 的锦上添花,不阻塞主线。

---

*本文档基于 2026-09-14 的代码状态编写。*

**核对范围说明**

已逐行核对的文件:`Com_Board.c`、`Com_Delay.c`、`Com_Uart.c`、`Int_Wire.c`、`Int_Adc.c`、`Int_Buzzer.c`、`Int_Relay.c`、`Int_Rs485.c`、`Int_Oled.c`(含刷新与绘图)、`Int_Font_CN.c`(字模重复结论以脚本逐字节比对得出)、`Int_Key.c`、`App_Measure.c`、`App_State.c`、`App_Config.c`、`App_Display.c`、`App_Menu.c`、`stm32f10x_it.c`、五个 main 文件、`project.uvprojx` 的分组与编译选项。

**仍未核实、需硬件或原理图确认的项**(文中已逐处标注):

- R9 是否串在蜂鸣器供电路径上(11.3)—— 来自 `Int_Buzzer.h` 注释,原注释本身要求去 Altium 核对;
- 按键引脚 PA0/PB0/PA8 的实际归属,以及 OLED SCL 在 PB0 还是 PB1(11.4)—— 需万用表量通断;
- RS485 与 I2C2 共用 PB10/PB11 的互斥性(11.5)—— 从两个头文件定义推出,未实测;
- `Int_Font_CN.c` 中未被塌缩的那 49 个字形是否正确(6.8)—— 只验证了有重复,没验证每个字形画出来像不像那个字。

**未逐行核对的文件**:`Int_Oled.c` 的 ASCII 字模数据表(0x20~0x7E 共 95 项)、`Int_Sgm58031.c`(阶段三未启用)、`App_Menu.c` 的部分显示分支、标准外设库与 CMSIS(`Library/`、`Start/` 未改动)。
