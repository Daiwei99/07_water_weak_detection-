#include "Int_Buzzer.h"
#include "Com_Board.h"
#include "Com_Delay.h"

/*==============================================================================
 * 为什么选 TIM3
 *
 *   TIM1 是高级定时器，留给后面可能的电机/PWM 场合；
 *   TIM2 的重映射引脚和本板已用的 PA1/PA2/PA15/PB3/PB10/PB11 大面积重叠，
 *        虽然这里只用它的中断不用引脚，但占着容易将来打架；
 *   TIM4 留给后面做扫描节拍或软件 I2C 的时基。
 *   TIM3 在本工程里没有别的用途，中断向量也没被 stm32f10x_it.c 占用。
 *
 * 频率算法：要 2700Hz 方波，就得每半个周期翻转一次电平，
 *           所以中断频率 = 2 × 2700 = 5400Hz。
 *
 *   TIM3 挂在 APB1。APB1 = 36MHz，但定时器时钟有个容易忽略的规则：
 *   当 APB 预分频 != 1 时，定时器时钟 = APB 时钟 × 2。
 *   本工程 SYSCLK=72MHz、APB1 预分频=2，所以 TIM3CLK = 36 × 2 = 72MHz。
 *
 *   取预分频 71（即除以 72）得到 1MHz 计数时钟，1 个计数 = 1µs：
 *       ARR = 1000000 / 5400 - 1 = 185.18 - 1 -> 184
 *   实际中断频率 = 1000000 / 185 = 5405.4Hz -> 方波 2702.7Hz，
 *   偏差 +0.1%，远小于蜂鸣器自身的共振带宽，听不出来。
 *============================================================================*/
#define BUZZER_TIM              TIM3
#define BUZZER_TIM_RCC          RCC_APB1Periph_TIM3
#define BUZZER_TIM_IRQn         TIM3_IRQn

/* 1MHz 计数时钟：72MHz / (71+1) = 1MHz，1 个计数 = 1µs */
#define BUZZER_TIM_PRESCALER    (72U - 1U)
#define BUZZER_TIM_CLK_HZ       1000000U

/* 中断频率 = 2 × 共振频率（每半周期翻转一次） */
#define BUZZER_TOGGLE_HZ        (BUZZER_FREQ_HZ * 2U)
#define BUZZER_TIM_PERIOD       ((BUZZER_TIM_CLK_HZ / BUZZER_TOGGLE_HZ) - 1U)

/*------------------------------------------------------------------------------
 * 节拍表：每种提示音的"响多久 / 停多久"，单位 ms
 *
 * on_ms = 0 表示静音，off_ms = 0 表示长鸣不停。
 * 索引与 BuzzerPattern_t 严格对应，改枚举时这张表要跟着改。
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint16_t on_ms;
    uint16_t off_ms;
} BuzzerBeat_t;

static const BuzzerBeat_t s_beat[] =
{
    {   0,    0 },     /* OFF   : 静音                        */
    { 100,    0 },     /* BEEP  : 响一声就停，见下面的单次逻辑 */
    { 200,  200 },     /* LEAK  : 急促，听着就紧张             */
    { 500, 1500 },     /* BREAK : 缓慢，和水浸区分开           */
    {   0,    0 }      /* ON    : 长鸣，特殊处理               */
};

static volatile BuzzerPattern_t s_pattern   = BUZZER_PATTERN_OFF;
static volatile uint8_t         s_sounding  = 0;   /* 当前是否在发声   */
static uint32_t                 s_phase_ts  = 0;   /* 本拍开始的时间戳 */
static uint8_t                  s_beep_done = 0;   /* BEEP 单次已完成  */

/**
 * @brief  初始化 PA4 和 TIM3
 * @note   PA4 配推挽输出。这里不用复用推挽，因为 PA4 上没有 TIM 通道，
 *         电平是靠中断里软件翻转的，对 GPIO 来说就是普通输出。
 *
 *         TIM3 配好但不启动（TIM_Cmd 保持 DISABLE），中断使能也留到
 *         Buzzer_Start() 里开。这样初始化完是确定的静音状态，
 *         不会因为上电瞬间的随机状态叫一声。
 *
 *         中断优先级设 2（低于默认的 0）：蜂鸣器只是提示，
 *         比 ADC 采样和串口更不重要，不该抢它们的时间。
 */
void Buzzer_Init(void)
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef        nvic;

    RCC_APB2PeriphClockCmd(BELL_RCC, ENABLE);
    RCC_APB1PeriphClockCmd(BUZZER_TIM_RCC, ENABLE);

    /* PA4 推挽输出，先拉低确保静音 */
    gpio.GPIO_Pin   = BELL_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BELL_PORT, &gpio);
    GPIO_WriteBit(BELL_PORT, BELL_PIN, Bit_RESET);

    tim.TIM_Prescaler         = BUZZER_TIM_PRESCALER;
    tim.TIM_CounterMode       = TIM_CounterMode_Up;
    tim.TIM_Period            = BUZZER_TIM_PERIOD;
    tim.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(BUZZER_TIM, &tim);

    /* TIM_TimeBaseInit 会产生一次更新事件，先清掉，
     * 否则 Buzzer_Start() 开中断的瞬间会立刻进一次中断 */
    TIM_ClearITPendingBit(BUZZER_TIM, TIM_IT_Update);

    nvic.NVIC_IRQChannel                   = BUZZER_TIM_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    s_pattern   = BUZZER_PATTERN_OFF;
    s_sounding  = 0;
    s_phase_ts  = Delay_GetTick();
    s_beep_done = 0;
}

/**
 * @brief  TIM3 中断服务：翻转 PA4
 * @note   这个函数每秒执行 5400 次，所以只做一件事：读回当前电平并取反。
 *         用 GPIO_ReadOutputDataBit 读的是输出寄存器（ODR）而不是引脚
 *         实际电平（IDR）—— 读 ODR 更快也更可靠，不受外部负载影响。
 *
 *         没有定义在 stm32f10x_it.c 里，不会重复定义（已核对该文件只有
 *         内核异常处理函数，没有任何外设 IRQHandler）。
 */
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(BUZZER_TIM, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(BUZZER_TIM, TIM_IT_Update);

        if (GPIO_ReadOutputDataBit(BELL_PORT, BELL_PIN) != 0)
        {
            GPIO_WriteBit(BELL_PORT, BELL_PIN, Bit_RESET);
        }
        else
        {
            GPIO_WriteBit(BELL_PORT, BELL_PIN, Bit_SET);
        }
    }
}

/**
 * @brief  开始发声
 * @note   幂等：已经在响的时候再调一次不会有副作用，也不会打断当前波形。
 */
void Buzzer_Start(void)
{
    if (s_sounding) { return; }

    TIM_ClearITPendingBit(BUZZER_TIM, TIM_IT_Update);
    TIM_ITConfig(BUZZER_TIM, TIM_IT_Update, ENABLE);
    TIM_Cmd(BUZZER_TIM, ENABLE);

    s_sounding = 1;
}

/**
 * @brief  停止发声
 * @note   必须把 PA4 拉低。如果停在高电平，Q1 一直导通，16Ω 线圈上持续
 *         跑约 300mA —— 不发声、白耗电、线圈发热，是那种"看起来没问题
 *         但摸上去烫手"的隐性故障。
 */
void Buzzer_Stop(void)
{
    TIM_Cmd(BUZZER_TIM, DISABLE);
    TIM_ITConfig(BUZZER_TIM, TIM_IT_Update, DISABLE);

    GPIO_WriteBit(BELL_PORT, BELL_PIN, Bit_RESET);

    s_sounding = 0;
}

void Buzzer_SetPattern(BuzzerPattern_t pattern)
{
    if (pattern == s_pattern) { return; }       /* 同一节奏不重置节拍 */

    s_pattern   = pattern;
    s_phase_ts  = Delay_GetTick();
    s_beep_done = 0;

    if (pattern == BUZZER_PATTERN_OFF)
    {
        Buzzer_Stop();
    }
    else
    {
        /* 新节奏立即从"响"开始，不等下一拍 */
        Buzzer_Start();
    }
}

/**
 * @brief  推进节拍
 * @note   靠 Delay_GetTick() 的时间戳比较，不占定时器。
 *         Delay_IsTimeout() 内部用无符号相减，49 天回绕也不会误判。
 *
 *         四种情况分开处理：
 *           OFF  - 确保静音
 *           ON   - 确保长鸣
 *           BEEP - 响一次就转 OFF，靠 s_beep_done 防止重复触发
 *           其余 - 按 on_ms / off_ms 交替
 */
void Buzzer_Poll(void)
{
    const BuzzerBeat_t *beat;

    if (s_pattern == BUZZER_PATTERN_OFF)
    {
        if (s_sounding) { Buzzer_Stop(); }
        return;
    }

    if (s_pattern == BUZZER_PATTERN_ON)
    {
        if (!s_sounding) { Buzzer_Start(); }
        return;
    }

    beat = &s_beat[(uint8_t)s_pattern];

    /* BEEP：单次，响完就静音 */
    if (s_pattern == BUZZER_PATTERN_BEEP)
    {
        if (s_beep_done) { return; }

        if (s_sounding && Delay_IsTimeout(s_phase_ts, beat->on_ms))
        {
            Buzzer_Stop();
            s_beep_done = 1;
            s_pattern   = BUZZER_PATTERN_OFF;
        }
        return;
    }

    /* 循环节奏：响 on_ms -> 停 off_ms -> 再响 */
    if (s_sounding)
    {
        if (Delay_IsTimeout(s_phase_ts, beat->on_ms))
        {
            Buzzer_Stop();
            s_phase_ts = Delay_GetTick();
        }
    }
    else
    {
        if (Delay_IsTimeout(s_phase_ts, beat->off_ms))
        {
            Buzzer_Start();
            s_phase_ts = Delay_GetTick();
        }
    }
}

/**
 * @brief  阻塞鸣叫
 * @note   自检用。不依赖主循环，逻辑最短，适合"上电响一声证明硬件是活的"。
 *         报警场景别用这个 —— 阻塞期间测量停了。
 */
void Buzzer_BlockingBeep(uint32_t ms)
{
    Buzzer_Start();
    Delay_ms(ms);
    Buzzer_Stop();

    /* 恢复到静音状态，避免 Buzzer_Poll() 之后又把它打开 */
    s_pattern   = BUZZER_PATTERN_OFF;
    s_beep_done = 0;
}
