/*==============================================================================
 * 阶段切换开关
 *
 * 在 Keil 的 Options -> C/C++ -> Preprocessor Symbols -> Define 里添加：
 *   - 不定义任何宏 = 默认阶段一
 *   - 定义 STAGE_TWO = 阶段二
 *
 * 或者在这里修改 #define 的值（0 = 阶段一，1 = 阶段二）
 *============================================================================*/
#ifndef STAGE_TWO
  #define STAGE_TWO  0      /* 0 = 阶段一验证，1 = 阶段二运行 */
#endif

#if STAGE_TWO
  /* 阶段二：正式运行 */
  #include "stm32f10x.h"
  #include "Com_Board.h"
  #include "Com_Delay.h"
  #include "Com_Uart.h"
  #include "Int_Buzzer.h"
  #include "Int_Relay.h"
  #include "Int_Rs485.h"
  #include "App_Measure.h"
  #include "App_State.h"
  #include "App_Config.h"
  #include <stdio.h>

  static void PrintBanner(void)
  {
      printf("\r\n\r\n");
      printf("========================================\r\n");
      printf(" 定位式水浸检测 - 阶段二 正式运行\r\n");
      printf(" MCU: STM32F103C8T6 @ %d MHz\r\n", (int)(SystemCoreClock / 1000000U));
      printf("========================================\r\n");
      printf(" 线缆参数\r\n");
      printf("   r = %d.%03d Ohm/m\r\n",
             (int)Measure_GetR(),
             (int)((Measure_GetR() - (float)(int)Measure_GetR()) * 1000.0f));
      printf("   L = %d m\r\n", (int)Measure_GetL());
      printf("========================================\r\n");
      printf("系统进入正常监测状态\r\n");
  }

  int main(void)
  {
      Board_Init();
      Delay_Init();
      Uart_Init();
      Buzzer_Init();
      Relay_Init();
      Rs485_Init();
      Measure_Init();
      Config_Init();    /* 从 Flash 加载参数 */
      State_Init();

      PrintBanner();

      printf("自检: 蜂鸣器短鸣\r\n");
      Buzzer_BlockingBeep(100);

      while (1)
      {
          State_Poll();
          Buzzer_Poll();
      }
  }

#else
  /* 阶段一：原理验证 */
  #include "stm32f10x.h"
  #include "Com_Board.h"
  #include "Com_Delay.h"
  #include "Com_Uart.h"
  #include "App_Measure.h"
  #include "Int_Buzzer.h"
  #include <stdio.h>

  #define LOOP_INTERVAL_MS    1000U

  static void PrintBanner(void)
  {
      printf("\r\n\r\n");
      printf("========================================\r\n");
      printf(" 定位式水浸检测 - 阶段一 原理验证\r\n");
      printf(" MCU: STM32F103C8T6 @ %d MHz\r\n", (int)(SystemCoreClock / 1000000U));
      printf(" ADC: 内部 12bit, PA1=网络ADC0, PA2=网络ADC1\r\n");
      printf("----------------------------------------\r\n");
      printf(" 线缆参数（★标定前结果无意义）\r\n");
      printf("   r = %d.%03d Ohm/m\r\n",
             (int)Measure_GetR(),
             (int)((Measure_GetR() - (float)(int)Measure_GetR()) * 1000.0f));
      printf("   L = %d m\r\n", (int)Measure_GetL());
      printf("   取样电阻常数 = %d Ohm (原理图 R11 = 3.9k)\r\n",
             (int)MEAS_R_SAMPLE_OHM);
      printf("----------------------------------------\r\n");
      printf(" 判定阈值\r\n");
      printf("   相等容差 = %d mV\r\n", MEAS_EQUAL_TOL_MV);
      printf("   零点门限 = %d mV\r\n", MEAS_ZERO_MV);
      printf("   满幅门限 = %d mV\r\n", MEAS_FULL_MV);
      printf("   切换稳定延时 = %d ms\r\n", WIRE_SETTLE_MS);
      printf("========================================\r\n");
  }

  int main(void)
  {
      MeasVolt_t volt;
      uint32_t   round = 0;

      Board_Init();
      Delay_Init();
      Uart_Init();
      Measure_Init();
      Buzzer_Init();

      PrintBanner();

      printf("自检: 蜂鸣器短鸣 100ms ...\r\n");
      Buzzer_BlockingBeep(100);

      while (1)
      {
          round++;
          printf("\r\n===== 第 %d 轮 (t=%d ms) =====\r\n",
                 (int)round, (int)Delay_GetTick());

          Measure_ReadAll(&volt);
          Measure_PrintVolt(&volt);
          Measure_PrintResult(&volt);

          Delay_ms(LOOP_INTERVAL_MS);
      }
  }
#endif
