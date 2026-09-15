#include "Int_Rs485.h"
#include "Com_Board.h"

/**
 * @brief  初始化 USART3 用于 RS485 通信
 * @note   PB10 = TX（复用推挽）、PB11 = RX（浮空输入）
 *         波特率 9600，8-N-1
 *         MAX13487 自动方向控制，不需要 DE/RE 引脚
 */
void Rs485_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RS485_TX_RCC | RS485_RX_RCC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* PB10 = TX，复用推挽 */
    gpio.GPIO_Pin   = RS485_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RS485_TX_PORT, &gpio);

    /* PB11 = RX，浮空输入 */
    gpio.GPIO_Pin  = RS485_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(RS485_RX_PORT, &gpio);

    usart.USART_BaudRate            = 9600;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &usart);

    USART_Cmd(USART3, ENABLE);
}

void Rs485_SendByte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
    {
        /* 等发送寄存器空 */
    }
    USART_SendData(USART3, byte);
}

void Rs485_SendString(const char *str)
{
    while (*str)
    {
        Rs485_SendByte((uint8_t)(*str++));
    }
}

void Rs485_SendData(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        Rs485_SendByte(data[i]);
    }
}
