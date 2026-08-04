/*
 * sci.c
 *
 * SCI-A轮询通信驱动，使用GPIO35发送、GPIO36接收。
 */

#include "sci.h"
#include "led.h"
#include <stdint.h>

#if SCI_COMM_ENABLE

#define SCIA_DEBUG_BUFFER_SIZE     64U
#define SCIA_FIXED_MAX_VALUE       9999.999f
#define SCIA_FIXED_SCALE           1000.0f

static void SCIA_InitGpio(void);
static Uint16 SCIA_AppendUnsigned(char *buffer, Uint16 index, Uint32 value);
static Uint16 SCIA_AppendFixed3(char *buffer, Uint16 index, float value);

SCIA_Debug SCIA_Debug_1;

/**
 * @brief  初始化SCI-A，配置为8位数据、无校验、1位停止位。
 * @param  baud  通信波特率。
 */
void SCIA_Init(Uint32 baud)
{
    Uint32 baudRegister;
    Uint32 baudDivisor;

    // 检查波特率是否能够转换成有效的16位寄存器值
    if ((baud == 0UL) || (baud > (SCIA_LSPCLK_HZ / 8UL)))
    {
        return;
    }

    // 使用四舍五入计算BRR，减小高波特率下的误差
    baudDivisor = 8UL * baud;
    baudRegister = ((SCIA_LSPCLK_HZ + (baudDivisor / 2UL)) / baudDivisor) - 1UL;
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
 * @brief  初始化SCI波形调试对象。
 */
void SCIA_Debug_Init(SCIA_Debug *p_debug, Uint16 updateDiv)
{
    if (p_debug == 0)
    {
        return;
    }

    // 清零数据快照和运行状态
    p_debug->data_1 = 0.0f;
    p_debug->data_2 = 0.0f;
    p_debug->data_3 = 0.0f;
    p_debug->data_4 = 0.0f;
    p_debug->Request = 0U;
    p_debug->Prescaler = 0U;
    p_debug->DroppedFrames = 0U;

    // 分频系数为0时按每次调用都采集处理
    p_debug->UpdateDiv = (updateDiv == 0U) ? 1U : updateDiv;
}

/**
 * @brief  在ADC ISR中分频保存一帧调试数据。
 */
void SCIA_Debug_Capture(SCIA_Debug *p_debug,
                        float data_1,
                        float data_2,
                        float data_3,
                        float data_4)
{
    if (p_debug == 0)
    {
        return;
    }

    p_debug->Prescaler++;
    if (p_debug->Prescaler < p_debug->UpdateDiv)
    {
        return;
    }
    p_debug->Prescaler = 0U;

    // 上一帧未发送时保留原快照，避免后台读取过程中数据被覆盖
    if (p_debug->Request != 0U)
    {
        if (p_debug->DroppedFrames < 0xFFFFU)
        {
            p_debug->DroppedFrames++;
        }
        return;
    }

    p_debug->data_1 = data_1;
    p_debug->data_2 = data_2;
    p_debug->data_3 = data_3;
    p_debug->data_4 = data_4;

    // 所有变量更新完成后再置位请求标志
    p_debug->Request = 1U;
}

/**
 * @brief  在后台将数据快照转换为CSV字符串并发送。
 * @note   输出顺序固定为data_1,data_2,data_3,data_4，保留三位小数。
 */
void SCIA_Debug_Service(SCIA_Debug *p_debug)
{
    char buffer[SCIA_DEBUG_BUFFER_SIZE];
    Uint16 index;
    float data_1;
    float data_2;
    float data_3;
    float data_4;

    if ((p_debug == 0) || (p_debug->Request == 0U))
    {
        return;
    }

    // 请求保持置位期间ISR不会覆盖快照，可安全复制到局部变量
    data_1 = p_debug->data_1;
    data_2 = p_debug->data_2;
    data_3 = p_debug->data_3;
    data_4 = p_debug->data_4;

    index = 0U;
    index = SCIA_AppendFixed3(buffer, index, data_1);
    buffer[index++] = ',';
    index = SCIA_AppendFixed3(buffer, index, data_2);
    buffer[index++] = ',';
    index = SCIA_AppendFixed3(buffer, index, data_3);
    buffer[index++] = ',';
    index = SCIA_AppendFixed3(buffer, index, data_4);
    buffer[index++] = '\r';
    buffer[index++] = '\n';
    buffer[index] = '\0';

    SCIA_SendString(buffer);
    LED6_TOGGLE;

    // 完整发送后释放快照，允许ISR写入下一帧
    p_debug->Request = 0U;
}

/**
 * @brief  将无符号整数追加到字符串缓冲区。
 */
static Uint16 SCIA_AppendUnsigned(char *buffer, Uint16 index, Uint32 value)
{
    char digits[10];
    Uint16 count;

    count = 0U;
    do
    {
        digits[count] = (char)('0' + (value % 10UL));
        value /= 10UL;
        count++;
    }
    while (value != 0UL);

    // 临时数组中的数字顺序相反，需要倒序写入输出缓冲区
    while (count > 0U)
    {
        count--;
        buffer[index] = digits[count];
        index++;
    }

    return index;
}

/**
 * @brief  将浮点数转换为固定三位小数的字符串。
 */
static Uint16 SCIA_AppendFixed3(char *buffer, Uint16 index, float value)
{
    int32_t scaledValue;
    Uint32 magnitude;
    Uint32 integerPart;
    Uint16 fractionPart;

    // 限制转换范围，并将无效数值按0处理
    if (value != value)
    {
        value = 0.0f;
    }
    else if (value > SCIA_FIXED_MAX_VALUE)
    {
        value = SCIA_FIXED_MAX_VALUE;
    }
    else if (value < -SCIA_FIXED_MAX_VALUE)
    {
        value = -SCIA_FIXED_MAX_VALUE;
    }

    // 转换成千分之一单位，并对正负数分别进行四舍五入
    if (value >= 0.0f)
    {
        scaledValue = (int32_t)(value * SCIA_FIXED_SCALE + 0.5f);
    }
    else
    {
        scaledValue = (int32_t)(value * SCIA_FIXED_SCALE - 0.5f);
    }

    if (scaledValue < 0)
    {
        buffer[index++] = '-';
        magnitude = (Uint32)(-scaledValue);
    }
    else
    {
        magnitude = (Uint32)scaledValue;
    }

    integerPart = magnitude / 1000UL;
    fractionPart = (Uint16)(magnitude % 1000UL);

    index = SCIA_AppendUnsigned(buffer, index, integerPart);
    buffer[index++] = '.';
    buffer[index++] = (char)('0' + (fractionPart / 100U));
    buffer[index++] = (char)('0' + ((fractionPart / 10U) % 10U));
    buffer[index++] = (char)('0' + (fractionPart % 10U));

    return index;
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
