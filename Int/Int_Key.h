/**
 ******************************************************************************
 * @file    Int_Key.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   按键输入驱动（3键导航）
 *
 * 硬件连接：
 *   - 按键1（上/减）：PA0，按下接GND
 *   - 按键2（下/加）：PB0，按下接GND
 *   - 按键3（确认）：PA8，按下接GND
 *
 * 功能特性：
 *   - 硬件去抖（20ms）
 *   - 短按检测
 *   - 长按检测（1秒）
 *   - 连续重复（长按后每200ms重复，仅上/下键）
 *
 * 使用示例：
 *   Key_Init();
 *
 *   // 在主循环中每10ms调用一次
 *   Key_Scan();
 *
 *   // 获取按键事件
 *   KeyEvent_e event = Key_GetEvent();
 *   if (event == KEY_EVENT_OK_SHORT) {
 *       // 处理确认键短按
 *   }
 ******************************************************************************
 */

#ifndef __INT_KEY_H
#define __INT_KEY_H

#include "stm32f10x.h"

/* ========== 按键事件类型 ========== */

typedef enum
{
    KEY_EVENT_NONE = 0,         // 无事件
    KEY_EVENT_UP_SHORT,         // 上键短按
    KEY_EVENT_DOWN_SHORT,       // 下键短按
    KEY_EVENT_OK_SHORT,         // 确认键短按
    KEY_EVENT_UP_LONG,          // 上键长按
    KEY_EVENT_DOWN_LONG,        // 下键长按
    KEY_EVENT_OK_LONG,          // 确认键长按
    KEY_EVENT_UP_REPEAT,        // 上键连续（长按自动重复）
    KEY_EVENT_DOWN_REPEAT,      // 下键连续
} KeyEvent_e;

/* ========== API 函数 ========== */

/**
 * @brief  按键初始化
 * @note   配置GPIO为上拉输入模式
 */
void Key_Init(void);

/**
 * @brief  按键扫描
 * @note   每10ms调用一次，进行去抖和事件检测
 */
void Key_Scan(void);

/**
 * @brief  获取按键事件
 * @retval 按键事件类型，获取后自动清除
 */
KeyEvent_e Key_GetEvent(void);

#endif /* __INT_KEY_H */
