#include "Int_Wire.h"
#include "Com_Board.h"
#include "Com_Delay.h"

/**
 * @brief  初始化 4066 使能引脚和 4052 地址引脚，全部推挽输出
 * @note   前置条件：必须先调用 Board_Init()，它关掉了 JTAG 才能用
 *         PA15 / PB3 / PB4。GPIO 时钟也在 Board_Init() 里开好了，
 *         这里再开一次是幂等的，只为让本文件单独看也能自洽。
 *
 *         初始化完成后所有通路断开、4052 停在通道 0。这是个安全的静止态：
 *         线缆上没有电压，不会因为上电瞬间的随机电平在被测线缆上乱加激励。
 */
void Wire_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* GPIOB 上的五根：PB3 黄线供电、PB4 红线供电、PB5 黑线接 R11、
     *                 PB6/PB7 = 4052 的 S1/S2 */
    gpio.GPIO_Pin = YEL_PWR_PIN | RED_PWR_PIN | BLK_R9_PIN |
                    SW_S1_PIN   | SW_S2_PIN;
    GPIO_Init(GPIOB, &gpio);

    /* GPIOA 上的一根：PA15 绿线接 R11 */
    gpio.GPIO_Pin = GRN_R9_PIN;
    GPIO_Init(GRN_R9_PORT, &gpio);

    Wire_AllOff();
    Wire_SelectPath(WIRE_PATH_YELLOW_BLACK);
}

void Wire_SetRedPower(uint8_t on)
{
    GPIO_WriteBit(RED_PWR_PORT, RED_PWR_PIN, on ? Bit_SET : Bit_RESET);
}

void Wire_SetYellowPower(uint8_t on)
{
    GPIO_WriteBit(YEL_PWR_PORT, YEL_PWR_PIN, on ? Bit_SET : Bit_RESET);
}

void Wire_SetGreenToR9(uint8_t on)
{
    GPIO_WriteBit(GRN_R9_PORT, GRN_R9_PIN, on ? Bit_SET : Bit_RESET);
}

void Wire_SetBlackToR9(uint8_t on)
{
    GPIO_WriteBit(BLK_R9_PORT, BLK_R9_PIN, on ? Bit_SET : Bit_RESET);
}

void Wire_SetSwitches(uint8_t red, uint8_t yellow, uint8_t green, uint8_t black)
{
    Wire_SetRedPower(red);
    Wire_SetYellowPower(yellow);
    Wire_SetGreenToR9(green);
    Wire_SetBlackToR9(black);
}

void Wire_AllOff(void)
{
    Wire_SetSwitches(0, 0, 0, 0);
}

/**
 * @brief  设置 4052 地址线
 * @param  path 通道号 0~3，见 WirePath_t 的对应表
 * @note   通道号的 bit0 -> S1(PB6)，bit1 -> S2(PB7)。
 *         两个 bank 共用地址线，所以一次调用同时改变 ADC0 和 ADC1 的来源。
 */
void Wire_SelectPath(WirePath_t path)
{
    GPIO_WriteBit(SW_S1_PORT, SW_S1_PIN,
                  ((uint8_t)path & 0x01U) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(SW_S2_PORT, SW_S2_PIN,
                  ((uint8_t)path & 0x02U) ? Bit_SET : Bit_RESET);
}

/**
 * @brief  建立一个完整的测量通路并等稳定
 * @note   顺序很重要：先断全部，再选 4052 通道，最后合 4066。
 *
 *         为什么先断开：上一项测量的通路如果还接着，和新通路会短暂并联，
 *         在线缆上形成意外回路。断开一下代价很小，省掉一类难查的串扰。
 *
 *         为什么先选通道再供电：4052 切换时输出端会有短暂的浮空/毛刺，
 *         让它发生在线缆还没加电的时候，采样点就干净些。
 */
void Wire_Apply(uint8_t red, uint8_t yellow, uint8_t green, uint8_t black,
                WirePath_t path)
{
    Wire_AllOff();
    Wire_SelectPath(path);
    Wire_SetSwitches(red, yellow, green, black);
    Delay_ms(WIRE_SETTLE_MS);
}
