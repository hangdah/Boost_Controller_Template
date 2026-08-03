/*
 * timer1.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "timer1.h"
#include "led.h"
#include "function.h" // 包含模块化后的头文件

// --- 模块化重构核心 ---
// 声明在 main.c 中定义的外部 "getter" 函数
extern BoostController* GetBoostHandle(void);
// ----------------------

#pragma CODE_SECTION(Timer1_IRQn, ".TI.ramfunc")

// Timer1_Init 函数保持不变
void Timer1_Init(float Freq, float Period)          // Freq: CPU 时钟频率，单位是 MHz; Period: 定时器周期，单位是 μs
{
    EALLOW;                                         // 允许访问受保护寄存器
    SysCtrlRegs.PCLKCR3.bit.CPUTIMER1ENCLK = 1;     // 使能CPU Timer1外设时钟
    EDIS;                                           // 禁止访问受保护寄存器

    EALLOW;                                         // 允许访问受保护寄存器
    PieVectTable.XINT13 = &Timer1_IRQn;             // 设置Timer1中断服务函数
    EDIS;                                           // 禁止访问受保护寄存器

    CpuTimer1.RegsAddr = &CpuTimer1Regs;            // 关联Timer1硬件寄存器
    CpuTimer1Regs.PRD.all = 0xFFFFFFFF;             // 设置周期寄存器初始值
    CpuTimer1Regs.TPR.all = 0;                      // 清零低位预分频寄存器
    CpuTimer1Regs.TPRH.all = 0;                     // 清零高位预分频寄存器
    CpuTimer1Regs.TCR.bit.TSS = 1;                  // 初始化期间停止Timer1
    CpuTimer1Regs.TCR.bit.TRB = 1;                  // 重新装载Timer1计数器
    CpuTimer1.InterruptCount = 0;                   // 清零软件中断计数值

    ConfigCpuTimer(&CpuTimer1, Freq, Period);       // 配置Timer1的计数周期
    CpuTimer1Regs.TCR.bit.TSS = 1;                  // 配置完成后仍保持停止状态

    IER |= M_INT13;                                 // 使能CPU INT13中断
}

void Timer1_Start(void)
{
    CpuTimer1Regs.TCR.bit.TSS = 1;                  // 启动前先停止Timer1
    CpuTimer1Regs.TCR.bit.TIF = 1;                  // 清除可能残留的Timer1中断标志
    CpuTimer1Regs.TCR.bit.TRB = 1;                  // 重新装载周期值并从头开始计数
    CpuTimer1Regs.TCR.bit.TSS = 0;                  // 启动Timer1计数
}
void Timer1_Freeze(void)
{
    CpuTimer1Regs.TCR.bit.TSS = 1;                  // 停止并冻结Timer1计数器
}

interrupt void Timer1_IRQn(void)
{

    BoostController *p = GetBoostHandle();  // 获取指向主控制器的指针
    EALLOW;
    LED1_TOGGLE;
//    SlowP(p);
    State_M(p);
    EDIS;

}
