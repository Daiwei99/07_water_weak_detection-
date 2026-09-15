/**
 ******************************************************************************
 * @file    Int_Sgm58031.c
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   SGM58031 16位ADC驱动实现
 ******************************************************************************
 */

#include "Int_Sgm58031.h"
#include "Com_Delay.h"

/* ========== 寄存器定义 ========== */

#define SGM58031_REG_CONVERSION     0x00    // 转换结果寄存器
#define SGM58031_REG_CONFIG         0x01    // 配置寄存器

/* 配置寄存器位定义 */
#define SGM58031_CFG_OS             (1 << 15)   // 单次转换启动
#define SGM58031_CFG_MUX_AIN0_AIN1  (0 << 12)   // 差分输入 AIN0-AIN1
#define SGM58031_CFG_PGA_SHIFT      9           // PGA增益位移
#define SGM58031_CFG_MODE_SINGLE    (1 << 8)    // 单次转换模式
#define SGM58031_CFG_DR_128SPS      (4 << 5)    // 采样率128 SPS
#define SGM58031_CFG_COMP_QUE_DIS   (3 << 0)    // 禁用比较器

/* I2C2 引脚定义 */
#define SGM58031_I2C                I2C2
#define SGM58031_I2C_CLK            RCC_APB1Periph_I2C2
#define SGM58031_GPIO_CLK           RCC_APB2Periph_GPIOB
#define SGM58031_SCL_PIN            GPIO_Pin_10
#define SGM58031_SDA_PIN            GPIO_Pin_11
#define SGM58031_GPIO_PORT          GPIOB

/* ========== 硬件I2C操作 ========== */

/**
 * @brief  等待I2C事件
 * @param  event: I2C事件
 * @retval 1=成功，0=超时
 */
static uint8_t I2C2_WaitEvent(uint32_t event)
{
    uint32_t timeout = 10000;
    while (!I2C_CheckEvent(SGM58031_I2C, event))
    {
        if (--timeout == 0)
            return 0;
    }
    return 1;
}

/**
 * @brief  I2C写寄存器（16位数据）
 * @param  reg: 寄存器地址
 * @param  data: 16位数据（高字节在前）
 * @retval 1=成功，0=失败
 */
static uint8_t Sgm58031_WriteReg(uint8_t reg, uint16_t data)
{
    // 起始信号
    I2C_GenerateSTART(SGM58031_I2C, ENABLE);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
        return 0;

    // 发送器件地址+写
    I2C_Send7bitAddress(SGM58031_I2C, SGM58031_I2C_ADDR << 1, I2C_Direction_Transmitter);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        return 0;

    // 发送寄存器地址
    I2C_SendData(SGM58031_I2C, reg);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return 0;

    // 发送数据高字节
    I2C_SendData(SGM58031_I2C, (uint8_t)(data >> 8));
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return 0;

    // 发送数据低字节
    I2C_SendData(SGM58031_I2C, (uint8_t)(data & 0xFF));
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return 0;

    // 停止信号
    I2C_GenerateSTOP(SGM58031_I2C, ENABLE);

    return 1;
}

/**
 * @brief  I2C读寄存器（16位数据）
 * @param  reg: 寄存器地址
 * @param  data: 读取的16位数据指针
 * @retval 1=成功，0=失败
 */
static uint8_t Sgm58031_ReadReg(uint8_t reg, uint16_t *data)
{
    uint8_t msb, lsb;

    // 起始信号
    I2C_GenerateSTART(SGM58031_I2C, ENABLE);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
        return 0;

    // 发送器件地址+写
    I2C_Send7bitAddress(SGM58031_I2C, SGM58031_I2C_ADDR << 1, I2C_Direction_Transmitter);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        return 0;

    // 发送寄存器地址
    I2C_SendData(SGM58031_I2C, reg);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return 0;

    // 重复起始信号
    I2C_GenerateSTART(SGM58031_I2C, ENABLE);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
        return 0;

    // 发送器件地址+读
    I2C_Send7bitAddress(SGM58031_I2C, SGM58031_I2C_ADDR << 1, I2C_Direction_Receiver);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        return 0;

    // 读取高字节（发送ACK）
    I2C_AcknowledgeConfig(SGM58031_I2C, ENABLE);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
        return 0;
    msb = I2C_ReceiveData(SGM58031_I2C);

    // 读取低字节（发送NACK）
    I2C_AcknowledgeConfig(SGM58031_I2C, DISABLE);
    if (!I2C2_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
        return 0;
    lsb = I2C_ReceiveData(SGM58031_I2C);

    // 停止信号
    I2C_GenerateSTOP(SGM58031_I2C, ENABLE);

    *data = ((uint16_t)msb << 8) | lsb;
    return 1;
}

/* ========== API 实现 ========== */

void Sgm58031_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef I2C_InitStructure;

    // 使能时钟
    RCC_APB1PeriphClockCmd(SGM58031_I2C_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(SGM58031_GPIO_CLK, ENABLE);

    // 配置I2C引脚：PB10(SCL), PB11(SDA)
    GPIO_InitStructure.GPIO_Pin = SGM58031_SCL_PIN | SGM58031_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SGM58031_GPIO_PORT, &GPIO_InitStructure);

    // 配置I2C2
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_ClockSpeed = 100000;          // 100kHz
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(SGM58031_I2C, &I2C_InitStructure);
    I2C_Cmd(SGM58031_I2C, ENABLE);

    // 等待I2C就绪
    Delay_ms(10);
}

int16_t Sgm58031_ReadDiff(Sgm58031Pga_e pga)
{
    uint16_t config, result;

    // 构造配置寄存器
    config = SGM58031_CFG_OS |                          // 启动单次转换
             SGM58031_CFG_MUX_AIN0_AIN1 |               // 差分输入
             ((uint16_t)pga << SGM58031_CFG_PGA_SHIFT) | // PGA增益
             SGM58031_CFG_MODE_SINGLE |                 // 单次模式
             SGM58031_CFG_DR_128SPS |                   // 128 SPS
             SGM58031_CFG_COMP_QUE_DIS;                 // 禁用比较器

    // 写入配置寄存器，启动转换
    if (!Sgm58031_WriteReg(SGM58031_REG_CONFIG, config))
        return 0;

    // 等待转换完成（128 SPS约需8ms）
    Delay_ms(10);

    // 读取转换结果
    if (!Sgm58031_ReadReg(SGM58031_REG_CONVERSION, &result))
        return 0;

    return (int16_t)result;
}

int32_t Sgm58031_AdcToMv(int16_t adc, Sgm58031Pga_e pga)
{
    int32_t full_scale_mv;

    // 根据PGA增益确定满量程电压
    switch (pga)
    {
        case SGM58031_PGA_1X: full_scale_mv = 6144; break;
        case SGM58031_PGA_2X: full_scale_mv = 4096; break;
        case SGM58031_PGA_4X: full_scale_mv = 2048; break;
        case SGM58031_PGA_8X: full_scale_mv = 1024; break;
        default: full_scale_mv = 6144; break;
    }

    // 转换：16位有符号 -> 毫伏
    // ADC范围：-32768~32767，电压范围：-full_scale~+full_scale
    return ((int32_t)adc * full_scale_mv) / 32768;
}
