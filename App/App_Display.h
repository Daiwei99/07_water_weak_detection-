/**
 ******************************************************************************
 * @file    App_Display.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   OLED 显示应用层 - 封装业务显示逻辑
 *
 * 显示布局（128x64，4行文字）：
 *   行0: 状态信息（正常/断线/水浸）
 *   行1: 定位距离 或 故障详情
 *   行2-3: 电压信息（V1~V10滚动显示）
 *
 * 使用示例：
 *   Display_Init();
 *   Display_ShowState("Normal");
 *   Display_ShowVoltage(0, 3250);  // V1=3.250V
 *   Display_Refresh();
 ******************************************************************************
 */

#ifndef __APP_DISPLAY_H
#define __APP_DISPLAY_H

#include "stm32f10x.h"
#include "App_Measure.h"
#include "App_State.h"

/* ========== 初始化 ========== */

/**
 * @brief  显示模块初始化
 * @note   内部调用 Oled_Init()
 */
void Display_Init(void);

/* ========== 状态显示 ========== */

/**
 * @brief  显示状态信息（第0行）
 * @param  state: 状态枚举
 * @param  detail: 详细信息（如断线的线路名称），可为NULL
 */
void Display_ShowStatus(SystemState_t state, const char *detail);

/**
 * @brief  显示定位结果（第1行）
 * @param  valid: 1=有效，0=无效
 * @param  pos_mm: 定位距离（毫米）
 */
void Display_ShowPosition(uint8_t valid, int32_t pos_mm);

/* ========== 电压显示 ========== */

/**
 * @brief  显示电压列表（第2-3行）
 * @param  volt: 电压结构体
 * @param  page: 显示页码（0=V1~V5, 1=V6~V10）
 */
void Display_ShowVoltages(const MeasVolt_t *volt, uint8_t page);

/* ========== 完整界面更新 ========== */

/**
 * @brief  完整界面更新（根据测量结果刷新全屏）
 * @param  result: 综合判定结果
 * @param  volt: 电压数据
 * @param  state: 当前系统状态
 */
void Display_UpdateAll(const MeasResult_t *result,
                       const MeasVolt_t *volt,
                       SystemState_t state);

/**
 * @brief  刷新到屏幕
 * @note   所有显示操作后必须调用此函数
 */
void Display_Refresh(void);

#endif /* __APP_DISPLAY_H */
