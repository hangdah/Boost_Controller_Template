/*
 * adc.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "adc.h"
#include "led.h"
#include "function.h" // 包含模块化后的头文件
#include "dac.h"
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

// --- 模块化重构核心 ---
// 声明在 main.c 中定义的外部 "getter" 函数
// 这使得本文件可以安全地访问到主控制器句柄
extern BoostController* GetBoostHandle(void);
// ----------------------


// ADC_Init 函数保持不变，因为你要求不修改初始化代码
void ADC_Init(void)
{

    // 1. 核心ADC寄存器配置
    InitAdc();

    EALLOW;

    // 2. 配置ADC时钟
    AdcRegs.ADCTRL3.bit.ADCCLKPS = ADC_MODCLK;

    // 3. 配置中断向量表
    PieVectTable.ADCINT = &adc_isr;
    PieCtrlRegs.PIEIER1.bit.INTx6 = 1;
    IER |= M_INT1;

    // 4. 设置转换模式
    AdcRegs.ADCTRL1.bit.SEQ_CASC    = 0;    // 0: 双排序器模式，SEQ1和SEQ2独立运行; 1：级联模式，SEQ1和SEQ2组成一个16状态排序器
    AdcRegs.ADCTRL1.bit.CONT_RUN    = 0;    // Continuous Run。0 = 单次运行模式。ADC排序器只在收到触发信号时运行一次。1 = 连续运行模式。ADC排序器完成一次转换后会立即自动开始下一次，忽略触发信号。
    AdcRegs.ADCTRL1.bit.ACQ_PS      = 7;    // Acquisition (Sample) Window Prescaler。采样时间 = (ACQ_PS + 1) * ADCCLK周期。
    AdcRegs.ADCTRL3.bit.SMODE_SEL   = 1;    // 0 = 顺序采样模式 (Sequential Sampling Mode)。1 = 同步采样模式 (Simultaneous Sampling Mode)

    // 5. 配置 SEQ1 (由 ePWM_SOCA 触发)
    AdcRegs.ADCMAXCONV.bit.MAX_CONV1 = 0x3;
    AdcRegs.ADCCHSELSEQ1.bit.CONV00  = 0x0;
    AdcRegs.ADCCHSELSEQ1.bit.CONV01  = 0x1;
    AdcRegs.ADCCHSELSEQ1.bit.CONV02  = 0x8;
    AdcRegs.ADCCHSELSEQ1.bit.CONV03  = 0x9;

    // 6. 配置 SEQ2 (由 ePWM_SOCB 触发)
    AdcRegs.ADCMAXCONV.bit.MAX_CONV2 = 0x3;
    AdcRegs.ADCCHSELSEQ2.bit.CONV04  = 0x0;
    AdcRegs.ADCCHSELSEQ2.bit.CONV05  = 0x1;
    AdcRegs.ADCCHSELSEQ2.bit.CONV06  = 0x8;
    AdcRegs.ADCCHSELSEQ2.bit.CONV07  = 0x9;

    // 7. 设置触发源和中断使能
    AdcRegs.ADCTRL2.bit.EPWM_SOCA_SEQ1 = 1; // 0: 禁止ePWM SOCA触发SEQ1; 1: 允许ePWM SOCA触发SEQ1
    AdcRegs.ADCTRL2.bit.INT_ENA_SEQ1   = 1; // 0: 禁止SEQ1向CPU请求中断;   1: 允许SEQ1向CPU请求中断
    AdcRegs.ADCTRL2.bit.EPWM_SOCB_SEQ2 = 1; // 0: 禁止ePWM SOCB触发SEQ2; 1: 允许ePWM SOCB触发SEQ2
    AdcRegs.ADCTRL2.bit.INT_ENA_SEQ2   = 1; // 0: 禁止SEQ2向CPU请求中断;   1: 允许SEQ2向CPU请求中断

    EDIS;
}

#pragma CODE_SECTION(adc_isr, ".TI.ramfunc")
interrupt void adc_isr(void)
{
    BoostController *p = GetBoostHandle();

    if (AdcRegs.ADCST.bit.INT_SEQ1 == 1)
    {
        // 1. ADC采样
        Process_Valley_Samples(p);
        // 2. 环路计算
        Control_Loop_Compute(p);
        // 3. PWM更新
        PWM_Update(p);
        // 4. DAC数据Get
#if DAC_DEBUG_ENABLE
        DacDebug_DataGet(&DAC_Debug_1, p->pwm.Duty);
#endif

        AdcRegs.ADCST.bit.INT_SEQ1_CLR = 1;
    }
    else if (AdcRegs.ADCST.bit.INT_SEQ2 == 1)
    {
        // 1. ADC采样
        Process_Peak_Samples(p);
        // 2. 环路计算
        Control_Loop_Compute(p);
        // 3. PWM更新
        PWM_Update(p);
        // 4. DAC数据Get
#if DAC_DEBUG_ENABLE
        DacDebug_DataGet(&DAC_Debug_1, p->pwm.Duty);
#endif

        AdcRegs.ADCST.bit.INT_SEQ2_CLR = 1;
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
