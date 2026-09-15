#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include "stm32f10x.h"

/*==============================================================================
 * 参数配置与 Flash 存储
 *
 * 阶段二功能：通过屏幕菜单修改运行参数（r、L、报警阈值等），掉电保存。
 *
 * STM32F103C8T6 Flash 布局：
 *   - 总容量 64KB，页大小 1KB（1024字节）
 *   - 用户程序从 0x0800_0000 开始向上增长
 *   - 当前工程 Code=7.6KB，留足裕量后参数区放在最后一页（0x0800_FC00）
 *
 * 参数结构：
 *   - 魔术字（0x5A5A_5A5A）：判断 Flash 是否写过参数
 *   - 线缆参数：r（Ω/m）、L（m）
 *   - 判定阈值：相等容差、零点、满幅（mV）
 *   - 功能开关：蜂鸣器使能、继电器使能
 *   - CRC16 校验
 *
 * Flash 写流程：
 *   1. 解锁 Flash
 *   2. 擦除目标页（整页 1KB）
 *   3. 按半字（16bit）写入数据
 *   4. 上锁
 *
 * 注意：Flash 擦写次数典型 10000 次，不要频繁保存。保存前校验参数合法性。
 *============================================================================*/

/* 参数区起始地址：Flash 最后一页 */
#define CONFIG_FLASH_ADDR       0x0800FC00U

/* 魔术字：判断是否已写过参数 */
#define CONFIG_MAGIC            0x5A5A5A5AU

/* 参数结构体 */
typedef struct
{
    uint32_t magic;             /* 魔术字 0x5A5A5A5A */

    /* 线缆参数（浮点转定点：乘 1000 存整数，避免浮点对齐问题） */
    int32_t  r_mOhm_per_m;      /* r × 1000，单位 mΩ/m，范围 [1, 10000] */
    int32_t  length_mm;         /* L × 1000，单位 mm，范围 [1000, 500000] */

    /* 判定阈值（mV） */
    int32_t  equal_tol_mv;      /* 相等容差，范围 [10, 200] */
    int32_t  zero_mv;           /* 零点门限，范围 [10, 500] */
    int32_t  full_mv;           /* 满幅门限，范围 [2800, 3290] */

    /* 功能开关 */
    uint8_t  buzzer_enable;     /* 1 = 蜂鸣器使能 */
    uint8_t  relay_enable;      /* 1 = 继电器使能 */
    uint16_t sampling_interval_ms;  /* 采样间隔（毫秒），范围 [50, 1000] */
    uint8_t  debounce_count;    /* 去抖次数，范围 [1, 10] */
    uint8_t  reserved[3];       /* 对齐到 4 字节 */

    uint16_t crc16;             /* 前面所有字段的 CRC16 */
} Config_t;

void Config_Init(void);

/* 从 Flash 加载参数，加载成功返回 1，失败返回 0（使用默认值） */
uint8_t Config_Load(void);

/* 保存参数到 Flash，成功返回 1，失败返回 0 */
uint8_t Config_Save(void);

/* 获取当前配置（返回只读指针） */
const Config_t* Config_Get(void);

/* 修改参数（修改后需调用 Config_Save 才会写入 Flash） */
void Config_SetCableParam(float r_ohm_per_m, float length_m);
void Config_SetThreshold(int32_t equal_tol_mv, int32_t zero_mv, int32_t full_mv);
void Config_SetBuzzerEnable(uint8_t enable);
void Config_SetRelayEnable(uint8_t enable);
void Config_SetSamplingInterval(uint16_t interval_ms);
void Config_SetDebounceCount(uint8_t count);

/* 恢复出厂默认值（内存中，需调用 Config_Save 才写 Flash） */
void Config_RestoreDefault(void);

#endif /* __APP_CONFIG_H */
