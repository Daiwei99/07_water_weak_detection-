/**
 ******************************************************************************
 * @file    App_Menu.c
 * @author  Claude (Anthropic)
 * @date    2026-09-13
 * @brief   菜单系统实现
 ******************************************************************************
 */

#include "App_Menu.h"
#include "Int_Key.h"
#include "Int_Oled.h"
#include "App_Config.h"
#include "Com_Delay.h"
#include <stdio.h>
#include <string.h>

/* ========== 菜单项定义 ========== */

typedef enum
{
    MAIN_MENU_VIEW = 0,         // 查看参数
    MAIN_MENU_EDIT,             // 修改参数
    MAIN_MENU_SYSTEM,           // 系统设置
    MAIN_MENU_BACK,             // 返回监测
    MAIN_MENU_COUNT,
} MainMenuItem_e;

typedef enum
{
    EDIT_MENU_R = 0,            // 修改 r
    EDIT_MENU_L,                // 修改 L
    EDIT_MENU_EQUAL_TOL,        // 修改相等容差
    EDIT_MENU_ZERO,             // 修改零点门限
    EDIT_MENU_FULL,             // 修改满幅门限
    EDIT_MENU_SAVE,             // 保存并返回
    EDIT_MENU_COUNT,
} EditMenuItem_e;

typedef enum
{
    SYS_MENU_BUZZER = 0,        // 蜂鸣器开关
    SYS_MENU_RELAY,             // 继电器开关
    SYS_MENU_SAMPLING,          // 采样间隔
    SYS_MENU_DEBOUNCE,          // 去抖次数
    SYS_MENU_HISTORY,           // 查看历史
    SYS_MENU_BACK,              // 返回
    SYS_MENU_COUNT,
} SysMenuItem_e;

/* ========== 静态变量 ========== */

static MenuState_e s_state = MENU_STATE_MONITOR;
static uint8_t s_cursor = 0;            // 当前光标位置
static uint8_t s_need_refresh = 1;      // 需要刷新显示

// 编辑参数临时缓存
static int32_t s_edit_r;                // r × 1000
static int32_t s_edit_L;                // L × 1000
static int32_t s_edit_equal_tol;
static int32_t s_edit_zero;
static int32_t s_edit_full;
static uint8_t s_edit_buzzer;
static uint8_t s_edit_relay;
static uint16_t s_edit_sampling_ms;     // 采样间隔
static uint8_t s_edit_debounce_cnt;     // 去抖次数

// 历史记录结构
typedef struct
{
    uint8_t valid;              // 是否有效
    uint8_t type;               // 0=断线, 1=水浸
    uint32_t timestamp;         // 时间戳（秒）
    float position_m;           // 位置（米）
    char detail[16];            // 详细信息
} HistoryRecord_t;

#define HISTORY_MAX 10
static HistoryRecord_t s_history[HISTORY_MAX];
static uint8_t s_history_head = 0;      // 最新记录索引
static uint8_t s_history_view_idx = 0;  // 查看索引

/* ========== 辅助函数 ========== */

static void Menu_ShowTitle(const char *title)
{
    Oled_Clear();
    Oled_ShowString(0, 0, title);
    Oled_ShowString(0, 1, "----------------");
}

static void Menu_ShowOption(uint8_t line, uint8_t selected, const char *text)
{
    if (selected)
    {
        Oled_ShowString(0, line, ">");
        Oled_ShowString(1, line, text);
    }
    else
    {
        Oled_ShowString(0, line, " ");
        Oled_ShowString(1, line, text);
    }
}

/* ========== 主菜单显示 ========== */

static void Menu_DisplayMain(void)
{
    Menu_ShowTitle("Main Menu");

    Menu_ShowOption(2, (s_cursor == MAIN_MENU_VIEW), "1.View Param");
    Menu_ShowOption(3, (s_cursor == MAIN_MENU_EDIT), "2.Edit Param");

    // 如果光标在下方，滚动显示
    if (s_cursor >= 2)
    {
        Menu_ShowOption(2, (s_cursor == MAIN_MENU_SYSTEM), "3.System Set");
        Menu_ShowOption(3, (s_cursor == MAIN_MENU_BACK), "4.Back");
    }
}

/* ========== 查看参数显示 ========== */

static void Menu_DisplayViewParam(void)
{
    const Config_t *cfg = Config_Get();
    char buf[17];

    Menu_ShowTitle("View Parameters");

    // 显示 r
    snprintf(buf, sizeof(buf), "r:%.3f Ohm/m",
             cfg->r_mOhm_per_m / 1000.0f);
    Oled_ShowString(0, 2, buf);

    // 显示 L
    snprintf(buf, sizeof(buf), "L:%.3f m",
             cfg->length_mm / 1000.0f);
    Oled_ShowString(0, 3, buf);
}

/* ========== 编辑参数菜单显示 ========== */

static void Menu_DisplayEditParam(void)
{
    Menu_ShowTitle("Edit Parameters");

    char buf[17];

    if (s_cursor == EDIT_MENU_R)
    {
        snprintf(buf, sizeof(buf), ">r:%.3f Ohm/m", s_edit_r / 1000.0f);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == EDIT_MENU_L)
    {
        snprintf(buf, sizeof(buf), ">L:%.3f m", s_edit_L / 1000.0f);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == EDIT_MENU_EQUAL_TOL)
    {
        snprintf(buf, sizeof(buf), ">EqualTol:%dmV", (int)s_edit_equal_tol);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == EDIT_MENU_ZERO)
    {
        snprintf(buf, sizeof(buf), ">Zero:%dmV", (int)s_edit_zero);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == EDIT_MENU_FULL)
    {
        snprintf(buf, sizeof(buf), ">Full:%dmV", (int)s_edit_full);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == EDIT_MENU_SAVE)
    {
        Oled_ShowString(0, 2, ">Save & Back");
        Oled_ShowString(0, 3, "Press OK");
    }
}

/* ========== 系统设置显示 ========== */

static void Menu_DisplaySystemSet(void)
{
    Menu_ShowTitle("System Setting");

    char buf[17];

    if (s_cursor == SYS_MENU_BUZZER)
    {
        snprintf(buf, sizeof(buf), ">Buzzer: %s",
                 s_edit_buzzer ? "ON" : "OFF");
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "OK:Toggle");
    }
    else if (s_cursor == SYS_MENU_RELAY)
    {
        snprintf(buf, sizeof(buf), ">Relay: %s",
                 s_edit_relay ? "ON" : "OFF");
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "OK:Toggle");
    }
    else if (s_cursor == SYS_MENU_SAMPLING)
    {
        snprintf(buf, sizeof(buf), ">Sample:%dms", s_edit_sampling_ms);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == SYS_MENU_DEBOUNCE)
    {
        snprintf(buf, sizeof(buf), ">Debounce:%d", s_edit_debounce_cnt);
        Oled_ShowString(0, 2, buf);
        Oled_ShowString(0, 3, "UP/DN:Adjust");
    }
    else if (s_cursor == SYS_MENU_HISTORY)
    {
        Oled_ShowString(0, 2, ">View History");
        Oled_ShowString(0, 3, "Press OK");
    }
    else if (s_cursor == SYS_MENU_BACK)
    {
        Oled_ShowString(0, 2, ">Back");
        Oled_ShowString(0, 3, "Press OK");
    }
}

/* ========== 历史记录显示 ========== */

static void Menu_DisplayHistory(void)
{
    Menu_ShowTitle("History Records");

    char buf[17];

    // 找到当前查看的记录
    uint8_t idx = (s_history_head + HISTORY_MAX - s_history_view_idx) % HISTORY_MAX;
    const HistoryRecord_t *rec = &s_history[idx];

    if (!rec->valid)
    {
        Oled_ShowString(0, 2, "No Record");
        Oled_ShowString(0, 3, "UP/DN:Nav OK:Back");
        return;
    }

    // 第1行：序号和类型
    snprintf(buf, sizeof(buf), "#%d %s",
             s_history_view_idx + 1,
             rec->type == 0 ? "Break" : "Leak");
    Oled_ShowString(0, 2, buf);

    // 第2行：位置或详细信息
    if (rec->type == 1)  // 水浸
    {
        snprintf(buf, sizeof(buf), "Pos:%.2fm", rec->position_m);
    }
    else  // 断线
    {
        snprintf(buf, sizeof(buf), "%s", rec->detail);
    }
    Oled_ShowString(0, 3, buf);
}

static void Menu_ProcessMain(KeyEvent_e event)
{
    if (event == KEY_EVENT_UP_SHORT)
    {
        if (s_cursor > 0)
            s_cursor--;
        s_need_refresh = 1;
    }
    else if (event == KEY_EVENT_DOWN_SHORT)
    {
        if (s_cursor < MAIN_MENU_COUNT - 1)
            s_cursor++;
        s_need_refresh = 1;
    }
    else if (event == KEY_EVENT_OK_SHORT)
    {
        // 进入子菜单
        switch (s_cursor)
        {
            case MAIN_MENU_VIEW:
                s_state = MENU_STATE_VIEW_PARAM;
                s_cursor = 0;
                s_need_refresh = 1;
                break;

            case MAIN_MENU_EDIT:
                // 加载当前参数到编辑缓存
                {
                    const Config_t *cfg = Config_Get();
                    s_edit_r = cfg->r_mOhm_per_m;
                    s_edit_L = cfg->length_mm;
                    s_edit_equal_tol = cfg->equal_tol_mv;
                    s_edit_zero = cfg->zero_mv;
                    s_edit_full = cfg->full_mv;
                }
                s_state = MENU_STATE_EDIT_PARAM;
                s_cursor = 0;
                s_need_refresh = 1;
                break;

            case MAIN_MENU_SYSTEM:
                // 加载当前设置
                {
                    const Config_t *cfg = Config_Get();
                    s_edit_buzzer = cfg->buzzer_enable;
                    s_edit_relay = cfg->relay_enable;
                    s_edit_sampling_ms = cfg->sampling_interval_ms;
                    s_edit_debounce_cnt = cfg->debounce_count;
                }
                s_state = MENU_STATE_SYSTEM_SET;
                s_cursor = 0;
                s_need_refresh = 1;
                break;

            case MAIN_MENU_BACK:
                // 返回监测界面
                s_state = MENU_STATE_MONITOR;
                s_cursor = 0;
                s_need_refresh = 1;
                break;
        }
    }
}

/* ========== 查看参数按键处理 ========== */

static void Menu_ProcessViewParam(KeyEvent_e event)
{
    if (event == KEY_EVENT_OK_SHORT || event == KEY_EVENT_OK_LONG)
    {
        // 返回主菜单
        s_state = MENU_STATE_MAIN;
        s_cursor = 0;
        s_need_refresh = 1;
    }
}

/* ========== 编辑参数按键处理 ========== */

static void Menu_ProcessEditParam(KeyEvent_e event)
{
    if (event == KEY_EVENT_UP_SHORT || event == KEY_EVENT_UP_REPEAT)
    {
        // 根据当前项调整值
        if (s_cursor == EDIT_MENU_R)
        {
            s_edit_r += 10;  // +0.01 Ohm/m
            if (s_edit_r > 10000) s_edit_r = 10000;
        }
        else if (s_cursor == EDIT_MENU_L)
        {
            s_edit_L += 1000;  // +1 m
            if (s_edit_L > 1000000) s_edit_L = 1000000;
        }
        else if (s_cursor == EDIT_MENU_EQUAL_TOL)
        {
            s_edit_equal_tol += 10;  // +10mV
            if (s_edit_equal_tol > 500) s_edit_equal_tol = 500;
        }
        else if (s_cursor == EDIT_MENU_ZERO)
        {
            s_edit_zero += 10;  // +10mV
            if (s_edit_zero > 200) s_edit_zero = 200;
        }
        else if (s_cursor == EDIT_MENU_FULL)
        {
            s_edit_full += 100;  // +100mV
            if (s_edit_full > 3300) s_edit_full = 3300;
        }
        s_need_refresh = 1;
    }
    else if (event == KEY_EVENT_DOWN_SHORT || event == KEY_EVENT_DOWN_REPEAT)
    {
        // 根据当前项调整值
        if (s_cursor == EDIT_MENU_R)
        {
            s_edit_r -= 10;  // -0.01 Ohm/m
            if (s_edit_r < 10) s_edit_r = 10;
        }
        else if (s_cursor == EDIT_MENU_L)
        {
            s_edit_L -= 1000;  // -1 m
            if (s_edit_L < 1000) s_edit_L = 1000;
        }
        else if (s_cursor == EDIT_MENU_EQUAL_TOL)
        {
            s_edit_equal_tol -= 10;  // -10mV
            if (s_edit_equal_tol < 10) s_edit_equal_tol = 10;
        }
        else if (s_cursor == EDIT_MENU_ZERO)
        {
            s_edit_zero -= 10;  // -10mV
            if (s_edit_zero < 10) s_edit_zero = 10;
        }
        else if (s_cursor == EDIT_MENU_FULL)
        {
            s_edit_full -= 100;  // -100mV
            if (s_edit_full < 1000) s_edit_full = 1000;
        }
        s_need_refresh = 1;
    }
    else if (event == KEY_EVENT_OK_SHORT)
    {
        // 切换到下一项
        if (s_cursor == EDIT_MENU_SAVE)
        {
            // 保存参数
            Config_SetCableParam(s_edit_r / 1000.0f, s_edit_L / 1000.0f);
            Config_SetThreshold(s_edit_equal_tol, s_edit_zero, s_edit_full);
            Config_Save();

            // 返回主菜单
            s_state = MENU_STATE_MAIN;
            s_cursor = 0;
        }
        else
        {
            // 切换到下一项
            s_cursor++;
            if (s_cursor >= EDIT_MENU_COUNT)
                s_cursor = 0;
        }

        s_need_refresh = 1;
    }
    else if (event == KEY_EVENT_OK_LONG)
    {
        // 长按取消，不保存
        s_state = MENU_STATE_MAIN;
        s_cursor = 0;
        s_need_refresh = 1;
    }
}

/* ========== 系统设置按键处理 ========== */

static void Menu_ProcessSystemSet(KeyEvent_e event)
{
    if (event == KEY_EVENT_UP_SHORT || event == KEY_EVENT_UP_REPEAT)
    {
        if (s_cursor == SYS_MENU_SAMPLING)
        {
            s_edit_sampling_ms += 10;  // +10ms
            if (s_edit_sampling_ms > 1000) s_edit_sampling_ms = 1000;
            Config_SetSamplingInterval(s_edit_sampling_ms);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_DEBOUNCE)
        {
            s_edit_debounce_cnt++;
            if (s_edit_debounce_cnt > 10) s_edit_debounce_cnt = 10;
            Config_SetDebounceCount(s_edit_debounce_cnt);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor > 0)
        {
            s_cursor--;
            s_need_refresh = 1;
        }
    }
    else if (event == KEY_EVENT_DOWN_SHORT || event == KEY_EVENT_DOWN_REPEAT)
    {
        if (s_cursor == SYS_MENU_SAMPLING)
        {
            s_edit_sampling_ms -= 10;  // -10ms
            if (s_edit_sampling_ms < 50) s_edit_sampling_ms = 50;
            Config_SetSamplingInterval(s_edit_sampling_ms);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_DEBOUNCE)
        {
            s_edit_debounce_cnt--;
            if (s_edit_debounce_cnt < 1) s_edit_debounce_cnt = 1;
            Config_SetDebounceCount(s_edit_debounce_cnt);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor < SYS_MENU_COUNT - 1)
        {
            s_cursor++;
            s_need_refresh = 1;
        }
    }
    else if (event == KEY_EVENT_OK_SHORT)
    {
        if (s_cursor == SYS_MENU_BUZZER)
        {
            s_edit_buzzer = !s_edit_buzzer;
            Config_SetBuzzerEnable(s_edit_buzzer);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_RELAY)
        {
            s_edit_relay = !s_edit_relay;
            Config_SetRelayEnable(s_edit_relay);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_SAMPLING)
        {
            // 保存采样间隔
            Config_SetSamplingInterval(s_edit_sampling_ms);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_DEBOUNCE)
        {
            // 保存去抖次数
            Config_SetDebounceCount(s_edit_debounce_cnt);
            Config_Save();
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_HISTORY)
        {
            // 进入历史记录查看
            s_state = MENU_STATE_VIEW_HISTORY;
            s_history_view_idx = 0;
            s_cursor = 0;
            s_need_refresh = 1;
        }
        else if (s_cursor == SYS_MENU_BACK)
        {
            s_state = MENU_STATE_MAIN;
            s_cursor = 0;
            s_need_refresh = 1;
        }
    }
}

/* ========== 历史记录按键处理 ========== */

static void Menu_ProcessHistory(KeyEvent_e event)
{
    if (event == KEY_EVENT_UP_SHORT)
    {
        // 查看更新的记录
        if (s_history_view_idx > 0)
        {
            s_history_view_idx--;
            s_need_refresh = 1;
        }
    }
    else if (event == KEY_EVENT_DOWN_SHORT)
    {
        // 查看更旧的记录
        if (s_history_view_idx < HISTORY_MAX - 1)
        {
            uint8_t idx = (s_history_head + HISTORY_MAX - s_history_view_idx - 1) % HISTORY_MAX;
            if (s_history[idx].valid)
            {
                s_history_view_idx++;
                s_need_refresh = 1;
            }
        }
    }
    else if (event == KEY_EVENT_OK_SHORT || event == KEY_EVENT_OK_LONG)
    {
        // 返回系统设置
        s_state = MENU_STATE_SYSTEM_SET;
        s_cursor = SYS_MENU_HISTORY;
        s_need_refresh = 1;
    }
}

/* ========== API 实现 ========== */

void Menu_Init(void)
{
    s_state = MENU_STATE_MONITOR;
    s_cursor = 0;
    s_need_refresh = 1;

    // 初始化历史记录
    for (uint8_t i = 0; i < HISTORY_MAX; i++)
    {
        s_history[i].valid = 0;
    }
    s_history_head = 0;
}

uint8_t Menu_Process(void)
{
    KeyEvent_e event = Key_GetEvent();

    if (event == KEY_EVENT_NONE)
        return 0;

    // 在监测界面，长按确认键进入主菜单
    if (s_state == MENU_STATE_MONITOR)
    {
        if (event == KEY_EVENT_OK_LONG)
        {
            s_state = MENU_STATE_MAIN;
            s_cursor = 0;
            s_need_refresh = 1;
            return 1;
        }
        return 0;
    }

    // 在菜单中，根据状态分发事件
    switch (s_state)
    {
        case MENU_STATE_MAIN:
            Menu_ProcessMain(event);
            break;

        case MENU_STATE_VIEW_PARAM:
            Menu_ProcessViewParam(event);
            break;

        case MENU_STATE_EDIT_PARAM:
            Menu_ProcessEditParam(event);
            break;

        case MENU_STATE_SYSTEM_SET:
            Menu_ProcessSystemSet(event);
            break;

        case MENU_STATE_VIEW_HISTORY:
            Menu_ProcessHistory(event);
            break;

        default:
            break;
    }

    return s_need_refresh;
}

void Menu_Display(void)
{
    if (!s_need_refresh)
        return;

    switch (s_state)
    {
        case MENU_STATE_MAIN:
            Menu_DisplayMain();
            break;

        case MENU_STATE_VIEW_PARAM:
            Menu_DisplayViewParam();
            break;

        case MENU_STATE_EDIT_PARAM:
            Menu_DisplayEditParam();
            break;

        case MENU_STATE_SYSTEM_SET:
            Menu_DisplaySystemSet();
            break;

        case MENU_STATE_VIEW_HISTORY:
            Menu_DisplayHistory();
            break;

        default:
            break;
    }

    Oled_Refresh();
    s_need_refresh = 0;
}

MenuState_e Menu_GetState(void)
{
    return s_state;
}

uint8_t Menu_IsMonitoring(void)
{
    return (s_state == MENU_STATE_MONITOR) ? 1 : 0;
}

void Menu_AddHistory(uint8_t type, float position_m, const char *detail)
{
    // 移动头指针
    s_history_head = (s_history_head + 1) % HISTORY_MAX;

    // 写入新记录
    HistoryRecord_t *rec = &s_history[s_history_head];
    rec->valid = 1;
    rec->type = type;
    rec->timestamp = Delay_GetTick() / 1000;  // 转换为秒
    rec->position_m = position_m;

    if (detail)
    {
        strncpy(rec->detail, detail, sizeof(rec->detail) - 1);
        rec->detail[sizeof(rec->detail) - 1] = '\0';
    }
    else
    {
        rec->detail[0] = '\0';
    }
}

uint16_t Menu_GetSamplingInterval(void)
{
    const Config_t *cfg = Config_Get();
    return cfg->sampling_interval_ms;
}

uint8_t Menu_GetDebounceCnt(void)
{
    const Config_t *cfg = Config_Get();
    return cfg->debounce_count;
}
