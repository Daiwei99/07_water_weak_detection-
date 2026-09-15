#ifndef __INT_BUZZER_H
#define __INT_BUZZER_H

#include "stm32f10x.h"

/*==============================================================================
 * 无源蜂鸣器驱动（BUZZER1 = MLT-8530，网络 BELL -> PA4）
 *
 * 【无源，必须给方波】
 *   手册 C94599_蜂鸣器_MLT-8530_规格书_WJ135517.PDF 第 1 页 Technical Parameter：
 *       Resonant Frequency : 2700Hz
 *       Coil Resistance    : 16Ω ±3
 *       Rated Voltage      : 3.6 Vo-p    (Operating 2.5~4.5 Vo-p)
 *       Rated Current      : max 95mA @ 2700Hz 50% duty square wave 5Vo-p
 *       Sound Output       : min 80dB @ 10cm，同上条件
 *   "Electro-Magnetic Buzzer" = 无源电磁式：里面只有线圈和振膜，没有振荡电路。
 *   BELL 恒定拉高只会让振膜吸合一次，"嗒"一声就停，不会持续发声。
 *   必须以 2700Hz、50% 占空比翻转，让振膜按共振频率往复运动。
 *
 * 【为什么用定时器中断翻转 GPIO，而不是硬件 PWM】
 *   PA4 在 STM32F103 上的复用功能只有 SPI1_NSS / USART2_CK / ADC_IN4，
 *   不挂任何 TIM 通道。重映射也解决不了：
 *       TIM3 完全重映射 -> PC6/PC7/PC8/PC9
 *       TIM2 各档重映射 -> PA0/PA1/PA2/PA3/PA15/PB3/PB10/PB11
 *   都不含 PA4。所以只能让定时器以 2 倍频率中断，每次中断翻转一次电平。
 *
 *   代价：2700Hz 方波需要 5400 次/秒中断。中断服务里只做一次 GPIO 翻转，
 *   @72MHz 约几十个时钟周期，CPU 占用不到 0.5%。本工程主循环是 1 秒一轮
 *   测量，完全不受影响。
 *
 * 【偏离共振频率的代价】
 *   电磁式蜂鸣器的声压对频率很敏感，偏离 2700Hz 几百 Hz 声音就明显变小。
 *   所以频率不要随意改，需要不同提示音时改"响多久、停多久"的节奏，
 *   而不是改频率。
 *
 * 【硬件疑点，接上蜂鸣器前请先确认】
 *   从原理图图片读到 BUZZER1 正端经 R9(10kΩ) 上拉到 5V。这在电气上讲不通：
 *   10kΩ 串在 16Ω 线圈的供电路径上，分压后线圈上只剩几毫伏，不可能发声
 *   （5V × 16/(10000+16) ≈ 8mV）。
 *   合理的接法是蜂鸣器正端直接接 5V，R9 另有用途（比如给 Q1 集电极上拉）。
 *   请在 Altium 里核对 R9 到底串在哪条支路。如果确实串在供电路径里，
 *   那是硬件问题，软件无法补救 —— 写得再对也不会响。
 *
 * 【另一个电压注意点】
 *   线圈供电是 5V，手册的 Operating 上限是 4.5 Vo-p，额定 3.6 Vo-p。
 *   超压使用会更响但寿命打折。本工程只在报警时短促鸣叫，占空比很低，
 *   实际影响有限。长期连续鸣叫的场景要考虑串个限流电阻降到 3.6V 附近。
 *============================================================================*/

/* 共振频率，来自手册。不要改 —— 偏离后声压掉得很快。 */
#define BUZZER_FREQ_HZ      2700U

/*------------------------------------------------------------------------------
 * 提示音类型
 *
 * 频率固定 2700Hz，靠"响多久 / 停多久 / 响几声"区分不同含义。
 * 断线和水浸用不同节奏，值班的人不用看屏幕就能分辨。
 *----------------------------------------------------------------------------*/
typedef enum
{
    BUZZER_PATTERN_OFF = 0,     /* 静音                                    */
    BUZZER_PATTERN_BEEP,        /* 单响一声 100ms，用于按键确认            */
    BUZZER_PATTERN_LEAK,        /* 水浸报警：急促，响 200ms 停 200ms       */
    BUZZER_PATTERN_BREAK,       /* 断线报警：缓慢，响 500ms 停 1500ms      */
    BUZZER_PATTERN_ON           /* 长鸣不停，用于自检                      */
} BuzzerPattern_t;

/**
 * @brief  初始化蜂鸣器：配 PA4 推挽输出 + TIM3 定时中断（默认不响）
 * @note   必须在 Board_Init() 之后调用（需要 GPIOA 时钟）。
 *         初始化完成后 PA4 = 0、TIM3 停止，蜂鸣器静音。
 */
void Buzzer_Init(void);

/**
 * @brief  设置提示音节奏
 * @note   只是登记状态，实际的"响-停"节拍由 Buzzer_Poll() 推进。
 *         切换节奏会立即从新节奏的第一拍开始，不等上一拍结束。
 */
void Buzzer_SetPattern(BuzzerPattern_t pattern);

/**
 * @brief  推进节拍，需要在主循环里反复调用
 * @note   为什么不在中断里做完：节拍是几百毫秒级的，放中断里要么占用另一个
 *         定时器，要么在 5400Hz 的中断里做计数判断 —— 都不划算。
 *         放主循环里用 Delay_GetTick() 比较时间戳，简单且够准。
 *
 *         调用间隔要明显小于最短的节拍（100ms），否则节奏会拖长。
 *         本工程主循环里有 1 秒的 Delay_ms，所以要么把长延时拆成小段并在
 *         每段之间调用本函数，要么在报警期间单独跑一个短循环。
 *         见 Buzzer_BlockingBeep() 的说明。
 */
void Buzzer_Poll(void);

/**
 * @brief  阻塞式鸣叫指定毫秒数，然后静音
 * @param  ms 鸣叫时长
 * @note   阶段一自检用。阻塞期间不做别的事，逻辑最简单，
 *         不依赖主循环调用 Buzzer_Poll()。
 *         正式运行时的报警请用 Buzzer_SetPattern() + Buzzer_Poll()，
 *         别用这个 —— 报警期间阻塞会耽误测量。
 */
void Buzzer_BlockingBeep(uint32_t ms);

/**
 * @brief  直接开关（内部用，也可用于自检）
 * @note   Buzzer_Start() 启动 TIM3 让 PA4 以 2700Hz 翻转；
 *         Buzzer_Stop() 停 TIM3 并把 PA4 拉低。
 *         停的时候必须拉低而不是留在随机电平：留在高电平会让线圈一直通电，
 *         16Ω 线圈上持续跑 300mA，既发热又没有声音。
 */
void Buzzer_Start(void);
void Buzzer_Stop(void);

#endif /* __INT_BUZZER_H */
