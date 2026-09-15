/**
 ******************************************************************************
 * @file    Int_Oled.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   OLED 显示驱动（SSD1306，128x64，软件I2C）
 *
 * 硬件连接：
 *   PB1 -> SCL (时钟线)
 *   PB2 -> SDA (数据线)
 *   无硬件I2C，GPIO模拟
 *
 * 使用示例：
 *   Oled_Init();
 *   Oled_Clear();
 *   Oled_ShowString(0, 0, "Hello");
 *   Oled_ShowNum(0, 2, 1234, 4);
 *   Oled_Refresh();
 ******************************************************************************
 */

#ifndef __INT_OLED_H
#define __INT_OLED_H

#include "stm32f10x.h"

/* OLED 参数 */
#define OLED_WIDTH      128     // 屏幕宽度（像素）
#define OLED_HEIGHT     64      // 屏幕高度（像素）
#define OLED_PAGE_NUM   8       // 页数（64/8=8）

/* I2C 地址（SSD1306 默认 0x78，7位地址+写位） */
#define OLED_I2C_ADDR   0x78

/* ========== 初始化与基础操作 ========== */

/**
 * @brief  OLED 初始化
 * @note   必须在使用前调用一次，包含软件I2C初始化和SSD1306配置
 */
void Oled_Init(void);

/**
 * @brief  清空显示缓冲区（全黑）
 * @note   需调用 Oled_Refresh() 才生效
 */
void Oled_Clear(void);

/**
 * @brief  刷新显示（将缓冲区内容发送到OLED）
 * @note   所有绘图操作后必须调用此函数才能看到效果
 */
void Oled_Refresh(void);

/**
 * @brief  开启显示
 */
void Oled_DisplayOn(void);

/**
 * @brief  关闭显示
 */
void Oled_DisplayOff(void);

/* ========== 绘图函数 ========== */

/**
 * @brief  画点
 * @param  x: 横坐标 (0~127)
 * @param  y: 纵坐标 (0~63)
 * @param  on: 1=点亮，0=熄灭
 */
void Oled_DrawPixel(uint8_t x, uint8_t y, uint8_t on);

/**
 * @brief  显示单个字符（8x16字体）
 * @param  x: 列位置 (0~15，每个字符占8像素宽)
 * @param  y: 行位置 (0~3，每行16像素高)
 * @param  ch: ASCII字符
 */
void Oled_ShowChar(uint8_t x, uint8_t y, char ch);

/**
 * @brief  显示字符串（8x16字体）
 * @param  x: 起始列位置 (0~15)
 * @param  y: 行位置 (0~3)
 * @param  str: 字符串（ASCII）
 */
void Oled_ShowString(uint8_t x, uint8_t y, const char *str);

/**
 * @brief  显示整数
 * @param  x: 起始列位置 (0~15)
 * @param  y: 行位置 (0~3)
 * @param  num: 要显示的数字
 * @param  len: 显示位数（不足补空格，超出截断）
 */
void Oled_ShowNum(uint8_t x, uint8_t y, int32_t num, uint8_t len);

/**
 * @brief  显示浮点数（毫伏转伏特格式：1234 -> "1.234"）
 * @param  x: 起始列位置
 * @param  y: 行位置
 * @param  mv: 毫伏值
 * @param  decimal: 小数位数
 */
void Oled_ShowVoltage(uint8_t x, uint8_t y, int32_t mv, uint8_t decimal);

/**
 * @brief  显示中文字符串（16x16字体）
 * @param  x: 起始列位置 (0~15)
 * @param  y: 行位置 (0~3，中文占2行高度)
 * @param  str: UTF-8编码的中文字符串
 * @note   每个汉字占16x16像素，自动从字库查找点阵数据
 */
void Oled_ShowChinese(uint8_t x, uint8_t y, const char *str);

#endif /* __INT_OLED_H */
