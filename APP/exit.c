/*
 * exit.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "exit.h"
#include "led.h"
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

void EXTI1_Init(void)
{
    EALLOW;
    SysCtrlRegs.PCLKCR3.bit.GPIOINENCLK = 1;    // GPIO input clock
    EDIS;

    EALLOW;
    //KEY端口配置
    GpioCtrlRegs.GPAMUX1.bit.GPIO12=0;
    GpioCtrlRegs.GPADIR.bit.GPIO12=0;
    GpioCtrlRegs.GPAPUD.bit.GPIO12=0;
    GpioCtrlRegs.GPAQSEL1.bit.GPIO12 = 0;        // 外部中断1（XINT1）与系统时钟SYSCLKOUT同步

    GpioCtrlRegs.GPBMUX2.bit.GPIO48=0;
    GpioCtrlRegs.GPBDIR.bit.GPIO48=1;
    GpioCtrlRegs.GPBPUD.bit.GPIO48=0;
    GpioDataRegs.GPBCLEAR.bit.GPIO48=1;
    EDIS;

    EALLOW;
    GpioIntRegs.GPIOXINT1SEL.bit.GPIOSEL = 12;   // XINT1是GPIO12
    EDIS;

    EALLOW; // 修改被保护的寄存器，修改前应添加EALLOW语句
    PieVectTable.XINT1 = &EXTI1_IRQn;
    EDIS;   // EDIS的意思是不允许修改被保护的寄存器

    PieCtrlRegs.PIEIER1.bit.INTx4 = 1;          // 使能PIE组1的INT4

    XIntruptRegs.XINT1CR.bit.POLARITY = 0;      // 下降沿触发中断
    XIntruptRegs.XINT1CR.bit.ENABLE= 1;        // 使能XINT1

    IER |= M_INT1;                              // 使能CPU中断1（INT1）
    EINT;                                       // 开全局中断
    ERTM;
}

interrupt void EXTI1_IRQn(void)
{
    Uint32 i;
    for(i=0;i<10000;i++);    //键盘消抖动
//    while(!KEY_H1);
//    LED2_TOGGLE;
    PieCtrlRegs.PIEACK.bit.ACK1=1;
}

void EXTI2_Init(void)
{
    EALLOW;
    SysCtrlRegs.PCLKCR3.bit.GPIOINENCLK = 1;    // GPIO input clock
    EDIS;

    EALLOW;
    //KEY端口配置
    GpioCtrlRegs.GPAMUX1.bit.GPIO13=0;
    GpioCtrlRegs.GPADIR.bit.GPIO13=0;
    GpioCtrlRegs.GPAPUD.bit.GPIO13=0;
    GpioCtrlRegs.GPAQSEL1.bit.GPIO13 = 2;        // 外部中断2（XINT2）输入限定6个采样窗口
    GpioCtrlRegs.GPACTRL.bit.QUALPRD1 = 0xFF;   // 每个采样窗口的周期为510*SYSCLKOUT

    GpioCtrlRegs.GPBMUX2.bit.GPIO48=0;
    GpioCtrlRegs.GPBDIR.bit.GPIO48=1;
    GpioCtrlRegs.GPBPUD.bit.GPIO48=0;
    GpioDataRegs.GPBCLEAR.bit.GPIO48=1;
    EDIS;

    EALLOW;
    GpioIntRegs.GPIOXINT2SEL.bit.GPIOSEL = 13;   // XINT2是GPIO13
    EDIS;

    EALLOW; // 修改被保护的寄存器，修改前应添加EALLOW语句
    PieVectTable.XINT2 = &EXTI2_IRQn;
    EDIS;   // EDIS的意思是不允许修改被保护的寄存器

    PieCtrlRegs.PIEIER1.bit.INTx5 = 1;          // 使能PIE组1的INT5

    XIntruptRegs.XINT2CR.bit.POLARITY = 0;      // 下降沿触发中断
    XIntruptRegs.XINT2CR.bit.ENABLE = 1;        // 使能XINT2

    IER |= M_INT1;                              // 使能CPU中断1（INT1）
    EINT;                                       // 开全局中断
    ERTM;
}

interrupt void EXTI2_IRQn(void)
{
    Uint32 i;
    for(i=0;i<10000;i++);    //键盘消抖动
//    while(!KEY_H2);
    LED3_TOGGLE;
    PieCtrlRegs.PIEACK.bit.ACK1=1;
}


