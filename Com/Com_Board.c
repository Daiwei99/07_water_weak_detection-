#include "Com_Board.h"

/**
 * @brief  板级初始化：开 GPIO 时钟 + 释放 JTAG 引脚
 * @note   必须最先调用。
 *
 *         关键一步是 GPIO_Remap_SWJ_JTAGDisable：
 *         PA15/PB3/PB4 上电默认是 JTDI/JTDO/JNTRST，被调试口占着。
 *         本板走 SWD（PA13 SWDIO / PA14 SWCLK，见 P3 排针），
 *         JTAG 那三根用不上，必须显式关掉才能当普通 GPIO 用。
 *         这里只关 JTAG、保留 SW，所以关掉之后 SWD 下载调试照常可用。
 */
void Board_Init(void)
{
    /* 复用功能时钟必须先开，否则 GPIO_PinRemapConfig 写不进去 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* 开本工程用到的 GPIO 端口时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);

    /* 关 JTAG、留 SWD —— 释放 PA15 / PB3 / PB4 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
}
