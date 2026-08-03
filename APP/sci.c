/*
 * sci.c
 *
 * SCI-A轮询通信驱动，使用GPIO35发送、GPIO36接收。
 */

#include "sci.h"

#if SCI_COMM_ENABLE

static void SCIA_InitGpio(void);

/**
 * @brief  初始化SCI-A，配置为8位数据、无校验、1位停止位。
 * @param  baud  通信波特率。
 */
void SCIA_Init(Uint32 baud)
{
    Uint32 baudRegister;

    // 检查波特率是否能够转换成有效的16位寄存器值
    if ((baud == 0UL) || (baud > (SCIA_LSPCLK_HZ / 8UL)))
    {
        return;
    }

    baudRegister = (SCIA_LSPCLK_HZ / (8UL * baud)) - 1UL;
    if (baudRegister > 0xFFFFUL)
    {
        return;
    }

    // 使能SCI-A外设时钟并配置对应GPIO
    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.SCIAENCLK = 1;
    EDIS;
    SCIA_InitGpio();

    // 配置期间保持SCI处于软件复位状态
    SciaRegs.SCICTL1.all = 0x0003;
    SciaRegs.SCICCR.all = 0x0007;
    SciaRegs.SCICTL2.all = 0x0000;

    // 根据当前37.5MHz低速外设时钟设置波特率
    SciaRegs.SCIHBAUD = (Uint16)((baudRegister >> 8) & 0x00FFUL);
    SciaRegs.SCILBAUD = (Uint16)(baudRegister & 0x00FFUL);

    // 启用收发FIFO，但不启用SCI中断
    SciaRegs.SCIFFTX.all = 0xE040;
    SciaRegs.SCIFFRX.all = 0x204F;
    SciaRegs.SCIFFCT.all = 0x0000;

    // 使能收发功能并退出软件复位
    SciaRegs.SCICTL1.all = 0x0023;
}

/**
 * @brief  通过SCI-A发送一个字节。
 */
void SCIA_SendByte(Uint16 data)
{
    // FIFO满时等待硬件移出数据
    while (SciaRegs.SCIFFTX.bit.TXFFST == 16U)
    {
    }

    SciaRegs.SCITXBUF = data & 0x00FFU;
}

/**
 * @brief  通过SCI-A发送以空字符结尾的字符串。
 */
void SCIA_SendString(const char *message)
{
    Uint16 index;

    if (message == 0)
    {
        return;
    }

    index = 0U;
    while (message[index] != '\0')
    {
        SCIA_SendByte((Uint16)message[index]);
        index++;
    }
}

/**
 * @brief   非阻塞读取一个SCI-A接收字节。
 * @return  1U表示读取成功，0U表示当前无数据或指针无效。
 */
Uint16 SCIA_TryReceiveByte(Uint16 *data)
{
    if ((data == 0) || (SciaRegs.SCIFFRX.bit.RXFFST == 0U))
    {
        return 0U;
    }

    // SCIRXBUF高位包含状态信息，只保留低8位数据
    *data = SciaRegs.SCIRXBUF.all & 0x00FFU;
    return 1U;
}

/**
 * @brief  SCI-A后台轮询任务，每次最多接收并回传一个字节。
 */
void SCIA_BackgroundTask(void)
{
    Uint16 receivedData;

    // 非阻塞检查接收FIFO，避免影响其他后台任务
    if (SCIA_TryReceiveByte(&receivedData) != 0U)
    {
        SCIA_SendByte(receivedData);
    }
}

/**
 * @brief  配置SCI-A使用的GPIO35和GPIO36。
 */
static void SCIA_InitGpio(void)
{
    EALLOW;

    // 使能内部上拉，GPIO36接收端使用异步采样
    GpioCtrlRegs.GPBPUD.bit.GPIO36 = 0;
    GpioCtrlRegs.GPBPUD.bit.GPIO35 = 0;
    GpioCtrlRegs.GPBQSEL1.bit.GPIO36 = 3;

    // GPIO36配置为SCIRXDA，GPIO35配置为SCITXDA
    GpioCtrlRegs.GPBMUX1.bit.GPIO36 = 1;
    GpioCtrlRegs.GPBMUX1.bit.GPIO35 = 1;

    EDIS;
}

#endif /* SCI_COMM_ENABLE */
