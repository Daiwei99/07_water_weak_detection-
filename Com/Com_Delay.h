#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f10x.h"

/**
 * 基于 SysTick 的延时与毫秒时基。
 * SysTick_Handler 定义在 Delay.c 里 —— stm32f10x_it.c 里没有这个函数，
 * 不会重复定义。
 */

void     Delay_Init(void);       /* 必须在 Board_Init() 之后调用 */
void     Delay_ms(uint32_t ms);
void     Delay_us(uint32_t us);
uint32_t Delay_GetTick(void);    /* 开机至今的毫秒数，用于软件定时/超时判断 */

/* 判断从 start 起是否已经过了 ms 毫秒，能正确处理 32 位回绕 */
uint8_t  Delay_IsTimeout(uint32_t start, uint32_t ms);

#endif /* __DELAY_H */
