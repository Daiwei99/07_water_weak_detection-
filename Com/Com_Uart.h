#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f10x.h"
#include <stdio.h>

/**
 * 调试串口 UART1（PA9 / PA10，115200-8-N-1）
 * 初始化后 printf 直接可用（工程已勾选 MicroLIB，fputc 重定向生效）。
 */

void Uart_Init(void);
void Uart_SendByte(uint8_t b);
void Uart_SendString(const char *s);

#endif /* __BSP_UART_H */
