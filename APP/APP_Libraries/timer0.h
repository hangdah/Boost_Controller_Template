/*
 * timer0.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_TIMER0_H_
#define APP_APP_LIBRARIES_TIMER0_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

void Timer0_Init(float Freq, float Period);
interrupt void Timer0_IRQn(void);



#endif /* APP_APP_LIBRARIES_TIMER0_H_ */
