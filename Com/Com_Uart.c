#include "Com_Uart.h"
#include "Com_Board.h"

/**
 * @brief  UART1 初始化，115200-8-N-1，只用 TX/RX 不用中断
 * @note   PA9 = TX 复用推挽，PA10 = RX 浮空输入。
 *         这两个引脚在 P3 排针上有引出，是唯一的调试出口 ——
 *         后面调 ADC、验证公式全靠它，所以第一个就要把它点亮。
 */
void Uart_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(DBG_UART_RCC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* TX: 复用推挽输出 */
    gpio.GPIO_Pin   = DBG_UART_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DBG_UART_PORT, &gpio);

    /* RX: 浮空输入 */
    gpio.GPIO_Pin  = DBG_UART_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(DBG_UART_PORT, &gpio);

    usart.USART_BaudRate            = DBG_UART_BAUD;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(DBG_UART, &usart);

    USART_Cmd(DBG_UART, ENABLE);
}

void Uart_SendByte(uint8_t b)
{
    USART_SendData(DBG_UART, b);
    while (USART_GetFlagStatus(DBG_UART, USART_FLAG_TXE) == RESET)
    {
        /* 等发送数据寄存器空 */
    }
}

void Uart_SendString(const char *s)
{
    while (*s)
    {
        Uart_SendByte((uint8_t)*s++);
    }
}

/**
 * @brief  printf 重定向
 * @note   工程 Options -> Target 已勾选 Use MicroLIB，这里实现 fputc 即可。
 *         若取消 MicroLIB，需要改用 __io_putchar + 完整的 retarget。
 */
int fputc(int ch, FILE *f)
{
    (void)f;
    Uart_SendByte((uint8_t)ch);
    return ch;
}
