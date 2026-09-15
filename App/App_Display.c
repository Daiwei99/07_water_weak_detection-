/**
 ******************************************************************************
 * @file    App_Display.c
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   OLED 显示应用层实现
 *
 * 屏幕是 128x64，8x16 字体 -> 每行 16 列、共 4 行。
 * 所有拼出来的字符串都必须 <= 16 字符，否则会被截断，屏上看到半截数字。
 ******************************************************************************
 */

#include "App_Display.h"
#include "Int_Oled.h"
#include <stdio.h>

/* 一行 16 列，留一个结束符 */
#define DISP_LINE_CHARS     16
#define DISP_BLANK_LINE     "                "      /* 正好 16 个空格 */

/* ========== 静态变量 ========== */

static uint8_t s_voltage_page = 0;  /* 电压显示页码（自动轮换） */

/**
 * @brief  毫伏格式化成 "x.xx"（两位小数），带负号
 * @note   两个原因必须单独写这个函数：
 *
 *         1) 负数。原来直接写 %d.%02d，传 -500mV 时整数部分算出 0、
 *            小数部分算出 -50，拼出来是 "0.-50" —— 负号丢了、格式也坏了。
 *            ADC 两个通道失配时 v1~v10 出负值是正常的，不是异常输入。
 *
 *         2) 宽度。"V1:%d.%02d V2:%d.%02d" 展开是 18 列，超出一行 16 列，
 *            snprintf 会把后半截截掉，屏上 V2 显示不全。
 *            先各自格式化成短串再拼，就能控制总宽度。
 */
static void FmtMv(char *dst, uint32_t cap, int32_t mv)
{
    int32_t neg = (mv < 0);
    int32_t a   = neg ? -mv : mv;

    snprintf(dst, cap, "%s%d.%02d",
             neg ? "-" : "",
             (int)(a / 1000),
             (int)((a % 1000) / 10));
}

/* ========== 初始化 ========== */

void Display_Init(void)
{
    Oled_Init();
    Oled_Clear();

    Oled_ShowString(0, 0, "Water Detector");
    Oled_ShowString(0, 1, "Init...");
    Oled_Refresh();
}

/* ========== 状态显示 ========== */

void Display_ShowStatus(SystemState_t state, const char *detail)
{
    (void)detail;   /* 故障详情放在第 1 行显示，这里只显示状态本身 */

    Oled_ShowString(0, 0, DISP_BLANK_LINE);

    switch (state)
    {
        case STATE_NORMAL:
            Oled_ShowString(0, 0, "OK: Normal");
            break;

        case STATE_BREAK_ALARM:
            Oled_ShowString(0, 0, "ALARM: Break");
            break;

        case STATE_LEAK_ALARM:
            Oled_ShowString(0, 0, "ALARM: Leak!");
            break;

        default:
            Oled_ShowString(0, 0, "Unknown");
            break;
    }
}

void Display_ShowPosition(uint8_t valid, int32_t pos_mm)
{
    char buf[DISP_LINE_CHARS + 1];

    Oled_ShowString(0, 1, DISP_BLANK_LINE);

    if (valid)
    {
        /* 定位值理论上非负，但除法/取模对负数的行为会拼出坏格式，
         * 所以照样走取绝对值那条路，不假设上游一定给正数。 */
        int32_t neg = (pos_mm < 0);
        int32_t a   = neg ? -pos_mm : pos_mm;

        snprintf(buf, sizeof(buf), "Pos:%s%d.%03dm",
                 neg ? "-" : "",
                 (int)(a / 1000),
                 (int)(a % 1000));
        Oled_ShowString(0, 1, buf);
    }
    else
    {
        /* 定位算不出来最常见的原因是 r/L 还没实测标定，
         * 直接把原因写在屏上，比只显示 "---" 省一次翻文档。 */
        Oled_ShowString(0, 1, "Pos:-- calib r/L");
    }
}

/* ========== 电压显示 ========== */

void Display_ShowVoltages(const MeasVolt_t *volt, uint8_t page)
{
    char buf[DISP_LINE_CHARS + 1];
    char a[8], b[8];
    int32_t v[4];
    const char *n[4];

    Oled_ShowString(0, 2, DISP_BLANK_LINE);
    Oled_ShowString(0, 3, DISP_BLANK_LINE);

    if (page == 0)
    {
        v[0] = volt->v1;  n[0] = "V1";
        v[1] = volt->v2;  n[1] = "V2";
        v[2] = volt->v3;  n[2] = "V3";
        v[3] = volt->v4;  n[3] = "V4";
    }
    else
    {
        v[0] = volt->v5;  n[0] = "V5";
        v[1] = volt->v6;  n[1] = "V6";
        v[2] = volt->v9;  n[2] = "V9";
        v[3] = volt->v10; n[3] = "VA";   /* V10 缩写成 VA，省一列 */
    }

    FmtMv(a, sizeof(a), v[0]);
    FmtMv(b, sizeof(b), v[1]);
    snprintf(buf, sizeof(buf), "%s:%s %s:%s", n[0], a, n[1], b);
    Oled_ShowString(0, 2, buf);

    FmtMv(a, sizeof(a), v[2]);
    FmtMv(b, sizeof(b), v[3]);
    snprintf(buf, sizeof(buf), "%s:%s %s:%s", n[2], a, n[3], b);
    Oled_ShowString(0, 3, buf);
}

/* ========== 完整界面更新 ========== */

void Display_UpdateAll(const MeasResult_t *result,
                       const MeasVolt_t *volt,
                       SystemState_t state)
{
    /* 第 0 行：状态 */
    Display_ShowStatus(state, result->break_line);

    /* 第 1 行：定位结果 或 故障详情 */
    if (result->leak_detected)
    {
        /* 进水了就显示定位，算不出来时 ShowPosition 会提示要标定 r/L。
         * 这里不再要求 pos_valid 才显示 —— 否则进水时第 1 行是空的，
         * 看屏的人不知道到底在测什么。 */
        Display_ShowPosition(result->pos_valid, result->pos_mm);
    }
    else if (result->break_detected)
    {
        /* 用数值标志判断哪条线断，不比较中文字符串：
         * 源文件按 UTF-8 保存，Keil 按 GBK 解析会把中文字面量截断。 */
        uint8_t f = result->fault_flags;

        Oled_ShowString(0, 1, DISP_BLANK_LINE);

        if ((f & (uint8_t)FAULT_BREAK_YEL) && (f & (uint8_t)FAULT_BREAK_RED))
        {
            Oled_ShowString(0, 1, "Break: YEL+RED");
        }
        else if (f & (uint8_t)FAULT_BREAK_YEL)
        {
            Oled_ShowString(0, 1, "Break: YEL");
        }
        else if (f & (uint8_t)FAULT_BREAK_RED)
        {
            Oled_ShowString(0, 1, "Break: RED");
        }
        else
        {
            Oled_ShowString(0, 1, "Break: ?");
        }
    }
    else
    {
        Oled_ShowString(0, 1, DISP_BLANK_LINE);
        Oled_ShowString(0, 1, "No fault");
    }

    /* 第 2-3 行：电压，两页轮换 */
    Display_ShowVoltages(volt, s_voltage_page);
    s_voltage_page = (s_voltage_page == 0) ? 1 : 0;
}

void Display_Refresh(void)
{
    Oled_Refresh();
}
