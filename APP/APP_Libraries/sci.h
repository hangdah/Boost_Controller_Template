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
#define SCIA_DEFAULT_BAUD    115200UL
#define SCIA_DEBUG_UPDATE_DIV 400U

/**
 * @brief SCI波形调试数据对象。
 * @note  ISR只更新快照和请求标志，字符串转换与发送由后台任务完成。
 */
typedef struct
{
    volatile float data_1;
    volatile float data_2;
    volatile float data_3;
    volatile float data_4;

    volatile Uint16 Request;
    Uint16 Prescaler;
    Uint16 UpdateDiv;
    volatile Uint16 DroppedFrames;
} SCIA_Debug;

extern SCIA_Debug SCIA_Debug_1;

void SCIA_Init(Uint32 baud);
void SCIA_SendByte(Uint16 data);
void SCIA_SendString(const char *message);
Uint16 SCIA_TryReceiveByte(Uint16 *data);
void SCIA_Debug_Init(SCIA_Debug *p_debug, Uint16 updateDiv);
void SCIA_Debug_Capture(SCIA_Debug *p_debug,
                        float data_1,
                        float data_2,
                        float data_3,
                        float data_4);
void SCIA_Debug_Service(SCIA_Debug *p_debug);

#endif /* SCI_COMM_ENABLE */

#endif /* APP_APP_LIBRARIES_SCI_H_ */
