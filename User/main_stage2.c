#include "stm32f10x.h"
#include "Com_Board.h"
#include "Com_Delay.h"
#include "Com_Uart.h"
#include "Int_Buzzer.h"
#include "Int_Relay.h"
#include "Int_Rs485.h"
#include "App_Measure.h"
#include "App_State.h"
#include <stdio.h>

/*==============================================================================
 * 阶段二：正式运行主程序
 *
 * 与阶段一的区别：
 *   - 不再打印 V1~V10 详细电压表（工程现场没人盯着看串口）
 *   - 加入状态机：正常监测 -> 故障报警 -> 恢复正常
 *   - 报警联动：蜂鸣器 + 继电器 + RS485 输出
 *   - 去抖：连续 N 次采样结果一致才切换状态
 *
 * UART1 仍然保留作为调试口，只打关键事件（进入/退出报警）。
 * RS485 输出 JSON 格式状态帧，供上位机解析。
 *============================================================================*/

/**
 * @brief  打印开机横幅
 */
static void PrintBanner(void)
{
    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf(" 定位式水浸检测 - 阶段二 正式运行\r\n");
    printf(" MCU: STM32F103C8T6 @ %d MHz\r\n", (int)(SystemCoreClock / 1000000U));
    printf("========================================\r\n");
    printf(" 线缆参数\r\n");
    printf("   r = %d.%03d Ohm/m\r\n",
           (int)Measure_GetR(),
           (int)((Measure_GetR() - (float)(int)Measure_GetR()) * 1000.0f));
    printf("   L = %d m\r\n", (int)Measure_GetL());
    printf("========================================\r\n");
    printf("系统进入正常监测状态\r\n");
}

int main(void)
{
    /* 初始化顺序：Board -> Delay -> Uart -> 外设 -> 应用层 */
    Board_Init();
    Delay_Init();
    Uart_Init();
    Buzzer_Init();
    Relay_Init();
    Rs485_Init();
    Measure_Init();
    State_Init();

    PrintBanner();

    /* 上电自检：短鸣 100ms */
    printf("自检: 蜂鸣器短鸣\r\n");
    Buzzer_BlockingBeep(100);

    while (1)
    {
        /* 状态机主循环 */
        State_Poll();

        /* 蜂鸣器节拍推进（必须调用，否则蜂鸣器不响） */
        Buzzer_Poll();
    }
}
