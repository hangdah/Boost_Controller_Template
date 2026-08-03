/*
 * epwm.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_EPWM_H_
#define APP_APP_LIBRARIES_EPWM_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

void EPWM1_Init(Uint16 tbprd);
void EPWM_TimeBase_Freeze(void);                           // 冻结所有ePWM时基计数器
void EPWM_TimeBase_Start(void);                            // 解除冻结并启动所有ePWM时基计数器

interrupt void epwm1_isr(void);

void EPwm1A_SetCompare(Uint16 val);
void EPwm1B_SetCompare(Uint16 val);



#endif /* APP_APP_LIBRARIES_EPWM_H_ */
