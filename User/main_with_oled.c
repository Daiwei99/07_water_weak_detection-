/**
 ******************************************************************************
 * @file    main_with_oled.c
 * @brief   阶段二主程序（带 OLED 显示，不使用按键）
 *
 * 功能：
 *   1. 循环采集 V1~V10，做断线/水浸判定与定位
 *   2. 状态机（去抖 + 自动联动蜂鸣器/继电器/RS485）
 *   3. OLED 实时显示
 *
 * 显示内容：
 *   行0: 系统状态（OK / ALARM: Break / ALARM: Leak）
 *   行1: 定位距离 或 故障线路
 *   行2-3: 电压 V1~V10（自动轮换两页）
 *
 * 【为什么调试打印全用英文】
 *   Keil armcc 在 GBK 代码页下处理源文件里的中文字面量时，单个汉字容易被
 *   截断成非法多字节序列，"关" 这类字的尾字节会把结束引号一起吞掉，
 *   直接报 error #8: missing closing quote。而且串口助手默认按 GBK 解码，
 *   源文件是 UTF-8 保存时打出来必然是乱码。调试信息是给开发者看的，
 *   用英文既躲开编码问题，也不影响任何功能。OLED 上的中文走字库，
 *   和这里无关。
 *
 * 【本版不使用按键】
 *   板上没焊按键，因此不调用 Key_Init()。这一点是刻意的：
 *   Int_Key.c 把"下键"定义在 PB0，Key_Init() 会把 PB0 配成上拉输入。
 *   OLED 的 SCL 究竟在 PB1 还是 PB0 目前尚有存疑（见 Com_Board.h 注释），
 *   万一是 PB0，Key_Init() 会把时钟线改成输入、把屏幕搞哑。
 *   不碰按键就没有这个风险。要用菜单时改挂 main_with_menu.c。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Com_Board.h"
#include "Com_Delay.h"
#include "Com_Uart.h"
#include "Int_Buzzer.h"
#include "Int_Relay.h"
#include "Int_Rs485.h"
#include "App_Measure.h"
#include "App_State.h"
#include "App_Config.h"
#include "App_Display.h"
#include <stdio.h>

/* 放静态区而不是栈上：MeasVolt_t + MeasResult_t 约 50 字节，
 * 主循环每轮都要用，放静态区省掉反复的栈操作，也便于调试时观察。 */
static MeasVolt_t   g_volt;
static MeasResult_t g_result;

/* 显示刷新间隔。OLED 刷全屏是软件 I2C 逐字节发的，比较慢，
 * 刷太勤会拖慢主循环，500ms 对人眼够用了。 */
#define DISPLAY_INTERVAL_MS   500U

/* 主循环节拍。Measure_ReadAll() 本身约 92ms（6 次通道切换，每次等 15ms
 * 稳定），再加这个延时约 200ms 一轮。 */
#define LOOP_INTERVAL_MS      100U

/**
 * @brief  打印开机横幅和当前参数
 * @note   参数从 Flash 读出来打一遍，是为了确认掉电保存真的生效了 ——
 *         改完参数重启，这里应该显示改后的值。
 */
static void PrintBanner(void)
{
    const Config_t *cfg = Config_Get();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf(" Water Leak Detector V2.0 (OLED)\r\n");
    printf(" MCU: STM32F103C8T6 @ %d MHz\r\n",
           (int)(SystemCoreClock / 1000000U));
    printf("----------------------------------------\r\n");
    printf(" Config (from Flash):\r\n");
    printf("   r = %d.%03d Ohm/m\r\n",
           (int)(cfg->r_mOhm_per_m / 1000), (int)(cfg->r_mOhm_per_m % 1000));
    printf("   L = %d.%03d m\r\n",
           (int)(cfg->length_mm / 1000), (int)(cfg->length_mm % 1000));
    printf("   equal tolerance = %d mV\r\n", (int)cfg->equal_tol_mv);
    printf("   zero threshold  = %d mV\r\n", (int)cfg->zero_mv);
    printf("   full threshold  = %d mV\r\n", (int)cfg->full_mv);
    printf("   buzzer = %s\r\n", cfg->buzzer_enable ? "ON" : "OFF");
    printf("   relay  = %s\r\n", cfg->relay_enable  ? "ON" : "OFF");
    printf("========================================\r\n");
}

int main(void)
{
    uint32_t tick_last_display = 0;

    /* 初始化顺序不能乱：
     *   Board_Init  必须最先 —— 它关掉 JTAG，PA15/PB3/PB4 才能当普通 GPIO 用。
     *               漏了这一步，红线供电、黄线供电、绿线接 R9 全都没反应。
     *   Delay_Init  要在任何用到延时的模块之前 —— Wire_Apply 和 OLED 的
     *               软件 I2C 都依赖 SysTick。
     *   Uart_Init   要在第一个 printf 之前。
     *   Measure_Init 内部会调 Wire_Init 和 Adc_Init，不用单独调那两个。 */
    Board_Init();
    Delay_Init();
    Uart_Init();

    Buzzer_Init();
    Relay_Init();
    Rs485_Init();

    Config_Init();       /* 从 Flash 读参数，必须在 PrintBanner 之前 */
    Measure_Init();      /* 内含 Wire_Init + Adc_Init */
    State_Init();
    Display_Init();      /* 内含 Oled_Init，会显示开机画面 */

    PrintBanner();

    /* 上电自检：蜂鸣器短鸣一声。
     *
     * 这一声是排查蜂鸣器故障的分界线，故意用阻塞式：它不依赖主循环、
     * 不依赖状态机、不依赖 Buzzer_Poll()。
     *   响了   -> 蜂鸣器硬件、极性、TIM3、GPIO 全部正常，
     *             后面报警不响就只能是状态机的问题。
     *   不响   -> 直接查硬件，别在软件里找。重点看 Int_Buzzer.h 里记的
     *             那个疑点：BUZZER1 正端经 R9(10k) 上拉到 5V，10k 串在
     *             16Ω 线圈的供电路径上，线圈只能分到约 8mV，物理上发不出声。 */
    printf("Self-test: buzzer beep 100ms ...\r\n");
    Buzzer_BlockingBeep(100);

    Delay_ms(500);       /* 让开机画面停留一会儿再进监测界面 */

    while (1)
    {
        /* 1. 采集 V1~V10 并判定 */
        Measure_ReadAll(&g_volt);
        Measure_Judge(&g_volt, &g_result);

        /* 2. 状态机更新。去抖、报警联动（蜂鸣器/继电器/RS485）都在里面。
         *    注意用 State_Update() 而不是 State_Poll() —— 后者会自己再采
         *    一遍 V1~V10，那样每轮要多花 92ms。 */
        State_Update(&g_result);

        /* 3. 推进蜂鸣器节拍。必须每轮都调，否则断线/水浸的间歇鸣叫
         *    不会切换（长鸣不停或者一直哑）。 */
        Buzzer_Poll();

        /* 4. 刷屏 + 串口简报 */
        if (Delay_GetTick() - tick_last_display >= DISPLAY_INTERVAL_MS)
        {
            SystemState_t st = State_Get();

            tick_last_display = Delay_GetTick();

            Display_UpdateAll(&g_result, &g_volt, st);
            Display_Refresh();

            printf("[%s] ",
                   (st == STATE_NORMAL)      ? "OK"    :
                   (st == STATE_BREAK_ALARM) ? "BREAK" : "LEAK");

            if (g_result.break_detected)
            {
                /* 用 fault_flags 而不是 break_line 那个中文字符串，
                 * 免得又碰上编码问题。两条线同时断时都会打出来。 */
                printf("break:");
                if (g_result.fault_flags & (uint8_t)FAULT_BREAK_YEL)
                {
                    printf(" YEL");
                }
                if (g_result.fault_flags & (uint8_t)FAULT_BREAK_RED)
                {
                    printf(" RED");
                }
                printf("  ");
            }

            if (g_result.leak_detected)
            {
                printf("leak");
                if (g_result.pos_valid)
                {
                    printf(" @ %d.%03d m",
                           (int)(g_result.pos_mm / 1000),
                           (int)(g_result.pos_mm % 1000));
                }
                else
                {
                    /* 定位算不出来不代表没进水 —— r/L 未标定时
                     * Measure_CalcL1() 几乎总是失败。报警照样要给。 */
                    printf(" @ ---(need r/L calibration)");
                }
                printf("  ");
            }

            printf("\r\n");
        }

        Delay_ms(LOOP_INTERVAL_MS);
    }
}
