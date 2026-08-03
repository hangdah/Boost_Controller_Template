/*
 * exit.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_EXIT_H_
#define APP_APP_LIBRARIES_EXIT_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

void EXTI1_Init(void);
interrupt void EXTI1_IRQn(void);
void EXTI2_Init(void);
interrupt void EXTI2_IRQn(void);



#endif /* APP_APP_LIBRARIES_EXIT_H_ */
