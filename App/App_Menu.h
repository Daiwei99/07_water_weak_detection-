/**
 ******************************************************************************
 * @file    App_Menu.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   菜单系统（参数查看与设置）
 *
 * 菜单结构：
 *   主界面（监测显示）
 *       ├─ [长按确认] 进入主菜单
 *       ├─ 1. 查看参数
 *       ├─ 2. 修改参数
 *       ├─ 3. 系统设置
 *       └─ 4. 返回监测
 *
 * 使用示例：
 *   Menu_Init();
 *
 *   // 在主循环中
 *   Menu_Process();  // 处理按键和菜单逻辑
 *   Menu_Display();  // 更新显示
 ******************************************************************************
 */

#ifndef __APP_MENU_H
#define __APP_MENU_H

#include "stm32f10x.h"

/* ========== 菜单状态 ========== */

typedef enum
{
    MENU_STATE_MONITOR = 0,     // 监测界面（主界面）
    MENU_STATE_MAIN,            // 主菜单
    MENU_STATE_VIEW_PARAM,      // 查看参数
    MENU_STATE_EDIT_PARAM,      // 修改参数
    MENU_STATE_SYSTEM_SET,      // 系统设置
    MENU_STATE_VIEW_HISTORY,    // 历史记录查看
} MenuState_e;

/* ========== API 函数 ========== */

/**
 * @brief  菜单系统初始化
 */
void Menu_Init(void);

/**
 * @brief  菜单逻辑处理
 * @note   处理按键事件，更新菜单状态
 * @retval 1=需要刷新显示，0=无需刷新
 */
uint8_t Menu_Process(void);

/**
 * @brief  菜单显示更新
 * @note   根据当前菜单状态更新OLED显示
 */
void Menu_Display(void);

/**
 * @brief  获取当前菜单状态
 * @retval 当前菜单状态
 */
MenuState_e Menu_GetState(void);

/**
 * @brief  判断是否在监测界面
 * @retval 1=在监测界面，0=在菜单中
 */
uint8_t Menu_IsMonitoring(void);

/**
 * @brief  添加历史记录
 * @param  type: 0=断线, 1=水浸
 * @param  position_m: 位置（米）
 * @param  detail: 详细信息字符串
 */
void Menu_AddHistory(uint8_t type, float position_m, const char *detail);

/**
 * @brief  获取采样间隔设置
 * @retval 采样间隔（毫秒）
 */
uint16_t Menu_GetSamplingInterval(void);

/**
 * @brief  获取去抖次数设置
 * @retval 去抖次数
 */
uint8_t Menu_GetDebounceCnt(void);

#endif /* __APP_MENU_H */
