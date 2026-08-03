/*
 * led.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "led.h"

void LED_Init(void){

    EALLOW;//关闭写保护
    SysCtrlRegs.PCLKCR3.bit.GPIOINENCLK=1;//开启GPIO时钟
    //LED1 端口配置
    GpioCtrlRegs.GPCMUX1.bit.GPIO68=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPCDIR.bit.GPIO68=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPCPUD.bit.GPIO68=0;//使能GPIO上拉电阻
    GpioDataRegs.GPCSET.bit.GPIO68=1;//设置GPIO输出为高电平
    //LED2 端口配置
    GpioCtrlRegs.GPCMUX1.bit.GPIO67=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPCDIR.bit.GPIO67=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPCPUD.bit.GPIO67=0;//使能GPIO上拉电阻
    GpioDataRegs.GPCSET.bit.GPIO67=1;//设置GPIO输出为高电平
    //LED3 端口配置
    GpioCtrlRegs.GPCMUX1.bit.GPIO66=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPCDIR.bit.GPIO66=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPCPUD.bit.GPIO66=0;//使能GPIO上拉电阻
    GpioDataRegs.GPCSET.bit.GPIO66=1;//设置GPIO输出为高电平
    //LED4 端口配置
    GpioCtrlRegs.GPCMUX1.bit.GPIO65=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPCDIR.bit.GPIO65=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPCPUD.bit.GPIO65=0;//使能GPIO上拉电阻
    GpioDataRegs.GPCSET.bit.GPIO65=1;//设置GPIO输出为高电平
    //LED5 端口配置
    GpioCtrlRegs.GPCMUX1.bit.GPIO64=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPCDIR.bit.GPIO64=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPCPUD.bit.GPIO64=0;//使能GPIO上拉电阻
    GpioDataRegs.GPCSET.bit.GPIO64=1;//设置GPIO输出为高电平
    //LED6 端口配置
    GpioCtrlRegs.GPAMUX1.bit.GPIO10=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPADIR.bit.GPIO10=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPAPUD.bit.GPIO10=0;//使能GPIO上拉电阻
    GpioDataRegs.GPASET.bit.GPIO10=1;//设置GPIO输出为高电平
    //LED7 端口配置
    GpioCtrlRegs.GPAMUX1.bit.GPIO11=0;//设置为通用PGIO功能
    GpioCtrlRegs.GPADIR.bit.GPIO11=1;//设置GPIO方向为输出
    GpioCtrlRegs.GPAPUD.bit.GPIO11=0;//使能GPIO上拉电阻
    GpioDataRegs.GPASET.bit.GPIO11=1;//设置GPIO输出为高电平

    EDIS;//开启写保护
}



