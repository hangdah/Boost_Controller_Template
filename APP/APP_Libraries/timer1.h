/*
 * timer1.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_TIMER1_H_
#define APP_APP_LIBRARIES_TIMER1_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"


void Timer1_Init(float Freq, float Period);
void Timer1_Start(void);
void Timer1_Freeze(void);
interrupt void Timer1_IRQn(void);



#endif /* APP_APP_LIBRARIES_TIMER1_H_ */
