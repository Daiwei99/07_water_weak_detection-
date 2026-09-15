/**
 ******************************************************************************
 * @file    Int_Font_CN.h
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   16x16 中文字库（常用汉字）
 *
 * 字库格式：
 *   - 每个汉字 16×16 点阵，共 32 字节
 *   - 纵向8位，高位在上
 *   - 使用 PCtoLCD2002 生成，阴码，逐行式
 ******************************************************************************
 */

#ifndef __INT_FONT_CN_H
#define __INT_FONT_CN_H

#include "stm32f10x.h"

/* ========== 中文字符编码表 ========== */

typedef struct
{
    const char *utf8;           // UTF-8 编码（3字节）
    const uint8_t *data;        // 点阵数据（32字节）
} FontCN_t;

/* ========== API 函数 ========== */

/**
 * @brief  根据 UTF-8 字符串查找汉字点阵
 * @param  utf8: UTF-8 编码字符串（3字节汉字）
 * @retval 点阵数据指针，未找到返回 NULL
 */
const uint8_t* FontCN_GetData(const char *utf8);

#endif /* __INT_FONT_CN_H */
