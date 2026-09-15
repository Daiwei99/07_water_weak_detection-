#include "stm32f10x.h"
#include "Com_Board.h"
#include "Com_Delay.h"
#include "Com_Uart.h"
#include "App_Measure.h"
#include "Int_Buzzer.h"
#include <stdio.h>

/*==============================================================================
 * 阶段一：原理验证主程序
 *
 * 做的事情只有一件：按需求文档《四、电压测量总表》的顺序循环采集 V1~V10，
 * 从 UART1 打出来，同时打出断线/水浸判定和定位结果。
 *
 * 对应需求文档"阶段一：原理验证测试"，目的是回答四个问题：
 *   1) 六组开关组合下，V1~V10 的实测值是否符合预期极性和量级；
 *   2) 公式里的常数 4000 该取 4000 还是 R11 的实际值 3900；
 *   3) 线缆单位长度电阻 r 的实测值是多少（Ω/m）；
 *   4) 4066/4052 切换后到底要等多久才稳定（WIRE_SETTLE_MS 现在保守取 15ms）。
 *
 * 这四个数标定完，才有资格往阶段二的正式程序走。
 *
 *------------------------------------------------------------------------------
 * 【阶段一暂时用 UART1 而不是 RS485】
 *   需求文档说"实时通过485接口输出数据"。但 RS485 要接转换器才能看到数据，
 *   而 UART1 在 P3 排针上直接引出，插个 USB-TTL 就能看。调试阶段少一个环节
 *   少一类问题，等数据格式定下来再切到 RS485（USART3，只是换个外设）。
 *
 * 【硬件连接】
 *   USB-TTL 的 RX 接 P3 的 UART1_TX，GND 对 GND，波特率 115200-8-N-1。
 *   注意 TX/RX 是交叉接的，不是同名对接。
 *============================================================================*/

/* 每轮测量之间的间隔。采一轮约 92ms，加上这个间隔约 1.1s 一轮，
 * 便于人眼跟着看串口输出。阶段二要提速时改这里。 */
#define LOOP_INTERVAL_MS 1000U

/**
 * @brief  打印开机横幅
 * @note   把关键配置打出来，是为了在串口日志里留下"这一版程序用的什么参数"。
 *         标定时会反复改 r 和 L 重新烧写，日志里没有参数记录的话，
 *         回头看数据根本分不清哪组数据对应哪组参数。
 */
static void PrintBanner(void)
{
    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf(" 定位式水浸检测 - 阶段一 原理验证\r\n");
    printf(" MCU: STM32F103C8T6 @ %d MHz\r\n", (int)(SystemCoreClock / 1000000U));
    printf(" ADC: 内部 12bit, PA1=网络ADC0, PA2=网络ADC1\r\n");
    printf("----------------------------------------\r\n");
    printf(" 线缆参数（★标定前结果无意义）\r\n");
    printf("   r = %d.%03d Ohm/m\r\n",
           (int)Measure_GetR(),
           (int)((Measure_GetR() - (float)(int)Measure_GetR()) * 1000.0f));
    printf("   L = %d m\r\n", (int)Measure_GetL());
    printf("   取样电阻常数 = %d Ohm (原理图 R11 = 3.9k)\r\n",
           (int)MEAS_R_SAMPLE_OHM);
    printf("----------------------------------------\r\n");
    printf(" 判定阈值\r\n");
    printf("   相等容差 = %d mV\r\n", MEAS_EQUAL_TOL_MV);
    printf("   零点门限 = %d mV\r\n", MEAS_ZERO_MV);
    printf("   满幅门限 = %d mV\r\n", MEAS_FULL_MV);
    printf("   切换稳定延时 = %d ms\r\n", WIRE_SETTLE_MS);
    printf("========================================\r\n");
}

int main(void)
{
    MeasVolt_t volt;
    uint32_t round = 0;

    /* 顺序不能改：
     *   Board_Init 必须最先 —— 它关掉 JTAG，PA15/PB3/PB4 才能当普通 GPIO。
     *     漏掉这一步的现象很迷惑：GPIO 读写都"正常"，但红线供电、黄线供电、
     *     绿线接取样电阻三路对外完全没反应。
     *   Delay_Init 要在 Wire 之前 —— Wire_Apply 里的 Delay_ms 依赖 SysTick。
     *   Uart_Init 尽量早 —— 后面所有排错都靠它。
     *   Measure_Init 内部会调 Wire_Init 和 Adc_Init。 */
    Board_Init();
    Delay_Init();
    Uart_Init();
    Measure_Init();
    Buzzer_Init();

    PrintBanner();

    /* 上电自检：短鸣一声。
     * 这一声的作用是把"蜂鸣器硬件是否正常"和"报警逻辑是否正确"分开 ——
     * 听不见就先查硬件（见 Int_Buzzer.h 里关于 R9 的疑点），
     * 不必在报警逻辑里瞎找。用阻塞方式是因为此刻还没进主循环。 */
    printf("自检: 蜂鸣器短鸣 100ms ...\r\n");
    Buzzer_BlockingBeep(100);

    while (1)
    {
        round++;
        printf("\r\n===== 第 %d 轮 (t=%d ms) =====\r\n",
               (int)round, (int)Delay_GetTick());

        /* 采完这一函数返回时 4066 已全部断开，线缆回到静止态 */
        Measure_ReadAll(&volt);

        Measure_PrintVolt(&volt);
        Measure_PrintResult(&volt);

        Delay_ms(LOOP_INTERVAL_MS);
    }
}
