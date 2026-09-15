#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "stm32f10x.h"

/*==============================================================================
 * 内部 ADC 采集（STM32 ADC1，12bit）
 *
 * 采的是 U7(HEF4052) 的两路输出：
 *     网络 ADC0 -> R14(0Ω) -> PA1 (U1 引脚 11) -> STM32 ADC1_IN1
 *     网络 ADC1 -> R15(0Ω) -> PA2 (U1 引脚 12) -> STM32 ADC1_IN2
 *
 * 【注意】PA0-WKUP（引脚 10）在原理图上是打叉的（未连接），不是 ADC 输入。
 *
 * 【命名】原理图网络名 "ADC1" 和 STM32 的 ADC1 外设撞名，而且网络 ADC0/ADC1
 *         的编号与 STM32 通道号也错开一位（网络 ADC0 走的是 ADC1_IN1）。
 *         本文件一律用 Net0 / Net1 指原理图网络，避免歧义：
 *             Adc_ReadNet0() 读的是网络 ADC0（4052 的 1Z，PA1，通道 1）
 *             Adc_ReadNet1() 读的是网络 ADC1（4052 的 2Z，PA2，通道 2）
 *
 * 【为什么阶段一用内部 ADC】
 *         同一个网络也接到 U5(SGM58031) 这颗外置 16bit ADC 上，但那需要先写
 *         I2C 驱动。内部 ADC 12bit 分辨率 0.8mV，足够验证定位公式和判定逻辑；
 *         公式验证通过后再换外置 ADC 提精度，接口保持不变。
 *============================================================================*/

/* 一次测量取几个原始样本做滤波。去掉最大最小后求平均，
 * 所以至少要 3 个才有意义。9 个是精度和耗时的折中（约 9×17µs）。 */
#define ADC_SAMPLE_COUNT    9

void Adc_Init(void);

/* 读原始码值 0~4095，已做去极值平均 */
uint16_t Adc_ReadNet0(void);
uint16_t Adc_ReadNet1(void);

/* 读电压值（V），= raw / 4095 * 3.3 */
float Adc_ReadNet0Volt(void);
float Adc_ReadNet1Volt(void);

/* 码值转电压，单独暴露出来方便外部换算 */
float Adc_RawToVolt(uint16_t raw);

#endif /* __BSP_ADC_H */
