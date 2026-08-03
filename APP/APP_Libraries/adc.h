/*
 * adc.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_ADC_H_
#define APP_APP_LIBRARIES_ADC_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

#define ADC_MODCLK 3

void ADC_Init(void);
Uint16 Read_ADC_CH0_Value(void);
interrupt void  adc_isr(void);



#endif /* APP_APP_LIBRARIES_ADC_H_ */
