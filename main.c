/**
 * @file    main.c
 * @brief   Boost 模板
 * @author  dah
 * @note    暂未验证可行
 */
#include "adc.h"
#include "timer0.h"
#include "timer1.h"
#include "epwm.h"
#include "led.h"
#include "mppt.h"
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"
#include "function.h"
#include <stdint.h>
#include "dac.h"

extern Uint16 RamfuncsLoadStart;
extern Uint16 RamfuncsLoadSize;
extern Uint16 RamfuncsRunStart;

// 全局 Boost 控制器实例，中断服务函数通过 GetBoostHandle() 访问
static BoostController boost_converter;

BoostController* GetBoostHandle(void)
{
    return &boost_converter;
}

void main()
{
    #ifdef _FLASHOK
    memcpy((uint16_t *)&RamfuncsRunStart,(uint16_t *)&RamfuncsLoadStart,(unsigned long)&RamfuncsLoadSize);
    #endif

    // --- 系统初始化 ---
    InitSysCtrl();

    #ifdef _FLASHOK
    InitFlash();
    #endif

    // --- 中断系统初始化 ---
    DINT;
    InitPieCtrl();
    IER = 0x0000;
    IFR = 0x0000;
    InitPieVectTable();
    EALLOW;
    PieCtrlRegs.PIEACK.all = 0xFFFF;
    EDIS;

    EPWM_TimeBase_Freeze();                                    // 冻结ePWM时基计数器
    Timer1_Freeze();                                           // 停止并冻结Timer1计数器
                                                               // Timer1_Freeze函数可删除，因为Timer初始化中默认冻结计数器
                                                               // 但是为了与ePWM对称，这里留着

    boost_converter_Init(&boost_converter);                    // 初始化Boost控制器结构体
    Control_Init(&boost_converter);                            // 初始化控制参数

    LED_Init();                                                // 初始化LED
    Timer1_Init(150.0f, 5000.0f);                              // 配置Timer1，但暂不启动
    EPWM1_Init(Get_TBPRD(&boost_converter));                   // 初始化ePWM1
    ADC_Init();                                                // 初始化ADC
#if DAC_DEBUG_ENABLE
    TLV5620_Init();                                            // 初始化外部TLV5620调试DAC
    DacDebug_Init(&DAC_Debug_1, 0.0f, 0.7f, 20U, 0U, 0U);     // DAC0输出Duty，约2kHz更新
#endif
    PWM_Disable();                                             // 禁止外部PWM波形输出

    EPWM_TimeBase_Start();                                     // 解除冻结，启动ePWM时基计数器
    Timer1_Start();                                            // 启动Timer1计数

    EINT;                                                      // 开启CPU全局中断
    ERTM;                                                      // 开启实时调试模式

    while (1)
    {
#if DAC_DEBUG_ENABLE
        DacDebug_Service(&DAC_Debug_1);                        // SPI发送仅在后台执行
#endif
    }
}
