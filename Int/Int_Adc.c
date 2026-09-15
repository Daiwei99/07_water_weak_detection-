#include "Int_Adc.h"
#include "Com_Board.h"

/*==============================================================================
 * 采样时间为什么必须取最长档 ADC_SampleTime_239Cycles5
 *
 * STM32F103 的 ADC 是电荷再分配型，采样时把内部 12.5pF 的保持电容充到输入
 * 电压。手册规定源阻抗 R_AIN 和采样时间要匹配，否则电容没充满就开始转换，
 * 读数会偏低。
 *
 * 本板的源阻抗很高，逐项加起来（数字来自手册，不是估的）：
 *     取样电阻 R11                      3900Ω
 *     4052 每路输入串的保护电阻 R22~R28  2000Ω
 *     4052 通道导通电阻 RON             见下
 *
 * HEF4052B 手册 Table 7 给的 RON(rail) 是 VDD-VEE = 5V 下典型 115Ω、
 * 最大 340Ω。但本板 U7 的 VDD 接的是 3V3（原理图 pin 16 -> 3V3），
 * 低于手册最低的 5V 测试点 —— CMOS 模拟开关的 RON 随供电降低明显上升，
 * 3.3V 下比 340Ω 更差，手册没给这一档的数，保守按 700Ω 估。
 *
 * 合计最坏约 3900 + 2000 + 700 ≈ 6.6kΩ。
 * F103 数据手册给的对应关系是：12bit 下 R_AIN ≤ 50kΩ 需要 239.5 周期。
 * ADCCLK = 72MHz / 6 = 12MHz，239.5 周期 ≈ 20µs，一次转换约 21µs。
 *
 * 取快档（比如 1.5 周期）在这块板上会直接读出偏低的电压，而且偏低的幅度
 * 随通道阻抗变化 —— 表现成"定位公式怎么标都标不准"，很难往采样时间上想。
 *============================================================================*/
#define ADC_SAMPLE_TIME     ADC_SampleTime_239Cycles5

/**
 * @brief  ADC1 初始化：单次转换、软件触发、无 DMA
 * @note   用最朴素的"设通道 -> 启动 -> 等标志 -> 读值"模式。
 *         本应用的采样节奏是几十毫秒一次，DMA 和扫描模式没有收益，
 *         反而让"当前采的是哪根线"这件事变得不好推理。
 *
 *         ADCCLK 最高 14MHz，72MHz 只能 /6 得 12MHz（/4 是 18MHz，超了）。
 */
void Adc_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef  adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    /* ADCCLK = PCLK2 / 6 = 72MHz / 6 = 12MHz，在 14MHz 上限内 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* PA1 / PA2 设为模拟输入。模拟输入模式下施密特触发器关闭、
     * 上下拉断开，引脚对外表现为高阻，不会给 4052 的输出加负载。 */
    gpio.GPIO_Pin  = NET_ADC0_PIN | NET_ADC1_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;                    /* 单通道，不扫描 */
    adc.ADC_ContinuousConvMode = DISABLE;                    /* 单次，软件触发 */
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);

    /* 上电后必须做一次校准，消除内部电容的失调误差。
     * 顺序固定：先复位校准寄存器，等它自清；再启动校准，等它自清。 */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) == SET)
    {
        /* 等复位校准完成 */
    }

    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) == SET)
    {
        /* 等校准完成 */
    }
}

/**
 * @brief  读一次指定通道的原始码值
 * @param  channel ADC_Channel_1（网络 ADC0）或 ADC_Channel_2（网络 ADC1）
 * @return 0~4095
 */
static uint16_t Adc_ReadOnce(uint8_t channel)
{
    /* 规则组第 1 个位置放目标通道。每次都重设，因为两个通道轮换用。 */
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SAMPLE_TIME);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        /* 等转换结束。约 21µs，忙等比开中断简单且够快。 */
    }

    /* 读 DR 会自动清 EOC 标志，不用手动清 */
    return ADC_GetConversionValue(ADC1);
}

/**
 * @brief  多次采样 + 去极值平均
 * @note   去掉一个最大和一个最小再平均，比纯平均更能压住偶发尖峰。
 *         这里的噪声来源主要是继电器/蜂鸣器动作时的电源扰动，
 *         那种扰动是脉冲状的，正好被去极值吃掉。
 *
 *         用 uint32_t 累加：9 × 4095 = 36855，uint16_t 会溢出。
 */
static uint16_t Adc_ReadFiltered(uint8_t channel)
{
    uint32_t sum = 0;
    uint16_t min = 0xFFFFU;
    uint16_t max = 0;
    uint8_t  i;

    for (i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        uint16_t v = Adc_ReadOnce(channel);

        sum += v;
        if (v < min) { min = v; }
        if (v > max) { max = v; }
    }

    sum -= min;
    sum -= max;

    return (uint16_t)(sum / (ADC_SAMPLE_COUNT - 2));
}

uint16_t Adc_ReadNet0(void)
{
    return Adc_ReadFiltered(NET_ADC0_CHANNEL);
}

uint16_t Adc_ReadNet1(void)
{
    return Adc_ReadFiltered(NET_ADC1_CHANNEL);
}

/**
 * @brief  码值转电压
 * @note   基准是 VDDA = 3.3V。这里用的是标称值，实际 AMS1117 输出有 ±1.5%
 *         误差，会直接体现为所有电压读数的比例偏差。
 *         定位公式里 VA1B1/(VA1B1-3.3) 对基准误差敏感，阶段一标定时如果发现
 *         系统性偏差，先用万用表量一下真实的 3V3，把这个宏改成实测值。
 */
float Adc_RawToVolt(uint16_t raw)
{
    return ((float)raw / ADC_FULL_SCALE) * ADC_VREF_VOLT;
}

float Adc_ReadNet0Volt(void)
{
    return Adc_RawToVolt(Adc_ReadNet0());
}

float Adc_ReadNet1Volt(void)
{
    return Adc_RawToVolt(Adc_ReadNet1());
}
