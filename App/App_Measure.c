#include "App_Measure.h"
#include "Int_Adc.h"
#include "Int_Sgm58031.h"
#include "Int_Wire.h"
#include "Com_Board.h"
#include "Com_Delay.h"
#include <stdio.h>

/*==============================================================================
 * 硬件配置选择（阶段二 vs 阶段三）
 *============================================================================*/
#ifndef USE_EXTERNAL_ADC
#define USE_EXTERNAL_ADC    0       /* 0=使用内部ADC（阶段二），1=使用SGM58031（阶段三） */
#endif

/*==============================================================================
 * 线缆参数。用静态变量而不是宏，因为阶段二要能通过屏幕菜单改并存到 Flash。
 *============================================================================*/
static float s_r_ohm_per_m = MEAS_DEFAULT_R;
static float s_cable_len_m = MEAS_DEFAULT_L;

/**
 * @brief  原始码值转毫伏（纯整数运算）
 * @note   mV = raw * 3300 / 4095。
 *         先乘后除，raw 最大 4095，4095*3300 = 13,513,500，uint32_t 装得下。
 *         加 2047 是四舍五入（半个除数），避免整数除法系统性偏低半个 LSB。
 *
 *         不用 Adc_RawToVolt() 是为了全程不碰 float —— 见 Measure.h 里
 *         关于 MicroLIB printf 的说明。
 */
static int32_t Adc_RawToMillivolt(uint16_t raw)
{
    return (int32_t)(((uint32_t)raw * (uint32_t)MEAS_VCC_MV + 2047U) / 4095U);
}

static int32_t Meas_ReadNet0Mv(void)
{
#if USE_EXTERNAL_ADC
    /* 阶段三：使用SGM58031读取差分电压（AIN0-AIN1对应Net0） */
    int16_t raw = Sgm58031_ReadSingle(0, SGM_GAIN_1X);
    int32_t uv = Sgm58031_RawToMicrovolts(raw, SGM_GAIN_1X);
    return uv / 1000;  /* 微伏转毫伏 */
#else
    /* 阶段二：使用内部ADC */
    return Adc_RawToMillivolt(Adc_ReadNet0());
#endif
}

static int32_t Meas_ReadNet1Mv(void)
{
#if USE_EXTERNAL_ADC
    /* 阶段三：使用SGM58031读取差分电压（AIN1-GND对应Net1） */
    int16_t raw = Sgm58031_ReadSingle(1, SGM_GAIN_1X);
    int32_t uv = Sgm58031_RawToMicrovolts(raw, SGM_GAIN_1X);
    return uv / 1000;  /* 微伏转毫伏 */
#else
    /* 阶段二：使用内部ADC */
    return Adc_RawToMillivolt(Adc_ReadNet1());
#endif
}

/**
 * @brief  取绝对值（整数）
 * @note   自己写而不用 stdlib 的 abs()，省一个头文件依赖。
 */
static int32_t Meas_Abs(int32_t x)
{
    return (x < 0) ? -x : x;
}

/**
 * @brief  初始化测量层
 * @note   顺带把依赖的两层也初始化了，main 里只调这一个就够。
 *         前置条件仍然是 Board_Init() 和 Delay_Init() 已经调过 ——
 *         Wire_Init 需要 JTAG 已关闭，Wire_Apply 需要 SysTick 已跑起来。
 */
void Measure_Init(void)
{
    Wire_Init();

#if USE_EXTERNAL_ADC
    Sgm58031_Init();    /* 阶段三：初始化外置16位ADC */
#else
    Adc_Init();         /* 阶段二：使用内部12位ADC */
#endif
}

void Measure_SetCableParam(float r_ohm_per_m, float length_m)
{
    if (r_ohm_per_m > 0.0f)
    {
        s_r_ohm_per_m = r_ohm_per_m;
    }
    if (length_m > 0.0f)
    {
        s_cable_len_m = length_m;
    }
}

float Measure_GetR(void)
{
    return s_r_ohm_per_m;
}

float Measure_GetL(void)
{
    return s_cable_len_m;
}

/*==============================================================================
 * 分项采集
 *
 * 每一项都是 "Wire_Apply 建立通路并等稳定 -> 读两路 ADC"。
 * Wire_Apply 的参数顺序是 (红, 黄, 绿, 黑, 4052通道)，
 * 前四个对应 4066 的四路使能，含义见 Bsp_Wire.h。
 *============================================================================*/

/**
 * @brief  采断线监测的 V1~V4
 * @note   对应需求文档：
 *           断线1  PB3=1 PB4=1 PB6=0 PB7=0  ->  ADC1=V1(黑) ADC0=V2(黄)
 *           断线2  PB3=1 PB4=1 PB6=0 PB7=1  ->  ADC1=V3(绿) ADC0=V4(红)
 *
 *         两项的 4066 开关状态完全一样（红黄都供电、绿黑都不接取样电阻），
 *         只有 4052 通道不同。理论上可以只 Apply 一次、切一下通道再采，
 *         省掉 15ms。这里仍然分两次 Apply，理由是让代码和文档逐行对应，
 *         排错时能直接比对。等阶段一验证通过、要提扫描速度时再合并。
 *
 *         原理：红黄两线在远端与绿黑短接构成回路（见"定位测漏检测线"结构）。
 *         回路通的时候，供电线和返回线上的电压应该相等；某一根断了，
 *         返回线就浮空，读数掉到 0 附近。
 */
void Measure_ReadBreak(MeasVolt_t *v)
{
    if (v == 0) { return; }

    /* 断线监测1：4052 通道 0 -> ADC0=黄线, ADC1=黑线 */
    Wire_Apply(1, 1, 0, 0, WIRE_PATH_YELLOW_BLACK);
    v->v2 = Meas_ReadNet0Mv();      /* ADC0 = 黄线 = V2 */
    v->v1 = Meas_ReadNet1Mv();      /* ADC1 = 黑线 = V1 */

    /* 断线监测2：4052 通道 2 -> ADC0=红线, ADC1=绿线 */
    Wire_Apply(1, 1, 0, 0, WIRE_PATH_RED_GREEN);
    v->v4 = Meas_ReadNet0Mv();      /* ADC0 = 红线 = V4 */
    v->v3 = Meas_ReadNet1Mv();      /* ADC1 = 绿线 = V3 */
}

/**
 * @brief  采水浸监测的 V5、V6
 * @note   对应需求文档：PB4=1 PB5=1 PB6=1 PB7=0 -> ADC1=V5(黄) ADC0=V6(绿)
 *         即：红线供电、黑线接取样电阻 R11，4052 选通道 1。
 *
 *         原理：红线加 3.3V，黑线经 R11 到地，构成一条回路。黄线和绿线
 *         此时都不接任何东西（悬空），只通过线缆间的绝缘电阻与红黑耦合。
 *           - 干燥时绝缘电阻极大，绿线被红线拉到接近 3.3V，黄线接近 0；
 *           - 进水时水在两组线之间形成一个有限电阻，把两边电位拉到中间，
 *             V5、V6 都落在 0~3.3V 之间。
 *         所以判定条件是"V6 接近满幅且 V5 接近 0"才算干燥。
 */
void Measure_ReadLeak(MeasVolt_t *v)
{
    if (v == 0) { return; }

    /* 4052 通道 1 -> ADC0=绿线, ADC1=黄线 */
    Wire_Apply(1, 0, 0, 1, WIRE_PATH_GREEN_YELLOW);
    v->v6 = Meas_ReadNet0Mv();      /* ADC0 = 绿线 = V6 */
    v->v5 = Meas_ReadNet1Mv();      /* ADC1 = 黄线 = V5 */
}

/**
 * @brief  采水浸定位的 V7~V10
 * @note   对应需求文档：
 *           定位1   PB3=1 PA15=1 PB6=1 PB7=0  ->  ADC1=V7(黄) ADC0=V8(绿)
 *           定位2a  PB4=1 PB5=1  PB6=0 PB7=1  ->  ADC0=V9(红)
 *           定位2b  PB4=1 PB5=1  PB6=0 PB7=0  ->  ADC1=V10(黑)
 *
 *         定位1 换成"黄线供电、绿线接取样电阻"，测的是另一组线对的分压。
 *         定位2 是"红线供电、黑线接取样电阻"，和水浸监测的开关状态相同，
 *         但要分两次读：V9 要 ADC0 接红线（通道 2），V10 要 ADC1 接黑线
 *         （通道 0）。4052 的两个 bank 共用地址线 S1/S2，没有哪个通道能
 *         同时把 ADC0 接红、ADC1 接黑，所以只能切两次通道。
 *
 *         两次之间 Wire_Apply 会先 AllOff 再重建通路，线缆上的电压会掉一下
 *         再回来。因为每次都等了 15ms，采样时已经重新稳定，不影响结果。
 */
void Measure_ReadLocate(MeasVolt_t *v)
{
    if (v == 0) { return; }

    /* 定位1：黄线供电、绿线接 R11，通道 1 -> ADC0=绿, ADC1=黄 */
    Wire_Apply(0, 1, 1, 0, WIRE_PATH_GREEN_YELLOW);
    v->v8 = Meas_ReadNet0Mv();      /* ADC0 = 绿线 = V8 */
    v->v7 = Meas_ReadNet1Mv();      /* ADC1 = 黄线 = V7 */

    /* 定位2a：红线供电、黑线接 R11，通道 2 -> ADC0=红线 = V9 */
    Wire_Apply(1, 0, 0, 1, WIRE_PATH_RED_GREEN);
    v->v9 = Meas_ReadNet0Mv();

    /* 定位2b：开关不变，只把通道切到 0 -> ADC1=黑线 = V10 */
    Wire_Apply(1, 0, 0, 1, WIRE_PATH_YELLOW_BLACK);
    v->v10 = Meas_ReadNet1Mv();
}

/**
 * @brief  阶段一：按顺序采完 V1~V10
 * @note   采完之后 4066 全断开，线缆回到静止态，不长时间给线缆加激励。
 *         长期加电会加速电极电化学腐蚀 —— 水浸检测靠的就是水的导电性，
 *         电极腐蚀了灵敏度会漂。
 *
 *         总耗时约 6 × 15ms（等稳定）+ 12 × 9 × 21µs（采样）≈ 92ms。
 */
void Measure_ReadAll(MeasVolt_t *v)
{
    if (v == 0) { return; }

    Measure_ReadBreak(v);
    Measure_ReadLeak(v);
    Measure_ReadLocate(v);

    Wire_AllOff();
}

/*==============================================================================
 * 判定
 *============================================================================*/

/**
 * @brief  断线判定
 * @return FaultFlag_t 的按位组合，0 表示两个回路都正常
 * @note   需求文档的判据：正常 = (V1==V2 且 V3==V4)。
 *         "相等"在实际电路里不可能严格相等，用 MEAS_EQUAL_TOL_MV 容差。
 *
 *         文档还提到"若 V1=0 或 V3=0 则确认断线"。这是个强证据：返回线
 *         浮空才会读到 0。所以这里两个条件取或 —— 差值超容差，或者返回线
 *         直接是 0，都判断线。后者能覆盖一种边界情况：如果供电线本身断了，
 *         V1 和 V2 可能都接近 0，差值反而在容差内，只看差值会漏判。
 */
uint8_t Measure_CheckBreak(const MeasVolt_t *v)
{
    uint8_t fault = FAULT_NONE;

    if (v == 0) { return FAULT_NONE; }

    /* 黄线回路：V2 黄线供电端，V1 黑线返回端 */
    if ((Meas_Abs(v->v1 - v->v2) > MEAS_EQUAL_TOL_MV) ||
        (v->v1 < MEAS_ZERO_MV))
    {
        fault |= (uint8_t)FAULT_BREAK_YEL;
    }

    /* 红线回路：V4 红线供电端，V3 绿线返回端 */
    if ((Meas_Abs(v->v3 - v->v4) > MEAS_EQUAL_TOL_MV) ||
        (v->v3 < MEAS_ZERO_MV))
    {
        fault |= (uint8_t)FAULT_BREAK_RED;
    }

    return fault;
}

/**
 * @brief  水浸判定
 * @return 1 = 检测到水浸，0 = 干燥
 * @note   需求文档的判据：
 *           无水浸：V6 == 3.3V 且 V5 == 0
 *           有水浸：V5、V6 都在 0~3.3V 之间
 *         取反来实现：只要 V6 明显低于满幅，或者 V5 明显高于 0，就算水浸。
 *         两个条件取或而不是取与 —— 漏水点靠近某一端时，可能只有一侧的
 *         电压变化明显，取与会漏判。
 */
uint8_t Measure_CheckLeak(const MeasVolt_t *v)
{
    if (v == 0) { return 0U; }

    if ((v->v6 < MEAS_FULL_MV) || (v->v5 > MEAS_ZERO_MV))
    {
        return 1U;
    }

    return 0U;
}

/*==============================================================================
 * 定位计算
 *
 * 需求文档给的公式：
 *
 *     VA2B2 = V5 - V6
 *     VA1B1 = V9 - V10
 *
 *              1  [                 VA1B1                VA2B2      ]
 *     L1 = -------| r*L + 4000 * ----------- - 4000 * ----------- |
 *             2r  [               VA1B1-3.3            VA2B2-3.3     ]
 *
 * 这一段是整个项目的核心，也是阶段一要验证的东西。公式本身照抄不改，
 * 只补齐文档没写的边界保护。
 *============================================================================*/

/**
 * @brief  计算水浸点距控制器的距离
 * @param  v      已采好的电压表，用到 v5,v6,v9,v10
 * @param  out_mm 输出距离，单位毫米
 * @return 1 = 结果有效，0 = 无法计算
 *
 * @note   三个必须挡住的情况，文档里都没提：
 *
 *         1) r <= 0 -> 公式的 1/(2r) 除零。r 是用户设的参数，必须校验。
 *
 *         2) VA1B1 或 VA2B2 接近 3.3V -> 分母 (V - 3.3) 趋近 0，商爆掉。
 *            物理上这对应"压差等于满幅"，也就是取样支路没有电流 —— 干燥
 *            状态才会这样，此时本来就没有漏水点可定位。所以这不是要
 *            规避的数值问题，而是"没有有效数据"的正常返回。
 *            阈值取 50mV：3.3V 时 1 LSB 是 0.8mV，50mV 留了足够余量，
 *            保证分母至少有 50mV 量级，商不会离谱。
 *
 *         3) 算出来的距离超出 [0, L] -> 说明标定参数不对或者电压不可信。
 *            这里不夹紧到边界，而是直接返回失败：夹紧会让"参数标错"
 *            伪装成"漏水点正好在线缆头/尾"，掩盖真问题。
 *
 *         整个函数是唯一用 float 的地方 —— 公式里有除法，整数做不了。
 *         结果转成毫米整数返回，调用方还是不用碰浮点打印。
 */
uint8_t Measure_CalcL1(const MeasVolt_t *v, int32_t *out_mm)
{
    float va1b1, va2b2;
    float term1, term2;
    float vcc, l1_m;

    if ((v == 0) || (out_mm == 0)) { return 0U; }

    /* 情况 1：r 非法 */
    if (s_r_ohm_per_m <= 0.0f) { return 0U; }

    vcc = (float)MEAS_VCC_MV / 1000.0f;            /* 3.3V */

    /* mV -> V */
    va2b2 = (float)(v->v5 - v->v6) / 1000.0f;
    va1b1 = (float)(v->v9 - v->v10) / 1000.0f;

    /* 情况 2：分母趋零。50mV 门限，两个都要查。 */
    if ((va1b1 - vcc > -0.050f) && (va1b1 - vcc < 0.050f)) { return 0U; }
    if ((va2b2 - vcc > -0.050f) && (va2b2 - vcc < 0.050f)) { return 0U; }

    term1 = MEAS_R_SAMPLE_OHM * (va1b1 / (va1b1 - vcc));
    term2 = MEAS_R_SAMPLE_OHM * (va2b2 / (va2b2 - vcc));

    l1_m = (s_r_ohm_per_m * s_cable_len_m + term1 - term2)
           / (2.0f * s_r_ohm_per_m);

    /* 情况 3：超出线缆物理范围 */
    if ((l1_m < 0.0f) || (l1_m > s_cable_len_m)) { return 0U; }

    *out_mm = (int32_t)(l1_m * 1000.0f);
    return 1U;
}

/*==============================================================================
 * 串口输出
 *
 * 全部用整数格式化，不出现 %f。见 Measure.h 里关于 MicroLIB 的说明。
 * 电压按 "整数部分.三位小数" 手工拆开打，看起来像浮点但走的是整数路径：
 *     1234mV -> printf("%d.%03d", 1234/1000, 1234%1000) -> "1.234"
 *============================================================================*/

/**
 * @brief  打印 V1~V10
 * @note   阶段一验证时对着这张表看。同时打 mV 整数和 V 形式，
 *         mV 便于和万用表逐位核对，V 便于快速判断量级。
 */
void Measure_PrintVolt(const MeasVolt_t *v)
{
    if (v == 0) { return; }

    printf("\r\n---- V1~V10 (mV) ----\r\n");
    printf("V1  黑线(断线1) = %5d  (%d.%03dV)\r\n",
           (int)v->v1, (int)(v->v1 / 1000), (int)(v->v1 % 1000));
    printf("V2  黄线(断线1) = %5d  (%d.%03dV)\r\n",
           (int)v->v2, (int)(v->v2 / 1000), (int)(v->v2 % 1000));
    printf("V3  绿线(断线2) = %5d  (%d.%03dV)\r\n",
           (int)v->v3, (int)(v->v3 / 1000), (int)(v->v3 % 1000));
    printf("V4  红线(断线2) = %5d  (%d.%03dV)\r\n",
           (int)v->v4, (int)(v->v4 / 1000), (int)(v->v4 % 1000));
    printf("V5  黄线(水浸)  = %5d  (%d.%03dV)\r\n",
           (int)v->v5, (int)(v->v5 / 1000), (int)(v->v5 % 1000));
    printf("V6  绿线(水浸)  = %5d  (%d.%03dV)\r\n",
           (int)v->v6, (int)(v->v6 / 1000), (int)(v->v6 % 1000));
    printf("V7  黄线(定位1) = %5d  (%d.%03dV)\r\n",
           (int)v->v7, (int)(v->v7 / 1000), (int)(v->v7 % 1000));
    printf("V8  绿线(定位1) = %5d  (%d.%03dV)\r\n",
           (int)v->v8, (int)(v->v8 / 1000), (int)(v->v8 % 1000));
    printf("V9  红线(定位2) = %5d  (%d.%03dV)\r\n",
           (int)v->v9, (int)(v->v9 / 1000), (int)(v->v9 % 1000));
    printf("V10 黑线(定位2) = %5d  (%d.%03dV)\r\n",
           (int)v->v10, (int)(v->v10 / 1000), (int)(v->v10 % 1000));
}

/**
 * @brief  打印判定结果 + 中间量 + 定位距离
 * @note   把 VA1B1 / VA2B2 也打出来，这两个是公式的直接输入。
 *         标定时如果 L1 不对，先看这两个中间量对不对，能快速分清
 *         "采集有问题"和"公式/参数有问题"。
 */
void Measure_PrintResult(const MeasVolt_t *v)
{
    uint8_t  fault;
    uint8_t  leak;
    int32_t  va1b1_mv, va2b2_mv;
    int32_t  l1_mm;

    if (v == 0) { return; }

    fault = Measure_CheckBreak(v);
    leak  = Measure_CheckLeak(v);

    printf("---- 判定 ----\r\n");

    if (fault == FAULT_NONE)
    {
        printf("断线: 正常 (V1-V2=%d mV, V3-V4=%d mV)\r\n",
               (int)(v->v1 - v->v2), (int)(v->v3 - v->v4));
    }
    else
    {
        printf("断线: 故障");
        if (fault & (uint8_t)FAULT_BREAK_YEL) { printf(" [黄线回路]"); }
        if (fault & (uint8_t)FAULT_BREAK_RED) { printf(" [红线回路]"); }
        printf("  (V1-V2=%d mV, V3-V4=%d mV)\r\n",
               (int)(v->v1 - v->v2), (int)(v->v3 - v->v4));
    }

    printf("水浸: %s\r\n", leak ? "检测到" : "干燥");

    /* 中间量，标定时排错用 */
    va2b2_mv = v->v5 - v->v6;
    va1b1_mv = v->v9 - v->v10;
    printf("VA1B1 = %d mV   VA2B2 = %d mV\r\n",
           (int)va1b1_mv, (int)va2b2_mv);

    if (Measure_CalcL1(v, &l1_mm))
    {
        printf("定位: L1 = %d.%03d m\r\n",
               (int)(l1_mm / 1000), (int)(l1_mm % 1000));
    }
    else
    {
        /* 干燥时必然走这条分支，不是错误 */
        printf("定位: 无有效结果 (干燥、参数未标定或超出线缆范围)\r\n");
    }
}

/**
 * @brief  阶段二：综合判定（断线 + 水浸 + 定位）
 * @note   一次性返回所有结果，供状态机使用
 */
void Measure_Judge(const MeasVolt_t *v, MeasResult_t *result)
{
    uint8_t fault;

    if ((v == 0) || (result == 0)) { return; }

    /* 清空结果 */
    result->break_detected = 0;
    result->leak_detected = 0;
    result->pos_valid = 0;
    result->pos_mm = 0;
    result->break_line = "";
    result->fault_flags = 0;

    /* 断线判定 */
    fault = Measure_CheckBreak(v);
    if (fault != FAULT_NONE)
    {
        result->break_detected = 1;
        result->fault_flags = fault;

        /* 确定断线的线路名称 */
        if (fault & (uint8_t)FAULT_BREAK_YEL)
        {
            result->break_line = "黄线回路";
        }
        else if (fault & (uint8_t)FAULT_BREAK_RED)
        {
            result->break_line = "红线回路";
        }
    }

    /* 水浸判定 */
    result->leak_detected = Measure_CheckLeak(v);

    /* 定位计算（仅水浸时有效） */
    if (result->leak_detected)
    {
        result->pos_valid = Measure_CalcL1(v, &result->pos_mm);
    }
}
