/**
 ******************************************************************************
 * @file    main_with_menu.c
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   阶段二主程序（带OLED + 按键菜单）
 *
 * 功能：
 *   - 状态机 + 报警联动
 *   - OLED实时显示
 *   - 3键菜单系统（查看/修改参数）
 *
 * 编译配置：
 *   Keil Define: USE_STDPERIPH_DRIVER,STM32F10X_MD
 *   Use MicroLIB: 必须勾选
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Com_Board.h"
#include "Com_Delay.h"
#include "Com_Uart.h"
#include "Int_Adc.h"
#include "Int_Wire.h"
#include "Int_Buzzer.h"
#include "Int_Relay.h"
#include "Int_Rs485.h"
#include "Int_Oled.h"
#include "Int_Key.h"
#include "App_Measure.h"
#include "App_State.h"
#include "App_Config.h"
#include "App_Display.h"
#include "App_Menu.h"
#include <stdio.h>

/* ========== 主程序 ========== */

int main(void)
{
    MeasVolt_t volt;
    MeasResult_t result;
    uint32_t last_scan_ms = 0;
    uint32_t last_key_ms = 0;
    uint32_t last_display_ms = 0;

    // 1. 板级初始化（必须第一个调用，关闭JTAG）
    Board_Init();

    // 2. 延时时基初始化
    Delay_Init();

    // 3. 串口初始化
    Uart_Init();
    printf("\r\n====================================\r\n");
    printf("  水浸检测控制器 V2.0 (菜单版)\r\n");
    printf("  MCU: STM32F103C8T6 @ 72MHz\r\n");
    printf("====================================\r\n");

    // 4. ADC初始化
    Adc_Init();

    // 5. 4066/4052通路控制初始化
    Wire_Init();

    // 6. 蜂鸣器初始化（上电自检）
    Buzzer_Init();
    Buzzer_Beep(150);  // 上电提示音
    Delay_ms(200);

    // 7. 继电器初始化
    Relay_Init();

    // 8. RS485初始化
    Rs485_Init();

    // 9. OLED初始化
    Oled_Init();
    Oled_Clear();
    Oled_ShowString(0, 0, "Water Detector");
    Oled_ShowString(0, 1, "V2.0 Menu");
    Oled_ShowString(0, 2, "Initializing...");
    Oled_Refresh();
    Delay_ms(500);

    // 10. 按键初始化
    Key_Init();

    // 11. 测量模块初始化
    Measure_Init();

    // 12. 状态机初始化
    State_Init();

    // 13. Flash参数加载
    Config_Init();
    printf("参数加载完成\r\n");

    // 14. 显示模块初始化
    Display_Init();

    // 15. 菜单系统初始化
    Menu_Init();

    printf("系统初始化完成，开始运行\r\n\r\n");

    // ========== 主循环 ==========
    while (1)
    {
        uint32_t now = Delay_GetTick();

        // 1. 按键扫描（每10ms一次）
        if (now - last_key_ms >= 10)
        {
            last_key_ms = now;
            Key_Scan();
        }

        // 2. 菜单逻辑处理
        if (Menu_Process())
        {
            // 菜单状态变化，立即刷新显示
            Menu_Display();
        }

        // 3. 状态机扫描（每100ms一次，仅在监测界面）
        if (Menu_IsMonitoring() && (now - last_scan_ms >= 100))
        {
            last_scan_ms = now;

            // 采集V1~V10
            Measure_ReadAll(&volt);

            // 综合判定
            Measure_Judge(&volt, &result);

            // 状态机更新（自动联动蜂鸣器、继电器、RS485）
            State_Update(&result);

            // 更新监测界面数据
            Display_Update(&volt, &result);
        }

        // 4. 显示刷新（每500ms一次，仅在监测界面）
        if (Menu_IsMonitoring() && (now - last_display_ms >= 500))
        {
            last_display_ms = now;
            Display_Refresh();
        }

        // 5. 蜂鸣器节拍推进
        Buzzer_Poll();
    }
}
