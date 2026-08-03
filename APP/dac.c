#include "dac.h"

/* =========================================================
 * 私有宏定义
 * =========================================================
 * 说明：
 * TLV5620 需要一个 LOAD 锁存脉冲来把刚刚通过 SPI 送进去的数据
 * 真正更新到模拟输出端。
 *
 * 这里用 GPIO26 来控制这个 LOAD 信号：
 * 1. DAC_SET_LOAD()   -> 把 GPIO26 置高
 * 2. DAC_CLEAR_LOAD() -> 把 GPIO26 置低
 *
 * 之所以写成宏，是为了让后面代码更直观。
 * ========================================================= */
#define DAC_SET_LOAD()      (GpioDataRegs.GPASET.bit.GPIO26 = 1)
#define DAC_CLEAR_LOAD()    (GpioDataRegs.GPACLEAR.bit.GPIO26 = 1)
#define DAC_SPI_TIMEOUT_COUNT    0xFFFFU

/* =========================================================
 * 内部静态函数声明
 * =========================================================
 * 该函数只在本文件内部使用，不对外开放。
 * 作用：把实际变量值转换成 TLV5620 能识别的 0~255 DAC 码值。
 * ========================================================= */
static Uint16 DacDebug_ValueToCode(const _DacDebug *pDac, float value);
static void DAC_InitGpio(void);

_DacDebug DAC_Debug_1;


/* =========================================================
 * @brief  初始化 TLV5620 所需的 SPIA 与 LOAD 引脚
 *
 * @note
 * 1. 该函数由原 tlv5620.c 转移而来
 * 2. 配置 SPIA 为 11 位数据模式，用于匹配 TLV5620 控制字格式
 * 3. 本函数只做底层硬件初始化，不涉及调试结构体初始化
 * ========================================================= */
DAC_Status TLV5620_Init(void)
{
    /* -----------------------------
     * 1. 使能 SPIA 外设时钟
     * -----------------------------
     * F28335 的外设在使用前，需要先打开对应时钟。
     * 这里打开 SPI-A 模块时钟，否则后面配置 SPI 寄存器无效。
     */
    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.SPIAENCLK = 1;
    EDIS;

    /* 2. 初始化本模块实际使用的 SPIA GPIO，避免依赖外部示例函数。 */
    DAC_InitGpio();

    /* -----------------------------
     * 3. 单独配置 GPIO26 作为 TLV5620 的 LOAD 引脚
     * -----------------------------
     * TLV5620 除了 SPI 数据输入外，还需要一个 LOAD 锁存信号。
     * 这里把 GPIO26 当作普通输出脚，用来手动产生锁存脉冲。
     */
    EALLOW;

    /* GPIO26 配置为普通 GPIO，而不是复用外设功能 */
    GpioCtrlRegs.GPAMUX2.bit.GPIO26 = 0;

    /* GPIO26 配置为输出方向，因为我们要主动拉高/拉低它 */
    GpioCtrlRegs.GPADIR.bit.GPIO26  = 1;

    /* 使能 GPIO26 的上拉，增强引脚默认状态稳定性 */
    GpioCtrlRegs.GPAPUD.bit.GPIO26  = 0;

    EDIS;

    /* -----------------------------
     * 4. 配置 SPIA 工作模式
     * -----------------------------
     * TLV5620 的一帧控制字有效位数为 11 位：
     * channel(2位) + rng(1位) + dat(8位)
     *
     * 所以这里把 SPI 配成 11 位数据模式。
     */

    /*
     * SPICCR = 0x000A
     * 含义：
     * 1. 先让 SPI 保持在复位状态，便于安全配置
     * 2. 配置字符长度为 11 位
     */
    SpiaRegs.SPICCR.all = 0x0a;

    /*
     * SPICTL = 0x0006
     * 配置 SPI 工作方式：
     * 1. 主机模式
     * 2. 使能发送
     * 3. 正常时钟/相位配置
     *
     * 这些参数与原普中例程保持一致，确保和 TLV5620 匹配。
     */
    SpiaRegs.SPICTL.all = 0x0006;

    /*
     * 设置 SPI 波特率分频
     * 原例程注释说明该设置下 SPI 时钟大约为 0.75MHz。
     * 这个速度对于 DAC 调试输出已经足够。
     */
    SpiaRegs.SPIBRR = 0x0031;

    /*
     * SPICCR = 0x008A
     * 在前面配置完成后，退出 SPI 复位状态，开始正常工作。
     */
    SpiaRegs.SPICCR.all = 0x8a;

    /*
     * FREE = 1
     * 在仿真暂停时允许 SPI 自由运行，避免调试时外设状态异常。
     */
    SpiaRegs.SPIPRI.bit.FREE = 1;

    /* -----------------------------
     * 5. 初始化 LOAD 引脚为高电平
     * -----------------------------
     * 一般让 LOAD 先保持高电平，等待后面每次发送数据后
     * 再人为拉低-拉高，形成一个锁存脉冲。
     */
    DAC_SET_LOAD();

    return DAC_STATUS_OK;
}


/* =========================================================
 * @brief  配置 TLV5620 使用的 SPIA 引脚
 * @note   仅占用 GPIO54(SPISIMOA) 和 GPIO56(SPICLKA)。
 *         GPIO55/57 未被本模块使用，保持原工程配置。
 * ========================================================= */
static void DAC_InitGpio(void)
{
    EALLOW;

    GpioCtrlRegs.GPBPUD.bit.GPIO54 = 0;
    GpioCtrlRegs.GPBPUD.bit.GPIO56 = 0;
    GpioCtrlRegs.GPBQSEL2.bit.GPIO54 = 3;
    GpioCtrlRegs.GPBQSEL2.bit.GPIO56 = 3;
    GpioCtrlRegs.GPBMUX2.bit.GPIO54 = 1;
    GpioCtrlRegs.GPBMUX2.bit.GPIO56 = 1;

    EDIS;
}


/* =========================================================
 * @brief  向 TLV5620 指定通道发送 8 位 DAC 数据
 * @param  channel  通道号：0~3
 * @param  rng      量程位：0/1
 * @param  dat      8位数据：0~255
 *
 * @note
 * TLV5620 控制字有效位共 11 位：
 * channel(2bit) + rng(1bit) + dat(8bit)
 * 这里左移后写入 SPITXBUF，以适配 F28335 SPI 发送格式
 * ========================================================= */
DAC_Status DAC_SetChannelData(unsigned char channel, unsigned char rng, unsigned char dat)
{
    Uint16 dacValue = 0U;
    Uint16 timeoutCount;
    Uint16 receivedData;

    if ((channel > 3U) || (rng > 1U))
    {
        return DAC_STATUS_INVALID_CONFIG;
    }

    /* -----------------------------
     * 1. 组装 TLV5620 的控制字
     * -----------------------------
     * TLV5620 需要的有效控制字是 11 位：
     * [channel(2bit)] [rng(1bit)] [dat(8bit)]
     *
     * 这里把它们拼成一个 16 位变量 dacValue，
     * 但真正有效的是高 11 位。
     *
     * 为什么左移到高位？
     * 因为 F28335 的 SPI 发送寄存器 SPITXBUF 在这种配置下，
     * 发送的是高位对齐的数据，所以要把控制字左对齐。
     *
     * bit15~14 : channel
     * bit13    : rng
     * bit12~5  : dat
     * bit4~0   : 填 0，无效位
     */
    dacValue = ((Uint16)(channel << 14) |
                (Uint16)(rng     << 13) |
                (Uint16)(dat     << 5));

    /* -----------------------------
     * 2. 等待 SPI 发送缓冲区空出来
     * -----------------------------
     * BUFFULL_FLAG = 1 表示发送缓冲区满，暂时不能写新数据。
     * 所以这里轮询等待，直到可以写为止。
     *
     * 这是阻塞式写法，简单直接，适合调试输出，
     * 但不建议放到高速控制 ISR 里频繁调用。
     */
    timeoutCount = DAC_SPI_TIMEOUT_COUNT;
    while ((SpiaRegs.SPISTS.bit.BUFFULL_FLAG == 1) && (timeoutCount > 0U))
    {
        timeoutCount--;
    }

    if (timeoutCount == 0U)
    {
        return DAC_STATUS_SPI_TIMEOUT;
    }

    /* 读取接收缓冲区，清除上一次传输可能残留的 INT_FLAG。 */
    receivedData = SpiaRegs.SPIRXBUF;
    (void)receivedData;

    /* -----------------------------
     * 3. 把控制字写入 SPI 发送寄存器
     * -----------------------------
     * 一旦写入 SPITXBUF，SPI 硬件就会自动开始串行发送数据。
     */
    SpiaRegs.SPITXBUF = dacValue;

    /* -----------------------------
     * 4. 等待本次 SPI 完整字符发送完成
     * -----------------------------
     * INT_FLAG 在完整字符移出 SPIDAT 后置位。
     * 这样后面再去拉 LOAD，才能保证 TLV5620 锁存到的是新数据。
     */
    timeoutCount = DAC_SPI_TIMEOUT_COUNT;
    while ((SpiaRegs.SPISTS.bit.INT_FLAG == 0) && (timeoutCount > 0U))
    {
        timeoutCount--;
    }

    if (timeoutCount == 0U)
    {
        return DAC_STATUS_SPI_TIMEOUT;
    }

    /* INT_FLAG 表示完整字符已移出；读取 SPIRXBUF 清除该标志。 */
    receivedData = SpiaRegs.SPIRXBUF;
    (void)receivedData;

    /* -----------------------------
     * 5. 产生 LOAD 锁存脉冲
     * -----------------------------
     * SPI 只是把数据送进 TLV5620 的输入寄存器，
     * 真正更新模拟输出，还需要一个 LOAD 脉冲。
     *
     * 这里的顺序是：
     * 1. LOAD 拉低
     * 2. 延时一小段时间，满足器件时序要求
     * 3. LOAD 拉高
     * 4. 再延时一小段时间，保证锁存完成
     */

    /* 把 LOAD 拉低，表示准备锁存新数据 */
    DAC_CLEAR_LOAD();

    /* 保持低电平至少一小段时间，满足时序要求 */
    DELAY_US(2);

    /* 再把 LOAD 拉高，完成一次锁存脉冲 */
    DAC_SET_LOAD();

    /* 给 DAC 一点建立时间，确保输出稳定 */
    DELAY_US(10);

    return DAC_STATUS_OK;
}


/* =========================================================
 * @brief  初始化 DAC 调试对象
 * @param  pDac       DAC 调试对象指针
 * @param  minValue   变量显示下限
 * @param  maxValue   变量显示上限
 * @param  updateDiv  DAC 更新分频系数
 * @param  channel    TLV5620 通道号（0~3）
 * @param  range      TLV5620 量程位（0/1）
 *
 * @note
 * 本函数只初始化软件对象参数，不调用 TLV5620_Init()
 * TLV5620_Init() 建议在 main() 初始化阶段单独调用一次
 * ========================================================= */
DAC_Status DacDebug_Init(_DacDebug *pDac,
                         float minValue,
                         float maxValue,
                         Uint16 updateDiv,
                         Uint16 channel,
                         Uint16 range)
{
    DAC_Status status;

    /* -----------------------------
     * 1. 参数合法性检查
     * -----------------------------
     * 如果传进来的结构体指针为空，说明调用者没有提供有效对象，
     * 此时直接退出，避免后面对空指针解引用。
     */
    if (pDac == 0)
    {
        return DAC_STATUS_INVALID_POINTER;
    }

    /* -----------------------------
     * 2. 初始化运行时状态变量
     * -----------------------------
     * Code      : 当前待输出的 DAC 码值
     * Request   : 输出请求标志
     * Prescaler : 分频计数器
     *
     * 上电初始化时都清零。
     */
    pDac->Code      = 0U;
    pDac->Request   = 0U;
    pDac->Prescaler = 0U;
    pDac->Enabled    = 0U;
    pDac->LastStatus = (Uint16)DAC_STATUS_OK;
    pDac->ErrorCount = 0U;

    if ((updateDiv == 0U) || (channel > 3U) || (range > 1U))
    {
        pDac->LastStatus = (Uint16)DAC_STATUS_INVALID_CONFIG;
        return DAC_STATUS_INVALID_CONFIG;
    }

    /* -----------------------------
     * 3. 保存用户配置参数
     * -----------------------------
     * UpdateDiv : 控制“每隔多少次 ISR 请求一次 DAC 输出”
     * Channel   : 输出到 TLV5620 哪一路通道
     * Range     : TLV5620 的量程位
     */
    pDac->UpdateDiv = updateDiv;
    pDac->Channel   = channel;
    pDac->Range     = range;

    /* -----------------------------
     * 4. 根据 min/max 配置缩放系数
     * -----------------------------
     * 这里会调用 DacDebug_Config()，计算：
     * Gain   : 线性映射增益
     * Offset : 线性映射偏置
     *
     * 后面 ISR 中就只需要乘法和加法，不需要再做除法。
     */
    status = DacDebug_Config(pDac, minValue, maxValue);
    if (status != DAC_STATUS_OK)
    {
        return status;
    }

    pDac->Enabled = 1U;
    return DAC_STATUS_OK;
}


/* =========================================================
 * @brief  配置 DAC 调试对象的量程范围
 * @param  pDac      DAC 调试对象指针
 * @param  minValue  变量显示下限，对应 DAC 码值 0
 * @param  maxValue  变量显示上限，对应 DAC 码值 255
 *
 * @note
 * 除法只放在这里做一次，不放在 ISR 中
 * ========================================================= */
DAC_Status DacDebug_Config(_DacDebug *pDac, float minValue, float maxValue)
{
    /* 参数检查，防止空指针 */
    if (pDac == 0)
    {
        return DAC_STATUS_INVALID_POINTER;
    }

    if (maxValue <= minValue)
    {
        pDac->LastStatus = (Uint16)DAC_STATUS_INVALID_CONFIG;
        return DAC_STATUS_INVALID_CONFIG;
    }

    /* -----------------------------
     * 1. 保存量程上下限
     * -----------------------------
     * Min 和 Max 表示你想观察的变量范围。
     * 例如看 duty，可以设置为 0.0 ~ 0.95
     * 例如看 Vout，可以设置为 0 ~ 50
     */
    pDac->Min = minValue;
    pDac->Max = maxValue;

    /* -----------------------------
     * 2. 计算线性映射参数
     * -----------------------------
     * 目标是把：
     * value = minValue 映射到 DAC码 0
     * value = maxValue 映射到 DAC码 255
     *
     * 所以线性关系写成：
     * code = value * Gain + Offset
     *
     * 其中：
     * Gain   = 255 / (maxValue - minValue)
     * Offset = -minValue * Gain
     *
     * 这样后面 ISR 里不需要除法，只需乘加即可。
     */
    pDac->Gain   = 255.0f / (maxValue - minValue);
    pDac->Offset = -minValue * pDac->Gain;
    pDac->LastStatus = (Uint16)DAC_STATUS_OK;

    return DAC_STATUS_OK;
}


/* =========================================================
 * @brief  内部函数：把变量值映射成 DAC 码值（0~255）
 * @param  pDac   DAC 调试对象指针
 * @param  value  当前变量值
 * @return Uint16 DAC 码值（0~255）
 *
 * @note
 * 本函数只在本模块内部使用
 * ISR 中调用本函数时，不包含除法，仅包含比较、乘法、加法
 * ========================================================= */
static Uint16 DacDebug_ValueToCode(const _DacDebug *pDac, float value)
{
    float codeFloat;

    /* -----------------------------
     * 1. 下限饱和
     * -----------------------------
     * 如果当前变量值小于等于配置的最小值，
     * 则直接输出最小 DAC 码值 0。
     *
     * 这样做的作用：
     * 1. 避免越界
     * 2. 保证 DAC 输出始终在 0~255 之间
     */
    if (value <= pDac->Min)
    {
        return 0U;
    }

    /* -----------------------------
     * 2. 上限饱和
     * -----------------------------
     * 如果当前变量值大于等于配置的最大值，
     * 则直接输出最大 DAC 码值 255。
     */
    if (value >= pDac->Max)
    {
        return 255U;
    }

    /* -----------------------------
     * 3. 线性映射
     * -----------------------------
     * 对于处在 Min ~ Max 范围内的值，
     * 使用线性关系：
     *
     * code = value * Gain + Offset
     *
     * 再加 0.5f 是为了实现四舍五入，
     * 而不是简单截断。
     *
     * 例如计算结果是 127.6，
     * 加 0.5 后转 Uint16 会得到 128。
     */
    codeFloat = value * pDac->Gain + pDac->Offset + 0.5f;

    /* -----------------------------
     * 4. 转成整数 DAC 码值
     * -----------------------------
     * TLV5620 最终需要的是 8 位整数码，
     * 这里返回 Uint16 是为了和工程中的 Uint16 类型统一。
     */
    return (Uint16)codeFloat;
}


/* =========================================================
 * @brief  在 ISR 中调用：更新待输出 DAC 数据，并按分频节奏发起请求
 * @param  pDac   DAC 调试对象指针
 * @param  value  当前想观察的变量值
 *
 * @note
 * 本函数不直接调用 DAC_SetChannelData()
 * 本函数适合放在 ADC ISR / 控制 ISR 的末尾
 * ========================================================= */
void DacDebug_DataGet(_DacDebug *pDac, float value)
{
    /* 空指针保护 */
    if (pDac == 0)
    {
        return;
    }

    if (pDac->Enabled == 0U)
    {
        return;
    }

    /* -----------------------------
     * 1. 把当前变量值转换成 DAC 码值
     * -----------------------------
     * 这里会调用内部映射函数，把 float 变量映射到 0~255。
     * 得到的结果先存到结构体中的 Code 字段。
     *
     * 注意：
     * 这里只是“准备好数据”，还没有真正往 DAC 芯片发。
     */
    pDac->Code = DacDebug_ValueToCode(pDac, value);

    /* -----------------------------
     * 2. 分频计数器加 1
     * -----------------------------
     * 控制 ISR 通常频率很高，比如 20kHz、50kHz。
     * 如果每次 ISR 都去请求 DAC 输出，会让后台更新过于频繁。
     *
     * 所以这里用一个简单分频器：
     * 每调用一次本函数，Prescaler 加 1。
     */
    pDac->Prescaler++;

    /* -----------------------------
     * 3. 到达分频门限后，置发送请求标志
     * -----------------------------
     * 当 Prescaler 累计到 UpdateDiv 时，
     * 说明到了“允许输出一次 DAC”的时机。
     *
     * 此时做两件事：
     * 1. Prescaler 清零，开始下一轮计数
     * 2. Request 置 1，通知 while(1) 后台可以发一次 DAC
     */
    if (pDac->Prescaler >= pDac->UpdateDiv)
    {
        pDac->Prescaler = 0U;
        pDac->Request   = 1U;
    }
}


/* =========================================================
 * @brief  在 while(1) 中调用：执行 DAC 后台发送
 * @param  pDac  DAC 调试对象指针
 *
 * @note
 * 1. ISR 负责置 Request = 1
 * 2. while(1) 中调用本函数后，真正向 TLV5620 发送一次数据
 * 3. 先清 Request，再抓取快照，避免请求处理逻辑混乱
 * ========================================================= */
DAC_Status DacDebug_Service(_DacDebug *pDac)
{
    Uint16 codeSnapshot;
    DAC_Status status;

    /* 空指针保护 */
    if (pDac == 0)
    {
        return DAC_STATUS_INVALID_POINTER;
    }

    if (pDac->Enabled == 0U)
    {
        return DAC_STATUS_DISABLED;
    }

    /* -----------------------------
     * 1. 检查是否有新的 DAC 输出请求
     * -----------------------------
     * 只有当 ISR 把 Request 置 1 后，
     * 后台才真正执行一次 DAC 发送。
     */
    if (pDac->Request == 1U)
    {
        /* -----------------------------
         * 2. 先清除本次请求标志
         * -----------------------------
         * 这样表示“这一次请求我已经接手处理了”。
         *
         * 为什么先清标志？
         * 因为这样即使后面发送过程中 ISR 又来了，
         * ISR 也可以重新把 Request 置 1，表示下一次还有新数据待发。
         * 这样逻辑更清晰，不容易漏掉后续请求。
         */
        pDac->Request = 0U;

        /* -----------------------------
         * 3. 取一份当前 DAC 码值的快照
         * -----------------------------
         * 这里把 Code 复制到局部变量 codeSnapshot，
         * 后面发送时只使用这份快照。
         *
         * 这样做的好处是：
         * 即使发送过程中 ISR 又更新了 pDac->Code，
         * 也不会影响本次已经决定要发的数据。
         */
        codeSnapshot  = pDac->Code;

        /* -----------------------------
         * 4. 真正把数据发送给 TLV5620
         * -----------------------------
         * 这里会调用底层驱动函数 DAC_SetChannelData()，
         * 完成：
         * 1. 组装控制字
         * 2. SPI 发送
         * 3. LOAD 锁存
         *
         * 参数含义：
         * pDac->Channel      -> 输出到哪个 DAC 通道
         * pDac->Range        -> TLV5620 量程位
         * codeSnapshot       -> 本次要输出的 8位数据
         */
        status = DAC_SetChannelData((unsigned char)pDac->Channel,
                                    (unsigned char)pDac->Range,
                                    (unsigned char)codeSnapshot);

        pDac->LastStatus = (Uint16)status;
        if (status != DAC_STATUS_OK)
        {
            if (pDac->ErrorCount < 0xFFFFU)
            {
                pDac->ErrorCount++;
            }
            pDac->Enabled = 0U;
            return status;
        }
    }

    return DAC_STATUS_OK;
}
