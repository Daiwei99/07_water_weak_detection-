#include "Com_Delay.h"
#include "Int_Buzzer.h"

static volatile uint32_t s_tick_ms = 0;
static volatile uint8_t  s_running = 0;

/**
 * @brief  SysTick 配置为 1ms 中断
 * @note   SystemCoreClock 由 system_stm32f10x.c 里的 SYSCLK_FREQ_72MHz 决定
 *         （外部 8MHz 晶振 X2 倍频到 72MHz）。
 */
void Delay_Init(void)
{
    s_tick_ms = 0;
    /* SysTick_Config 来自 CMSIS core_cm3.h，同时使能中断和计数 */
    if (SysTick_Config(SystemCoreClock / 1000U) == 0U)
    {
        s_running = 1;
    }
}

void SysTick_Handler(void)
{
    s_tick_ms++;

    /* 蜂鸣器节拍推进放在这里，不放主循环。
     *
     * 原因：带 OLED 的主循环一轮要 600ms 以上（一轮完整采集约 550ms，
     * 加上刷屏和 Delay_ms），而 LEAK 节拍是 200ms 响 / 200ms 停。
     * 主循环调用 Buzzer_Poll() 追不上 200ms 的拍子，每次进去都发现
     * 早该切换，于是发声时间被压缩到一个主循环周期以内，听不出声。
     *
     * 放在 1ms 中断里，节拍精度与主循环彻底解耦。Buzzer_Poll() 本身
     * 只做几个整数比较和 GPIO/TIM 操作，开销可以忽略。 */
    Buzzer_Poll();
}

uint32_t Delay_GetTick(void)
{
    return s_tick_ms;
}

uint8_t Delay_IsTimeout(uint32_t start, uint32_t ms)
{
    /* 无符号相减天然处理回绕 */
    return ((uint32_t)(s_tick_ms - start) >= ms) ? 1U : 0U;
}

/**
 * @brief  毫秒延时
 * @note   依赖 SysTick 中断。若 Delay_Init() 没调用过就会退化成忙等循环，
 *         避免死锁在这里。
 */
void Delay_ms(uint32_t ms)
{
    if (s_running)
    {
        uint32_t start = s_tick_ms;
        while ((uint32_t)(s_tick_ms - start) < ms)
        {
            /* 等 */
        }
    }
    else
    {
        while (ms--)
        {
            Delay_us(1000U);
        }
    }
}

/**
 * @brief  微秒级延时（软件循环，粗略）
 * @note   @72MHz 下每次循环约 6 个时钟周期，故 us * 12 约等于目标时间。
 *         这是个近似值，只用于"等硬件稳定"这类不需要精确定时的场合。
 *         真要精确定时请改用 TIM。
 */
void Delay_us(uint32_t us)
{
    volatile uint32_t n = us * 12U;
    while (n--)
    {
        __NOP();
    }
}
