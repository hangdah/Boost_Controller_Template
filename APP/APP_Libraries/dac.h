#ifndef DAC_H_
#define DAC_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

#ifndef DAC_DEBUG_ENABLE
#define DAC_DEBUG_ENABLE    1U
#endif

typedef enum
{
    DAC_STATUS_OK = 0,
    DAC_STATUS_INVALID_POINTER,
    DAC_STATUS_INVALID_CONFIG,
    DAC_STATUS_SPI_TIMEOUT,
    DAC_STATUS_DISABLED
} DAC_Status;

/* =========================================================
 * DAC 调试对象结构体
 * =========================================================
 * Min / Max      : 被观察变量的显示范围
 * Gain / Offset  : 把变量映射到 0~255 DAC 码值的缩放参数
 * Code           : 当前待输出的 DAC 码值
 * Request        : 发送请求标志，ISR 置位，while 中消费
 * Prescaler      : 分频计数器
 * UpdateDiv      : 更新分频系数
 * Channel        : TLV5620 通道号（0~3）
 * Range          : TLV5620 量程位（0/1）
 * ========================================================= */
typedef struct
{
    float  Min;
    float  Max;
    float  Gain;
    float  Offset;

    volatile Uint16 Code;
    volatile Uint16 Request;
    Uint16 Prescaler;

    Uint16 UpdateDiv;
    Uint16 Channel;
    Uint16 Range;

    volatile Uint16 Enabled;
    volatile Uint16 LastStatus;
    volatile Uint16 ErrorCount;

}_DacDebug;

extern _DacDebug DAC_Debug_1;


/* =========================================================
 * 底层 TLV5620 驱动函数
 * ========================================================= */

/**
 * @brief  初始化 TLV5620 所需的 SPIA 与 LOAD 引脚
 * @note   该函数原本位于 tlv5620.c，现在已整合到 DAC.c 中
 */
DAC_Status TLV5620_Init(void);

/**
 * @brief  向 TLV5620 指定通道发送 8 位 DAC 数据
 * @param  channel  通道号：0~3
 * @param  rng      量程位：0/1
 * @param  dat      8位数据：0~255
 * @note   该函数原本位于 tlv5620.c，现在已整合到 DAC.c 中
 */
DAC_Status DAC_SetChannelData(unsigned char channel, unsigned char rng, unsigned char dat);


/* =========================================================
 * DAC 调试封装函数
 * ========================================================= */

/**
 * @brief  初始化 DAC 调试对象
 * @param  pDac       DAC 调试对象指针
 * @param  minValue   变量显示下限
 * @param  maxValue   变量显示上限
 * @param  updateDiv  DAC 更新分频系数
 * @param  channel    TLV5620 通道号（0~3）
 * @param  range      TLV5620 量程位（0/1）
 */
DAC_Status DacDebug_Init(_DacDebug *pDac,
                         float minValue,
                         float maxValue,
                         Uint16 updateDiv,
                         Uint16 channel,
                         Uint16 range);

/**
 * @brief  配置 DAC 调试对象的量程范围
 * @param  pDac      DAC 调试对象指针
 * @param  minValue  变量显示下限
 * @param  maxValue  变量显示上限
 */
DAC_Status DacDebug_Config(_DacDebug *pDac, float minValue, float maxValue);

/**
 * @brief  在 ISR 中调用：获取当前变量并更新 DAC 调试数据
 * @param  pDac   DAC 调试对象指针
 * @param  value  当前想观察的变量值
 */
void DacDebug_DataGet(_DacDebug *pDac, float value);

/**
 * @brief  在 while(1) 中调用：执行一次 DAC 后台发送服务
 * @param  pDac  DAC 调试对象指针
 */
DAC_Status DacDebug_Service(_DacDebug *pDac);

#endif /* DAC_H_ */
