#include "App_State.h"
#include "App_Measure.h"
#include "Int_Buzzer.h"
#include "Int_Relay.h"
#include "Int_Rs485.h"
#include "Com_Delay.h"
#include "Com_Uart.h"
#include <stdio.h>
#include <string.h>

/* 当前状态 */
static SystemState_t s_state = STATE_NORMAL;

/* 去抖计数器 */
static uint8_t s_debounce_cnt = 0;

/* 上次扫描时间戳 */
static uint32_t s_last_scan_ts = 0;

/* 定位结果缓存（水浸状态下有效） */
static int32_t s_leak_pos_mm = 0;

/**
 * @brief  发送 RS485 状态帧
 * @note   格式：JSON 字符串，便于上位机解析
 *         正常: {"state":"normal"}
 *         断线: {"state":"break","line":"黄线回路"}
 *         水浸: {"state":"leak","pos_m":12.345}
 */
static void SendStatusFrame(const char *state_str, const char *detail)
{
    char buf[128];
    int len;

    if (detail != NULL)
    {
        len = snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"detail\":\"%s\"}\r\n",
                       state_str, detail);
    }
    else
    {
        len = snprintf(buf, sizeof(buf), "{\"state\":\"%s\"}\r\n", state_str);
    }

    if (len > 0 && len < (int)sizeof(buf))
    {
        Rs485_SendData((const uint8_t *)buf, (uint16_t)len);
    }
}

static void SendLeakPosFrame(int32_t pos_mm)
{
    char buf[128];
    int len;

    /* 毫米转米，保留三位小数 */
    len = snprintf(buf, sizeof(buf), "{\"state\":\"leak\",\"pos_m\":%d.%03d}\r\n",
                   (int)(pos_mm / 1000), (int)(pos_mm % 1000));

    if (len > 0 && len < (int)sizeof(buf))
    {
        Rs485_SendData((const uint8_t *)buf, (uint16_t)len);
    }
}

/**
 * @brief  进入正常状态
 */
static void EnterNormal(void)
{
    s_state = STATE_NORMAL;
    s_debounce_cnt = 0;

    Buzzer_SetPattern(BUZZER_PATTERN_OFF);
    Relay_Off();

    printf("[状态] 进入正常监测\r\n");
    SendStatusFrame("normal", NULL);
}

/**
 * @brief  进入断线报警状态
 */
static void EnterBreakAlarm(const char *line_name)
{
    s_state = STATE_BREAK_ALARM;
    s_debounce_cnt = 0;

    Buzzer_SetPattern(BUZZER_PATTERN_BREAK);
    Relay_On();

    printf("[报警] 断线故障: %s\r\n", line_name);
    SendStatusFrame("break", line_name);
}

/**
 * @brief  进入水浸报警状态
 */
static void EnterLeakAlarm(int32_t pos_mm)
{
    s_state = STATE_LEAK_ALARM;
    s_debounce_cnt = 0;
    s_leak_pos_mm = pos_mm;

    Buzzer_SetPattern(BUZZER_PATTERN_LEAK);
    Relay_On();

    printf("[报警] 水浸定位: %d.%03d m\r\n",
           (int)(pos_mm / 1000), (int)(pos_mm % 1000));
    SendLeakPosFrame(pos_mm);
}

void State_Init(void)
{
    s_state = STATE_NORMAL;
    s_debounce_cnt = 0;
    s_last_scan_ts = Delay_GetTick();
    s_leak_pos_mm = 0;

    Buzzer_SetPattern(BUZZER_PATTERN_OFF);
    Relay_Off();
}

void State_Poll(void)
{
    MeasVolt_t volt;
    MeasResult_t result;

    /* 扫描节拍控制 */
    if (!Delay_IsTimeout(s_last_scan_ts, STATE_SCAN_INTERVAL_MS))
    {
        return;
    }
    s_last_scan_ts = Delay_GetTick();

    /* 执行一次完整测量 */
    Measure_ReadAll(&volt);
    Measure_Judge(&volt, &result);

    State_Update(&result);
}

void State_Update(const MeasResult_t *in)
{
    MeasResult_t result;

    if (in == 0) { return; }
    result = *in;

    /* 去抖计数加饱和，避免 uint8_t 加到 255 回绕后凭空出现一个报警窗口 */
    #define DEBOUNCE_INC()  do { if (s_debounce_cnt < 250) { s_debounce_cnt++; } } while (0)

    /* 状态机转换逻辑 */
    switch (s_state)
    {
    case STATE_NORMAL:
        /* 水浸优先于断线：进水比断线危险，而且未标定时 EQUAL_TOL 偏紧容易误判断线，
         * 若让断线抢先，真实进水会被整个报警期屏蔽掉。 */
        if (result.leak_detected)
        {
            DEBOUNCE_INC();
            /* 注意：这里不再要求 pos_valid。
             * pos_valid 由 Measure_CalcL1() 给出，未标定 r/L 时几乎恒为 0，
             * 以它作为报警前提会导致"泡在水里但蜂鸣器不响"。
             * 定位算不出来只影响显示什么位置，不影响该不该报警。 */
            if (s_debounce_cnt >= STATE_DEBOUNCE_COUNT)
            {
                EnterLeakAlarm(result.pos_valid ? result.pos_mm : -1);
            }
        }
        else if (result.break_detected)
        {
            DEBOUNCE_INC();
            if (s_debounce_cnt >= STATE_DEBOUNCE_COUNT)
            {
                EnterBreakAlarm(result.break_line);
            }
        }
        else
        {
            s_debounce_cnt = 0;     /* 无故障，清零计数 */
        }
        break;

    case STATE_BREAK_ALARM:
        /* 断线报警 -> 检测恢复 */
        if (!result.break_detected)
        {
            s_debounce_cnt++;
            if (s_debounce_cnt >= STATE_RECOVER_COUNT)
            {
                EnterNormal();
            }
        }
        else
        {
            s_debounce_cnt = 0;     /* 故障仍存在 */
        }
        break;

    case STATE_LEAK_ALARM:
        /* 水浸报警 -> 检测恢复 */
        if (!result.leak_detected)
        {
            s_debounce_cnt++;
            if (s_debounce_cnt >= STATE_RECOVER_COUNT)
            {
                EnterNormal();
            }
        }
        else
        {
            s_debounce_cnt = 0;

            /* 水浸持续期间，如果定位结果变化超过 100mm，更新位置 */
            if (result.pos_valid)
            {
                int32_t delta = result.pos_mm - s_leak_pos_mm;
                if (delta < 0) { delta = -delta; }

                if (delta > 100)
                {
                    s_leak_pos_mm = result.pos_mm;
                    printf("[定位更新] %d.%03d m\r\n",
                           (int)(result.pos_mm / 1000), (int)(result.pos_mm % 1000));
                    SendLeakPosFrame(result.pos_mm);
                }
            }
        }
        break;

    default:
        s_state = STATE_NORMAL;
        break;
    }
}

SystemState_t State_Get(void)
{
    return s_state;
}
