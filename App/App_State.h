#ifndef __APP_STATE_H
#define __APP_STATE_H

#include "stm32f10x.h"
#include "App_Measure.h"

/*==============================================================================
 * 阶段二：正式运行状态机
 *
 * 需求文档"阶段二：正式运行程序"：
 *   1. 常态循环监测：循环执行断线监测和水浸监测，无故障时持续扫描
 *   2. 异常处理：
 *      - 断线故障：屏幕显示报警、485输出、蜂鸣器/继电器触发
 *      - 水浸故障：屏幕显示定位点、485输出定位数据、蜂鸣器/继电器报警
 *
 * 状态定义：
 *   NORMAL      - 正常监测，无故障
 *   BREAK_ALARM - 断线报警中
 *   LEAK_ALARM  - 水浸报警中（已定位）
 *
 * 状态转换逻辑：
 *   NORMAL -> BREAK_ALARM：检测到断线且持续 DEBOUNCE_COUNT 次
 *   NORMAL -> LEAK_ALARM：检测到水浸且定位成功
 *   BREAK_ALARM / LEAK_ALARM -> NORMAL：故障消失且持续 RECOVER_COUNT 次
 *
 * 去抖：防止偶发干扰触发误报，要求连续 N 次采样结果一致才切换状态
 *============================================================================*/

/* 状态定义 */
typedef enum
{
    STATE_NORMAL = 0,       /* 正常监测 */
    STATE_BREAK_ALARM,      /* 断线报警 */
    STATE_LEAK_ALARM        /* 水浸报警 */
} SystemState_t;

/* 去抖计数：连续多少次采样结果一致才切换状态 */
#define STATE_DEBOUNCE_COUNT    3       /* 进入故障状态的去抖次数 */
#define STATE_RECOVER_COUNT     5       /* 恢复正常的去抖次数（更严格） */

/* 扫描间隔（ms）：每轮测量之间的延时 */
#define STATE_SCAN_INTERVAL_MS  500U

void State_Init(void);

/* 主循环调用：执行一次状态机循环（内部自己采集测量） */
void State_Poll(void);

/* 主循环已经采集过测量结果时调用这个，避免重复采集（一轮采集约 92ms）。
 * 调用方负责节拍控制。 */
void State_Update(const MeasResult_t *result);

/* 获取当前状态 */
SystemState_t State_Get(void);

#endif /* __APP_STATE_H */
