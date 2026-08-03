/*
 * timer0.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "timer0.h"
#include "function.h" // 包含模块化后的头文件
#include "led.h"

// --- 模块化重构核心 ---
// 声明在 main.c 中定义的外部 "getter" 函数
extern BoostController* GetBoostHandle(void);
// ----------------------

// Timer0_Init 函数保持不变
void Timer0_Init(float Freq, float Period)
{
    EALLOW;
    SysCtrlRegs.PCLKCR3.bit.CPUTIMER0ENCLK = 1; // CPU Timer 0
    EDIS;

    EALLOW;
    PieVectTable.TINT0 = &Timer0_IRQn;
    EDIS;

    // CPU Timer 0
    CpuTimer0.RegsAddr = &CpuTimer0Regs;
    CpuTimer0Regs.PRD.all  = 0xFFFFFFFF;
    CpuTimer0Regs.TPR.all  = 0;
    CpuTimer0Regs.TPRH.all = 0;
    CpuTimer0Regs.TCR.bit.TSS = 1;
    CpuTimer0Regs.TCR.bit.TRB = 1;
    CpuTimer0.InterruptCount = 0;

    ConfigCpuTimer(&CpuTimer0, Freq, Period);
    CpuTimer0Regs.TCR.bit.TSS=0;

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1;

    EINT;
    ERTM;
}

interrupt void Timer0_IRQn(void)
{
    // --- 模块化重构核心 ---
    // 获取指向主控制器的指针
    BoostController *p = GetBoostHandle();
    // ----------------------

    // 调用模块化的函数并传入指针
    Adc_Sample_Process(p);

    // 注意：在这个设计中，PI控制器和PWM更新通常在ADC中断中完成。
    // 如果你确实需要Timer0作为主控制中断，你需要将PI和PWM更新调用移到这里。
    // V_PI_Controller_ComputeIncremental_AntiWindup(p);
    // I_PI_Controller_ComputeIncremental_AntiWindup(p);
    // PWM_Update(p);

    // 清除中断标志
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
