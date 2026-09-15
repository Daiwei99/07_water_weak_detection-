/**
 ******************************************************************************
 * @file    Int_Key.c
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   按键输入驱动实现
 ******************************************************************************
 */

#include "Int_Key.h"
#include "Com_Board.h"

/* ========== 按键配置 ========== */

#define KEY_DEBOUNCE_TIME   20      // 去抖时间（ms）
#define KEY_LONG_PRESS_TIME 1000    // 长按时间（ms）
#define KEY_REPEAT_TIME     200     // 连续重复间隔（ms）

// 按键引脚定义
#define KEY_UP_GPIO         GPIOA
#define KEY_UP_PIN          GPIO_Pin_0
#define KEY_UP_RCC          RCC_APB2Periph_GPIOA

#define KEY_DOWN_GPIO       GPIOB
#define KEY_DOWN_PIN        GPIO_Pin_0
#define KEY_DOWN_RCC        RCC_APB2Periph_GPIOB

#define KEY_OK_GPIO         GPIOA
#define KEY_OK_PIN          GPIO_Pin_8
#define KEY_OK_RCC          RCC_APB2Periph_GPIOA

/* ========== 按键ID ========== */

typedef enum
{
    KEY_ID_NONE = 0,
    KEY_ID_UP,
    KEY_ID_DOWN,
    KEY_ID_OK,
} KeyId_e;

/* ========== 按键状态 ========== */

typedef struct
{
    uint8_t pressed;            // 当前按下状态
    uint8_t last_pressed;       // 上次按下状态
    uint16_t press_time;        // 按下持续时间（ms）
    uint16_t repeat_time;       // 重复计时（ms）
    uint8_t long_triggered;     // 长按已触发标志
    uint8_t repeat_enabled;     // 是否支持重复
} KeyState_t;

/* ========== 静态变量 ========== */

static KeyState_t s_keys[3];    // 三个按键的状态
static KeyEvent_e s_event = KEY_EVENT_NONE;  // 待读取的事件

/* ========== 内部函数 ========== */

/**
 * @brief  读取按键物理状态
 * @param  key_id: 按键ID
 * @retval 1=按下，0=释放
 */
static uint8_t Key_ReadPin(KeyId_e key_id)
{
    switch (key_id)
    {
        case KEY_ID_UP:
            return (GPIO_ReadInputDataBit(KEY_UP_GPIO, KEY_UP_PIN) == Bit_RESET) ? 1 : 0;

        case KEY_ID_DOWN:
            return (GPIO_ReadInputDataBit(KEY_DOWN_GPIO, KEY_DOWN_PIN) == Bit_RESET) ? 1 : 0;

        case KEY_ID_OK:
            return (GPIO_ReadInputDataBit(KEY_OK_GPIO, KEY_OK_PIN) == Bit_RESET) ? 1 : 0;

        default:
            return 0;
    }
}

/**
 * @brief  发送按键事件
 * @param  event: 事件类型
 */
static void Key_PostEvent(KeyEvent_e event)
{
    // 只保存最新事件（覆盖未读取的旧事件）
    s_event = event;
}

/**
 * @brief  扫描单个按键
 * @param  key_id: 按键ID
 * @param  state: 按键状态结构体
 */
static void Key_ScanOne(KeyId_e key_id, KeyState_t *state)
{
    uint8_t current = Key_ReadPin(key_id);

    // 按键按下
    if (current && !state->last_pressed)
    {
        state->pressed = 1;
        state->press_time = 0;
        state->repeat_time = 0;
        state->long_triggered = 0;
    }
    // 按键保持按下
    else if (current && state->last_pressed)
    {
        state->press_time += 10;  // 每次调用增加10ms

        // 达到去抖时间后才算稳定按下
        if (state->press_time >= KEY_DEBOUNCE_TIME)
        {
            // 检测长按
            if (!state->long_triggered && state->press_time >= KEY_LONG_PRESS_TIME)
            {
                state->long_triggered = 1;

                // 发送长按事件
                switch (key_id)
                {
                    case KEY_ID_UP:
                        Key_PostEvent(KEY_EVENT_UP_LONG);
                        break;
                    case KEY_ID_DOWN:
                        Key_PostEvent(KEY_EVENT_DOWN_LONG);
                        break;
                    case KEY_ID_OK:
                        Key_PostEvent(KEY_EVENT_OK_LONG);
                        break;
                    default:
                        break;
                }
            }

            // 检测连续重复（仅上/下键）
            if (state->long_triggered && state->repeat_enabled)
            {
                state->repeat_time += 10;
                if (state->repeat_time >= KEY_REPEAT_TIME)
                {
                    state->repeat_time = 0;

                    // 发送重复事件
                    switch (key_id)
                    {
                        case KEY_ID_UP:
                            Key_PostEvent(KEY_EVENT_UP_REPEAT);
                            break;
                        case KEY_ID_DOWN:
                            Key_PostEvent(KEY_EVENT_DOWN_REPEAT);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
    // 按键释放
    else if (!current && state->last_pressed)
    {
        // 只有在去抖时间后且未触发长按的情况下，才算短按
        if (state->press_time >= KEY_DEBOUNCE_TIME && !state->long_triggered)
        {
            // 发送短按事件
            switch (key_id)
            {
                case KEY_ID_UP:
                    Key_PostEvent(KEY_EVENT_UP_SHORT);
                    break;
                case KEY_ID_DOWN:
                    Key_PostEvent(KEY_EVENT_DOWN_SHORT);
                    break;
                case KEY_ID_OK:
                    Key_PostEvent(KEY_EVENT_OK_SHORT);
                    break;
                default:
                    break;
            }
        }

        state->pressed = 0;
        state->press_time = 0;
        state->repeat_time = 0;
        state->long_triggered = 0;
    }

    state->last_pressed = current;
}

/* ========== API 实现 ========== */

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // 使能时钟
    RCC_APB2PeriphClockCmd(KEY_UP_RCC | KEY_DOWN_RCC | KEY_OK_RCC, ENABLE);

    // 配置上键（PA0）
    GPIO_InitStructure.GPIO_Pin = KEY_UP_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(KEY_UP_GPIO, &GPIO_InitStructure);

    // 配置下键（PB0）
    GPIO_InitStructure.GPIO_Pin = KEY_DOWN_PIN;
    GPIO_Init(KEY_DOWN_GPIO, &GPIO_InitStructure);

    // 配置确认键（PA8）
    GPIO_InitStructure.GPIO_Pin = KEY_OK_PIN;
    GPIO_Init(KEY_OK_GPIO, &GPIO_InitStructure);

    // 初始化按键状态
    s_keys[0].repeat_enabled = 1;  // 上键支持重复
    s_keys[1].repeat_enabled = 1;  // 下键支持重复
    s_keys[2].repeat_enabled = 0;  // 确认键不支持重复

    s_event = KEY_EVENT_NONE;
}

void Key_Scan(void)
{
    Key_ScanOne(KEY_ID_UP, &s_keys[0]);
    Key_ScanOne(KEY_ID_DOWN, &s_keys[1]);
    Key_ScanOne(KEY_ID_OK, &s_keys[2]);
}

KeyEvent_e Key_GetEvent(void)
{
    KeyEvent_e event = s_event;
    s_event = KEY_EVENT_NONE;  // 读取后清除
    return event;
}
