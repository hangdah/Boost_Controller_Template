# TMS320F28335 SCI-A 移植指南

本文用于指导 AI 将本示例的 SCI-A 轮询通信移植到另一个 TMS320F28335 工程。无需复制本工程的 `uart.c` 或 `uart.h`；应根据下面的代码块，将接口合并到目标工程现有的驱动目录中。

## 1. 原工程通信参数

| 项目 | 配置 |
| --- | --- |
| SCI 模块 | SCI-A |
| 发送引脚 | GPIO35 / SCITXDA |
| 接收引脚 | GPIO36 / SCIRXDA |
| 数据格式 | 8 data bits, no parity, 1 stop bit (8N1) |
| 收发方式 | FIFO + polling |
| SYSCLKOUT | 150 MHz |
| LOSPCP | 2，即 LSPCLK = SYSCLKOUT / 4 = 37.5 MHz |
| 示例波特率 | 4800 baud |

F28335 的 SCI 波特率寄存器计算公式为：

```text
BRR = LSPCLK / (8 * baud) - 1
```

如果目标工程修改了系统时钟或 `SysCtrlRegs.LOSPCP`，必须同步修改 `UARTA_LSPCLK_HZ`，否则上位机将收到乱码。

## 2. 移植前检查

目标工程必须已经具备 TI F28335 设备支持环境：

- 编译器能够找到 `DSP2833x_Device.h` 和 `DSP2833x_Examples.h`。
- `DSP2833x_GlobalVariableDefs.c` 已且仅已加入工程一次，以提供 `SciaRegs`、`GpioCtrlRegs` 和 `SysCtrlRegs` 等全局寄存器定义。
- 非 BIOS 工程已链接包含 `SciaRegsFile`、`GpioCtrlRegsFile` 和 `SysCtrlRegsFile` 映射的设备头文件 CMD 文件，例如 `DSP2833x_Headers_nonBIOS.cmd`。
- 系统启动时已经调用 `InitSysCtrl()`。不要为了移植 SCI 覆盖目标工程原有的 PLL、`LOSPCP` 或其他外设时钟配置。
- GPIO35、GPIO36 没有被目标工程的其他功能占用。

本指南把 GPIO 配置放入 SCI 驱动，因此不依赖原工程的 `DSP2833x_Sci.c` 或 `InitSciaGpio()`。如果目标工程已经配置这些引脚，应比较配置后只保留一处初始化。

## 3. 对外接口

将以下声明加入目标工程合适的驱动头文件，例如已有的 `uart.h`。保留接口名称可减少上层代码改动。

```c
#ifndef UART_H_
#define UART_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

/* Update this value when LSPCLK changes. */
#define UARTA_LSPCLK_HZ    37500000UL

void UARTa_Init(Uint32 baud);
void UARTa_SendByte(int a);
void UARTa_SendString(char *msg);

#endif /* UART_H_ */
```

## 4. GPIO 与 SCI-A 驱动代码

将下面的实现合并到目标工程的 UART/SCI 驱动源文件。该版本保持原示例的 8N1、FIFO 和轮询行为，但不会开启未使用的 SCI 中断。

```c
#include "uart.h"

static void UARTa_InitGpio(void)
{
    EALLOW;

    /* Enable internal pull-ups for SCI-A pins. */
    GpioCtrlRegs.GPBPUD.bit.GPIO36 = 0;
    GpioCtrlRegs.GPBPUD.bit.GPIO35 = 0;

    /* Use asynchronous qualification for the RX input. */
    GpioCtrlRegs.GPBQSEL1.bit.GPIO36 = 3;

    /* GPIO36 = SCIRXDA, GPIO35 = SCITXDA. */
    GpioCtrlRegs.GPBMUX1.bit.GPIO36 = 1;
    GpioCtrlRegs.GPBMUX1.bit.GPIO35 = 1;

    EDIS;
}

void UARTa_Init(Uint32 baud)
{
    Uint32 brr;

    /* baud must be non-zero and produce a 16-bit BRR value. */
    if ((baud == 0UL) || (baud > (UARTA_LSPCLK_HZ / 8UL)))
    {
        return;
    }

    brr = (UARTA_LSPCLK_HZ / (8UL * baud)) - 1UL;
    if (brr > 0xFFFFUL)
    {
        return;
    }

    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.SCIAENCLK = 1;
    EDIS;

    UARTa_InitGpio();

    /* Hold SCI in software reset while configuring it. */
    SciaRegs.SCICTL1.all = 0x0003;

    /* 1 stop bit, no parity, 8 data bits, asynchronous mode. */
    SciaRegs.SCICCR.all = 0x0007;

    /* Polling mode: do not enable TX or RX interrupts. */
    SciaRegs.SCICTL2.all = 0x0000;

    SciaRegs.SCIHBAUD = (Uint16)((brr >> 8) & 0x00FFUL);
    SciaRegs.SCILBAUD = (Uint16)(brr & 0x00FFUL);

    /* Enable and reset the transmit and receive FIFOs. */
    SciaRegs.SCIFFTX.all = 0xE040;
    SciaRegs.SCIFFRX.all = 0x204F;
    SciaRegs.SCIFFCT.all = 0x0000;

    /* Enable TX/RX and release SCI from software reset. */
    SciaRegs.SCICTL1.all = 0x0023;
}

void UARTa_SendByte(int a)
{
    /* Wait only when the 16-byte transmit FIFO is full. */
    while (SciaRegs.SCIFFTX.bit.TXFFST == 16U)
    {
    }

    SciaRegs.SCITXBUF = (Uint16)(a & 0x00FF);
}

void UARTa_SendString(char *msg)
{
    Uint16 i = 0;

    if (msg == 0)
    {
        return;
    }

    while (msg[i] != '\0')
    {
        UARTa_SendByte(msg[i]);
        i++;
    }
}
```

`UARTa_Init()` 的无效参数处理方式是直接返回，因为保留的原接口没有返回值。调用方应传入有效波特率，并确认算出的 BRR 不超过 16 位。

## 5. 主程序接入与轮询接收

必须先完成系统时钟初始化，再初始化 SCI。纯轮询模式不依赖 `InitPieCtrl()`、`InitPieVectTable()`、PIEIER 或 CPU `IER` 中的 SCI 中断配置。

```c
void main(void)
{
    Uint16 receivedChar;
    char *message;

    InitSysCtrl();
    UARTa_Init(4800UL);

    message = "SCI-A ready.\r\n";
    UARTa_SendString(message);

    while (1)
    {
        /* Wait until at least one byte is present in RX FIFO. */
        while (SciaRegs.SCIFFRX.bit.RXFFST == 0U)
        {
        }

        /* SCIRXBUF also contains status bits; keep the low 8 data bits. */
        receivedChar = SciaRegs.SCIRXBUF.all & 0x00FFU;
        UARTa_SendByte((int)receivedChar);
    }
}
```

不要使用原示例中的 `while (RXFFST != 1)`。如果 FIFO 在检查前已收到多个字节，`RXFFST` 可能大于 1，从而使程序一直等待；检查是否等于 0 才能表达“FIFO 为空”。

## 6. 硬件连接与上位机设置

- 上位机串口设置为 **4800 baud, 8 data bits, no parity, 1 stop bit, no flow control**。
- DSP TX 应连接接收端 RX，DSP RX 应连接发送端 TX，并确保两端共地。
- F28335 引脚是 3.3 V 逻辑。不要把裸芯片引脚直接连接到具有正负电压的传统 RS-232 接口；应核对开发板上是否已有 USB-UART 或 RS-232 电平转换电路。

## 7. 验证步骤

1. 编译目标工程，确认没有未定义的 `SciaRegs`、`GpioCtrlRegs` 或 `SysCtrlRegs`。
2. 下载程序，打开上位机串口，复位 DSP，确认收到 `SCI-A ready.`。
3. 分别发送单个字符和连续字符串，确认每个字节都被回传。
4. 若需要传输二进制数据，发送包含 `0x00` 和大于 `0x7F` 的字节，确认回传值保持低 8 位不变。

## 8. 常见故障定位

| 现象 | 优先检查 |
| --- | --- |
| 完全无输出 | `SCIAENCLK`、GPIO35 复用、TX/RX 交叉连接、公共地、串口电平 |
| 输出乱码 | `UARTA_LSPCLK_HZ`、`LOSPCP`、SYSCLKOUT、两端波特率和 8N1 设置 |
| 能发送但收不到 | GPIO36 复用及异步采样、上位机 TX 连接、`RXFFST` 状态 |
| 链接时报寄存器未定义 | `DSP2833x_GlobalVariableDefs.c` 和设备头文件 CMD 文件是否加入工程 |
| 初始化后其他外设异常 | GPIO35/36 或 `PCLKCR0` 是否与目标工程已有配置冲突 |

本指南只覆盖轮询 SCI-A 驱动。若目标工程需要中断、DMA、环形缓冲区或通信协议解析，应在基础收发验证通过后单独设计，不能仅通过打开 `TXINTENA`/`RXBKINTENA` 完成。
