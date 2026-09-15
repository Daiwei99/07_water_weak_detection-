#include "Int_Relay.h"
#include "Com_Board.h"

/**
 * @brief  初始化继电器控制引脚
 * @note   PB14 推挽输出，初始拉低（继电器释放状态）。
 *         这样上电时继电器不会误动作，避免负载侧瞬间断电再恢复。
 */
void Relay_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RELAY_RCC, ENABLE);

    gpio.GPIO_Pin   = RELAY_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;      /* 继电器不需要快速翻转 */
    GPIO_Init(RELAY_PORT, &gpio);

    GPIO_WriteBit(RELAY_PORT, RELAY_PIN, Bit_RESET);
}

void Relay_On(void)
{
    GPIO_WriteBit(RELAY_PORT, RELAY_PIN, Bit_SET);
}

void Relay_Off(void)
{
    GPIO_WriteBit(RELAY_PORT, RELAY_PIN, Bit_RESET);
}
