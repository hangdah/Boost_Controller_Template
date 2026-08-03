/*
 * epwm.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "epwm.h"
#include "exit.h"
#include "led.h"
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

#pragma CODE_SECTION(EPwm1A_SetCompare, ".TI.ramfunc")

void EPwm1A_SetCompare(Uint16 val)
{
    EPwm1Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm1B_SetCompare(Uint16 val)
{
    EPwm1Regs.CMPB = val;  //设置占空比
}
void EPwm2A_SetCompare(Uint16 val)
{
    EPwm2Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm2B_SetCompare(Uint16 val)
{
    EPwm2Regs.CMPB = val;  //设置占空比
}
void EPwm3A_SetCompare(Uint16 val)
{
    EPwm3Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm3B_SetCompare(Uint16 val)
{
    EPwm3Regs.CMPB = val;  //设置占空比
}
void EPwm4A_SetCompare(Uint16 val)
{
    EPwm4Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm4B_SetCompare(Uint16 val)
{
    EPwm4Regs.CMPB = val;  //设置占空比
}
void EPwm5A_SetCompare(Uint16 val)
{
    EPwm5Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm5B_SetCompare(Uint16 val)
{
    EPwm5Regs.CMPB = val;  //设置占空比
}
void EPwm6A_SetCompare(Uint16 val)
{
    EPwm6Regs.CMPA.half.CMPA = val;  //设置占空比
}
void EPwm6B_SetCompare(Uint16 val)
{
    EPwm6Regs.CMPB = val;  //设置占空比
}



void EPWM1_Init(Uint16 tbprd){

    InitEPwm1Gpio();
    //初始化时基模块
    //SETUP SYNC
    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_DISABLE;
    //ALLOW EACH TIMER TO BESYNC'ED
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE;
    EPwm1Regs.TBPHS.half.TBPHS = 0;
    EPwm1Regs.TBCTR = 0x0000;
    EPwm1Regs.TBPRD = tbprd;
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN;
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1;
    EPwm1Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm1Regs.TBCTL.bit.PRDLD = TB_SHADOW;

    //初始化比较模块，CC
    //setup shadow register load on zero
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;     //影子装载模式
    EPwm1Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;     //影子装载
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO_PRD;   //CTR = 0与CTR = PRD时装载
    EPwm1Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO_PRD;   //CTR = 0与CTR = PRD时装载

    //初始化动作模块，AQ
    //set actions
    EPwm1Regs.AQCTLA.bit.ZRO = AQ_NO_ACTION;
    EPwm1Regs.AQCTLA.bit.CAU = AQ_CLEAR;
    EPwm1Regs.AQCTLA.bit.CAD = AQ_SET;

    EPwm1Regs.AQCTLB.bit.ZRO = AQ_NO_ACTION;
    EPwm1Regs.AQCTLB.bit.CBU = AQ_CLEAR;
    EPwm1Regs.AQCTLB.bit.CBD = AQ_SET;

    //初始化死区模块，DB
    EPwm1Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE;      //对epwmA还是epwmB有效
    EPwm1Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC;           //db输出极性选择，其他值可能会出现极性反转
    EPwm1Regs.DBCTL.bit.IN_MODE = DBA_ALL;      //
    //初始化斩波模块，PC

    //错误联防模块，TZ
    EPwm1Regs.TZSEL.bit.OSHT1 = TZ_ENABLE;          // TZ1 will be one shot signal for EPWM1
    EPwm1Regs.TZCTL.bit.TZA = TZ_FORCE_LO;          // 错误事件发生时，强制ePWM1A低状态
    EPwm1Regs.TZCTL.bit.TZB = TZ_FORCE_LO;          // 错误事件发生时，强制ePWM1B低状态
    EPwm1Regs.TZCLR.all = 0xffff;                   // TZ中断标志位全部清0；
    EPwm1Regs.TZEINT.all = 0;                       // 中断均不使能；

    // ePWM产生SOCA信号 (用于波谷采样)
    EPwm1Regs.ETSEL.bit.SOCAEN = 1;        // 使能 ePWM1 的 SOCA 事件
    EPwm1Regs.ETSEL.bit.SOCASEL = ET_CTR_ZERO; // <--- BUG修复: 在计数器为0时触发 (波谷)
    EPwm1Regs.ETPS.bit.SOCAPRD = ET_1ST;     // 每个事件都触发

    // ePWM产生SOCB信号 (用于波峰采样)
    EPwm1Regs.ETSEL.bit.SOCBEN = 1;        // <--- BUG修复: 使能 ePWM1 的 SOCB 事件 (不是SOCAEN)
    EPwm1Regs.ETSEL.bit.SOCBSEL = ET_CTR_PRD;  // <--- BUG修复: 在计数器等于周期值时触发 (波峰) (不是SOCASEL)
    EPwm1Regs.ETPS.bit.SOCBPRD = ET_1ST;     // <--- BUG修复: 设置SOCB的预分频 (不是SOCAPRD)

    EDIS;

}

interrupt void epwm1_isr(void){
    // 1. 采样ADC数据（一般是ADC中断先采样并暂存在变量中）
//    float vin = ADC_Read_Vin();     // 比如你映射 ADCRESULT0
//    float vout = ADC_Read_Vout();   // 比如你映射 ADCRESULT1

    // 2. PI控制器更新占空比
//    float error = Vout_Ref - vout;
//    duty = PI_Controller(error);

    // 3. 更新PWM占空比（修改CMPA）
//    EPwm1Regs.CMPA.half.CMPA = (Uint16)(duty * tbprd); // duty 是 0~1 的比例

    // 4. 清除中断标志
    EPwm1Regs.ETCLR.bit.INT = 1;                //清除EPWM1模块的中断标志，这是中断处理的必要步骤，否则中断会一直被触发。
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;     //向中断控制器（PIE）确认中断已经处理完毕，允许接收来自同一中断组（Group 3）的后续中断。
}

void EPWM_TimeBase_Freeze(void)
{
    EALLOW;                                                // 允许访问受保护寄存器
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;                // 冻结所有ePWM模块的时基计数器
    EDIS;                                                  // 禁止访问受保护寄存器
}

void EPWM_TimeBase_Start(void)
{
    EALLOW;                                                // 允许访问受保护寄存器
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;                // 解除冻结，使所有ePWM时基计数器开始工作
    EDIS;                                                  // 禁止访问受保护寄存器
}


