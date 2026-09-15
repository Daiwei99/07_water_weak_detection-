#ifndef __BOARD_H
#define __BOARD_H

#include "stm32f10x.h"

/*==============================================================================
 * 板级引脚定义 —— 全部引脚集中在此，改板只改这里
 *
 * 来源：原理图.pdf (漏水检测-控制板-第2版·2026-09-06)
 * MCU ：STM32F103C8T6  LQFP48
 *
 * 【重要】PA15 / PB3 / PB4 默认被 JTAG-DP 占用（JTDI / JTDO / JNTRST）。
 *         本板调试口走 SWD（PA13/PA14，见 P3 排针），所以必须在初始化时
 *         调用 Board_Init() 关闭 JTAG，否则红线供电 / 黄线供电 / 绿线接R9
 *         三路控制全部失效，且现象非常迷惑（引脚读写都"正常"但外部没反应）。
 *============================================================================*/

/*---------------------- 四线电压控制（U4 = 74HC4066，高电平使能）-----------*/
/* 原理图标注："四线电压控制（高电平使能）" */
#define RED_PWR_PORT        GPIOB
#define RED_PWR_PIN         GPIO_Pin_4          /* PB4 = 1 红线供电         */
#define RED_PWR_RCC         RCC_APB2Periph_GPIOB

#define YEL_PWR_PORT        GPIOB
#define YEL_PWR_PIN         GPIO_Pin_3          /* PB3 = 1 黄线供电         */
#define YEL_PWR_RCC         RCC_APB2Periph_GPIOB

#define GRN_R9_PORT         GPIOA
#define GRN_R9_PIN          GPIO_Pin_15         /* PA15 = 1 绿线接取样电阻  */
#define GRN_R9_RCC          RCC_APB2Periph_GPIOA

#define BLK_R9_PORT         GPIOB
#define BLK_R9_PIN          GPIO_Pin_5          /* PB5 = 1 黑线接取样电阻   */
#define BLK_R9_RCC          RCC_APB2Periph_GPIOB

/*---------------------- 模拟开关通道选择（U7 = HEF4052）--------------------*/
/* IO1 = S1 = PB6 , IO2 = S2 = PB7                                          */
#define SW_S1_PORT          GPIOB
#define SW_S1_PIN           GPIO_Pin_6          /* PB6 -> 4052 S1 (IO1)     */
#define SW_S2_PORT          GPIOB
#define SW_S2_PIN           GPIO_Pin_7          /* PB7 -> 4052 S2 (IO2)     */

/*---------------------- ADC 采样输入 --------------------------------------*/
/* 网络名 ADC0 / ADC1 是 U7(4052) 的两路输出 1Z / 2Z，
 * 经 R14 / R15（均为 0Ω）同时接到两处：
 *   - U5 SGM58031 的 AIN0 / AIN1   （外置 16bit I2C ADC，后期提精度用）
 *   - MCU 的 PA1 / PA2             （内部 12bit ADC，阶段一验证用）
 *
 * 【命名陷阱】原理图网络 "ADC1" 与 STM32 的 ADC1 外设同名但含义完全不同。
 *   本工程统一约定：
 *       NET_ADC0 == PA1 == STM32 ADC1_IN1
 *       NET_ADC1 == PA2 == STM32 ADC1_IN2
 *   代码里一律用 Adc_ReadNet0() / Adc_ReadNet1() 取值，不要直接写通道号。
 *
 * 【注意】U1 引脚 10 = PA0-WKUP 在原理图上是打叉的（未连接）。
 *   数引脚时容易整体错一格，把 PA0 当成 ADC0 —— 认准引脚序号：
 *     pin 10 = PA0-WKUP  未连接（×）
 *     pin 11 = PA1       网络 ADC0
 *     pin 12 = PA2       网络 ADC1
 *     pin 13 = PA3       网络 ALERT
 *     pin 14 = PA4       网络 BELL                                        */
#define NET_ADC0_PORT       GPIOA
#define NET_ADC0_PIN        GPIO_Pin_1
#define NET_ADC0_CHANNEL    ADC_Channel_1       /* PA1，U1 引脚 11 */

#define NET_ADC1_PORT       GPIOA
#define NET_ADC1_PIN        GPIO_Pin_2
#define NET_ADC1_CHANNEL    ADC_Channel_2       /* PA2，U1 引脚 12 */

/* 内部 ADC 的参考电压 = VDDA = 3.3V。
 * 注意：外置 ADC(U5) 用的是 TL431 产生的 ADC_VREF，接在 AIN3/VREFIN 上，
 *       两者基准不同，换用外置 ADC 时这个值要跟着改。 */
#define ADC_VREF_VOLT       3.3f
#define ADC_FULL_SCALE      4095.0f             /* 内部 ADC 12bit */

/*---------------------- 报警输出（NPN 驱动，高电平有效）--------------------*/
/* BELL -> R10(2k) -> Q1(MMS9013-H-TP) 基极，Q1 集电极拉 BUZZER1 的负端。
 * R13(10k) 是基极下拉，保证 MCU 复位期间引脚浮空时蜂鸣器不误响。
 *
 * 【BUZZER1 是无源电磁式，不能用 GPIO 拉高来响】
 *   型号 MLT-8530，手册（C94599_蜂鸣器_MLT-8530_规格书_WJ135517.PDF）明确写的是
 *   "Electro-Magnetic Buzzer"，共振频率 2700Hz，要求 50% 占空比方波驱动。
 *   BELL 恒定拉高只会让线圈吸合一次，"嗒"一声就没了。
 *   驱动方式见 Int_Buzzer.h —— 用 TIM3 中断翻转 PA4 做软件 PWM。
 *
 * 【为什么不能用硬件 PWM】
 *   PA4 在 F103 上的复用功能只有 SPI1_NSS / USART2_CK / ADC_IN4，
 *   不带任何 TIM 通道，重映射也映不过来（TIM3 全重映射去 PC6~PC9，
 *   TIM2 去 PA15/PB3/PB10/PB11），所以只能软件翻转。 */
#define BELL_PORT           GPIOA
#define BELL_PIN            GPIO_Pin_4          /* PA4，U1 引脚 14 */
#define BELL_RCC            RCC_APB2Periph_GPIOA

/* RELAY -> R30(2k) -> Q4(MMS9013-H-TP) 基极，Q4 拉 K1(HFD3/5) 线圈低端，
 * 线圈高端接 5V，D1(BZT52C5V1) 反并在线圈上吸收断电反峰。
 * 触点引到 P6 端子的 COM / NO。R29(10k) 基极下拉，同样是防复位期误动作。
 * 所以 RELAY = 1 -> 继电器吸合 -> COM 与 NO 接通。 */
#define RELAY_PORT          GPIOB
#define RELAY_PIN           GPIO_Pin_14         /* PB14，U1 引脚 27 */
#define RELAY_RCC           RCC_APB2Periph_GPIOB

/*---------------------- 外置 ADC 转换完成中断 ------------------------------*/
/* U5(SGM58031) 的 ALERT/RDY 引脚 -> 网络 ALERT -> PA3。
 * 阶段一用内部 ADC，不需要这个中断，此处仅登记引脚占用。 */
#define ADC_ALERT_PORT      GPIOA
#define ADC_ALERT_PIN       GPIO_Pin_3          /* PA3，U1 引脚 13 */

/*---------------------- 调试串口 UART1 ------------------------------------*/
/* P3 排针上引出了 UART1_TX / UART1_RX，是唯一的调试出口 */
#define DBG_UART            USART1
#define DBG_UART_RCC        RCC_APB2Periph_USART1
#define DBG_UART_PORT       GPIOA
#define DBG_UART_TX_PIN     GPIO_Pin_9          /* PA9  */
#define DBG_UART_RX_PIN     GPIO_Pin_10         /* PA10 */
#define DBG_UART_BAUD       115200

/*---------------------- RS485（U2 = MAX13487，自动方向）-------------------*/
/* MAX13487 内部自动收发切换，不需要 DE/RE 方向控制引脚，省一个 GPIO */
#define RS485_UART          USART3
#define RS485_UART_RCC      RCC_APB1Periph_USART3
#define RS485_PORT          GPIOB
#define RS485_TX_PORT       GPIOB
#define RS485_RX_PORT       GPIOB
#define RS485_TX_PIN        GPIO_Pin_10         /* PB10 = USART3_TX */
#define RS485_RX_PIN        GPIO_Pin_11         /* PB11 = USART3_RX */
#define RS485_TX_RCC        RCC_APB2Periph_GPIOB
#define RS485_RX_RCC        RCC_APB2Periph_GPIOB
#define RS485_BAUD          9600

/*---------------------- OLED（U8 = HS96L03W2C03）--------------------------
 * 网络 SCL -> PB1（U1 引脚 19），SDA -> PB2（U1 引脚 20）。
 *
 * 【别数错一格】U1 引脚 18 = PB0 在原理图上未接网络：
 *     pin 18 = PB0  未用
 *     pin 19 = PB1  = 网络 SCL
 *     pin 20 = PB2  = 网络 SDA
 *
 * 【注意 1】PB1/PB2 不是任何 I2C 外设的引脚，只能用软件模拟 I2C。
 *
 * 【注意 2】PB2 就是 BOOT1。它只在复位瞬间被采样以决定启动模式，之后当
 *           普通 GPIO 用没问题。但 OLED 那侧若有上拉，复位时 BOOT1 被拉高，
 *           会影响 BOOT0=1 时的启动分支选择。板上 BOOT0 经 R1(0Ω) 接地
 *           （见原理图 U1 引脚 44 旁），所以正常从 Flash 启动不受影响。
 *           后面接屏幕后如果出现"偶尔启动不起来"，先查这里。
 *
 * 【注意 3】U8 的 VCC 接的是 5V，不是 3V3。
 *
 * 阶段一不需要屏幕，此处仅登记引脚。
 *------------------------------------------------------------------------*/
#define OLED_SCL_PORT       GPIOB
#define OLED_SCL_PIN        GPIO_Pin_1          /* PB1，U1 引脚 19 */
#define OLED_SDA_PORT       GPIOB
#define OLED_SDA_PIN        GPIO_Pin_2          /* PB2，U1 引脚 20 = BOOT1 */

/*---------------------- 外置 ADC I2C（U5 SGM58031）------------------------
 * 网络 SCL_1 -> PB8（U1 引脚 45），SDA_1 -> PB9（U1 引脚 46），
 * 各带 R17 / R18 = 4.7k 上拉到 3V3。这正好是 I2C1 重映射后的引脚分配
 * （I2C1_SCL = PB8、I2C1_SDA = PB9），所以硬件 I2C1 可用，
 * 但要记得调用 GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE)。
 *
 * SCL_1 / SDA_1 同时引到 H1 排针（PZ1.0-UP1D-2A），方便外部挂逻辑分析仪。
 * U5 的 ADDR 引脚接地 -> 7 位从机地址 0x48。
 * 阶段一用内部 ADC，不碰这里。
 *------------------------------------------------------------------------*/
#define EADC_SCL_PORT       GPIOB
#define EADC_SCL_PIN        GPIO_Pin_8          /* PB8 = I2C1_SCL(remap) */
#define EADC_SDA_PORT       GPIOB
#define EADC_SDA_PIN        GPIO_Pin_9          /* PB9 = I2C1_SDA(remap) */
#define EADC_I2C_ADDR       0x48                /* ADDR 接地 */

/*==============================================================================
 * 板级初始化：时钟 + 关闭 JTAG。必须在所有外设初始化之前调用。
 *============================================================================*/
void Board_Init(void);

#endif /* __BOARD_H */
