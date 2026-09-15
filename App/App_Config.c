#include "App_Config.h"
#include "App_Measure.h"
#include "Com_Uart.h"
#include <stdio.h>
#include <string.h>

/* 当前配置（RAM 副本） */
static Config_t s_config;

/**
 * @brief  计算 CRC16-CCITT
 * @note   多项式 0x1021，初值 0xFFFF。
 *         计算范围：从 magic 到 reserved，不包括 crc16 本身。
 */
static uint16_t Config_CalcCrc16(const Config_t *cfg)
{
    const uint8_t *data = (const uint8_t *)cfg;
    uint16_t len = (uint16_t)((uint32_t)&cfg->crc16 - (uint32_t)cfg);
    uint16_t crc = 0xFFFFU;
    uint16_t i, j;

    for (i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000U)
            {
                crc = (crc << 1) ^ 0x1021U;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief  参数合法性检查
 * @note   避免非法值损坏测量逻辑
 */
static uint8_t Config_Validate(const Config_t *cfg)
{
    if (cfg->magic != CONFIG_MAGIC) { return 0; }

    /* r 范围 [0.001, 10.0] Ω/m */
    if ((cfg->r_mOhm_per_m < 1) || (cfg->r_mOhm_per_m > 10000)) { return 0; }

    /* L 范围 [1.0, 500.0] m */
    if ((cfg->length_mm < 1000) || (cfg->length_mm > 500000)) { return 0; }

    /* 阈值范围 */
    if ((cfg->equal_tol_mv < 10) || (cfg->equal_tol_mv > 200)) { return 0; }
    if ((cfg->zero_mv < 10) || (cfg->zero_mv > 500)) { return 0; }
    if ((cfg->full_mv < 2800) || (cfg->full_mv > 3290)) { return 0; }

    /* 采样间隔和去抖次数 */
    if ((cfg->sampling_interval_ms < 50) || (cfg->sampling_interval_ms > 1000)) { return 0; }
    if ((cfg->debounce_count < 1) || (cfg->debounce_count > 10)) { return 0; }

    /* CRC 校验 */
    if (Config_CalcCrc16(cfg) != cfg->crc16) { return 0; }

    return 1;
}

/**
 * @brief  设置默认参数
 */
static void Config_SetDefault(Config_t *cfg)
{
    cfg->magic = CONFIG_MAGIC;

    /* 线缆参数 */
    cfg->r_mOhm_per_m = (int32_t)(MEAS_DEFAULT_R * 1000.0f);
    cfg->length_mm = (int32_t)(MEAS_DEFAULT_L * 1000.0f);

    /* 判定阈值 */
    cfg->equal_tol_mv = MEAS_EQUAL_TOL_MV;
    cfg->zero_mv = MEAS_ZERO_MV;
    cfg->full_mv = MEAS_FULL_MV;

    /* 功能开关 */
    cfg->buzzer_enable = 1;
    cfg->relay_enable = 1;
    cfg->sampling_interval_ms = 100;  // 默认100ms
    cfg->debounce_count = 3;          // 默认3次
    cfg->reserved[0] = 0;
    cfg->reserved[1] = 0;
    cfg->reserved[2] = 0;

    /* 计算 CRC */
    cfg->crc16 = Config_CalcCrc16(cfg);
}

void Config_Init(void)
{
    /* 先加载 Flash 参数，失败则用默认值 */
    if (!Config_Load())
    {
        printf("[配置] Flash 参数无效，使用默认值\r\n");
        Config_SetDefault(&s_config);
    }

    /* 同步到测量层 */
    Measure_SetCableParam((float)s_config.r_mOhm_per_m / 1000.0f,
                          (float)s_config.length_mm / 1000.0f);
}

uint8_t Config_Load(void)
{
    const Config_t *flash_cfg = (const Config_t *)CONFIG_FLASH_ADDR;

    /* 校验 Flash 数据 */
    if (!Config_Validate(flash_cfg))
    {
        return 0;
    }

    /* 拷贝到 RAM */
    memcpy(&s_config, flash_cfg, sizeof(Config_t));

    printf("[配置] 从 Flash 加载成功\r\n");
    printf("  r = %d.%03d Ohm/m\r\n",
           (int)(s_config.r_mOhm_per_m / 1000),
           (int)(s_config.r_mOhm_per_m % 1000));
    printf("  L = %d.%03d m\r\n",
           (int)(s_config.length_mm / 1000),
           (int)(s_config.length_mm % 1000));

    return 1;
}

uint8_t Config_Save(void)
{
    uint32_t addr;
    uint16_t *src;
    uint16_t i;
    FLASH_Status status;

    /* 更新 CRC */
    s_config.crc16 = Config_CalcCrc16(&s_config);

    /* 再次校验（防止参数被改坏） */
    if (!Config_Validate(&s_config))
    {
        printf("[配置] 参数非法，放弃保存\r\n");
        return 0;
    }

    /* 解锁 Flash */
    FLASH_Unlock();

    /* 擦除目标页 */
    status = FLASH_ErasePage(CONFIG_FLASH_ADDR);
    if (status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        printf("[配置] 擦除失败\r\n");
        return 0;
    }

    /* 按半字写入 */
    src = (uint16_t *)&s_config;
    addr = CONFIG_FLASH_ADDR;

    for (i = 0; i < sizeof(Config_t) / 2; i++)
    {
        status = FLASH_ProgramHalfWord(addr, src[i]);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            printf("[配置] 写入失败 @ 0x%08X\r\n", (unsigned int)addr);
            return 0;
        }
        addr += 2;
    }

    /* 上锁 */
    FLASH_Lock();

    printf("[配置] 保存成功\r\n");
    return 1;
}

const Config_t* Config_Get(void)
{
    return &s_config;
}

void Config_SetCableParam(float r_ohm_per_m, float length_m)
{
    if (r_ohm_per_m > 0.0f)
    {
        s_config.r_mOhm_per_m = (int32_t)(r_ohm_per_m * 1000.0f);
    }
    if (length_m > 0.0f)
    {
        s_config.length_mm = (int32_t)(length_m * 1000.0f);
    }

    /* 同步到测量层 */
    Measure_SetCableParam((float)s_config.r_mOhm_per_m / 1000.0f,
                          (float)s_config.length_mm / 1000.0f);
}

void Config_SetThreshold(int32_t equal_tol_mv, int32_t zero_mv, int32_t full_mv)
{
    if ((equal_tol_mv >= 10) && (equal_tol_mv <= 200))
    {
        s_config.equal_tol_mv = equal_tol_mv;
    }
    if ((zero_mv >= 10) && (zero_mv <= 500))
    {
        s_config.zero_mv = zero_mv;
    }
    if ((full_mv >= 2800) && (full_mv <= 3290))
    {
        s_config.full_mv = full_mv;
    }
}

void Config_SetBuzzerEnable(uint8_t enable)
{
    s_config.buzzer_enable = enable ? 1U : 0U;
}

void Config_SetRelayEnable(uint8_t enable)
{
    s_config.relay_enable = enable ? 1U : 0U;
}

void Config_SetSamplingInterval(uint16_t interval_ms)
{
    if ((interval_ms >= 50) && (interval_ms <= 1000))
    {
        s_config.sampling_interval_ms = interval_ms;
    }
}

void Config_SetDebounceCount(uint8_t count)
{
    if ((count >= 1) && (count <= 10))
    {
        s_config.debounce_count = count;
    }
}

void Config_RestoreDefault(void)
{
    Config_SetDefault(&s_config);

    /* 同步到测量层 */
    Measure_SetCableParam((float)s_config.r_mOhm_per_m / 1000.0f,
                          (float)s_config.length_mm / 1000.0f);

    printf("[配置] 恢复出厂默认值\r\n");
}
