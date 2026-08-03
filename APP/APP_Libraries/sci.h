/*
 * sci.h
 *
 * SCI-A轮询通信驱动。
 */

#ifndef APP_APP_LIBRARIES_SCI_H_
#define APP_APP_LIBRARIES_SCI_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

#ifndef SCI_COMM_ENABLE
#define SCI_COMM_ENABLE      1U
#endif

#if SCI_COMM_ENABLE

#define SCIA_LSPCLK_HZ       37500000UL
#define SCIA_DEFAULT_BAUD    4800UL

void SCIA_Init(Uint32 baud);
void SCIA_SendByte(Uint16 data);
void SCIA_SendString(const char *message);
Uint16 SCIA_TryReceiveByte(Uint16 *data);
void SCIA_BackgroundTask(void);

#endif /* SCI_COMM_ENABLE */

#endif /* APP_APP_LIBRARIES_SCI_H_ */
