/**
 ******************************************************************************
 * @file    Int_Sgm58031.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   SGM58031 16位ADC驱动（差分测量，I2C接口）
 *
 * 硬件连接：
 *   PB10 -> SCL (I2C2)
 *   PB11 -> SDA (I2C2)
 *   使用STM32硬件I2C2
 *
 * SGM58031特性：
 *   - 16位分辨率
 *   - 差分输入（AIN0 - AIN1）
 *   - 内部PGA：1x, 2x, 4x, 8x
 *   - 采样率：8~860 SPS
 *   - I2C地址：0x48（ADDR引脚接GND）
 *
 * 使用示例：
 *   Sgm58031_Init();
 *   int16_t adc = Sgm58031_ReadDiff(SGM58031_PGA_1X);
 *   int32_t mv = Sgm58031_AdcToMv(adc, SGM58031_PGA_1X);
 ******************************************************************************
 */

#ifndef __INT_SGM58031_H
#define __INT_SGM58031_H

#include "stm32f10x.h"

/* I2C地址 */
#define SGM58031_I2C_ADDR   0x48

/* PGA增益配置 */
typedef enum
{
    SGM58031_PGA_1X = 0,    // ±6.144V
    SGM58031_PGA_2X = 1,    // ±4.096V
    SGM58031_PGA_4X = 2,    // ±2.048V
    SGM58031_PGA_8X = 3,    // ±1.024V
} Sgm58031Pga_e;

/* ========== 初始化与基础操作 ========== */

/**
 * @brief  SGM58031 初始化
 * @note   配置I2C2硬件接口（PB10/PB11），速率100kHz
 */
void Sgm58031_Init(void);

/**
 * @brief  读取差分ADC值（AIN0 - AIN1）
 * @param  pga: PGA增益选择
 * @retval 16位有符号ADC值（-32768~32767）
 */
int16_t Sgm58031_ReadDiff(Sgm58031Pga_e pga);

/**
 * @brief  ADC值转电压（毫伏）
 * @param  adc: 16位ADC值
 * @param  pga: 当前PGA增益
 * @retval 电压值（mV），带符号
 */
int32_t Sgm58031_AdcToMv(int16_t adc, Sgm58031Pga_e pga);

#endif /* __INT_SGM58031_H */
