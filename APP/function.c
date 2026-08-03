/*
 * =================================================================================================
 * | 文件名        |   function.c
 * | 作者          |   da
 * | 创建于        |   2025年8月5日
 * | 修订          |   为模块化、基于句柄的架构进行了重构 (修正版)
 * |
 * | @简介
 * | 该文件实现了Boost变换器的核心控制逻辑。
 * | 它包含初始化程序、ADC采样处理、级联双闭环PI控制器、PWM占空比更新、用于系统操作的
 * | 综合状态机，以及基于软件的保护机制。整个模块被设计为自包含的，并围绕一个中央数据
 * | 结构（句柄）`BoostController` 进行操作，以促进代码的可重用性和封装性。
 * =================================================================================================
 */

#include "function.h"

// Rise状态下各控制模式的软启动参数
#define RISE_V_REF_START    0.0f    // 双环模式电压参考初值
#define RISE_V_TARGET       24.0f   // 双环模式电压参考目标值
#define RISE_V_REF_STEP     0.1f    // 每次状态机调用的电压参考步长
#define RISE_I_REF_START    0.6f    // 单环模式电流参考初值
#define RISE_I_TARGET       2.4f    // 单环模式电流参考目标值
#define RISE_I_REF_STEP     0.01f   // 每次状态机调用的电流参考步长
#define RISE_D_TARGET       0.3f    // 开环模式占空比目标值
#define RISE_D_STEP         0.02f   // 每次状态机调用的占空比步长


/*
 * =================================================================================================
 * 私有函数原型 (内部链接)
 * =================================================================================================
 * @简介
 * 这些函数被声明为 'static'，意味着它们仅在当前源文件(function.c)内部可见和可调用。
 * 这是封装的关键原则，它向其他模块隐藏了实现细节，并防止了意外的外部调用。
 */
static void StateMInit(BoostController *p);
static void StateMWait(BoostController *p);
static void StateMRise(BoostController *p);
static void StateMControlConfigError(BoostController *p);
static Uint16 StateMRiseRampToTarget(float *value, float target, float step);
static Uint16 StateMRiseEnter(BoostController *p);
static Uint16 StateMRiseUpdate(BoostController *p, Uint16 rampEnabled);
static void StateMRun(BoostController *p);
static void StateMErr(BoostController *p);
static void SwOCP(BoostController *p);
static void SwOVP(BoostController *p);
static void SwUVP(BoostController *p);
static void ValInit(BoostController *p);
static inline float SMC_sat(float x);
static inline float SMC_Clamp(float value, float minValue, float maxValue);
static float VoltageSMC_Compute(SMController *smc,
                                float Vref,
                                float Vout,
                                float Iout,
                                float Duty,
                                float Cout,
                                float Ts);


/*
 * =================================================================================================
 * 公共函数实现
 * =================================================================================================
 * @简介
 * 这些函数在 function.h 中声明，并作为本模块的公共应用程序编程接口 (API)。
 * 外部文件 (如 main.c, timer.c, adc.c) 通过这些函数与Boost变换器的逻辑进行交互。
 */

//=================================================================================================
// PI_Reset
//=================================================================================================
/**
 * @brief     复位 PI 控制器的内部状态（误差历史和积分累加器）。
 * @details   通常在模式切换、软启动或故障恢复前调用，确保控制器从确定的状态开始运行。
 * @param[inout] pi  指向 PI 控制器结构体的指针。
 */
void PI_Reset(PIController *pi)
{
    pi->Error1   = 0.0f;
    pi->Error2   = 0.0f;
    pi->Out      = 0.0f;
    pi->DeltaOut = 0.0f;
}

//=================================================================================================
// PI_ComputeIncremental_AntiWindup
//=================================================================================================
/**
 * @brief     执行增量式 PI 控制器的一个计算步骤，带抗积分饱和。
 * @details   通用 PI 算法，可用于电压环、电流环或任何其他需要 PI 控制的环节。
 *            1. 计算误差 = ref - fbk。
 *            2. 可选：对误差进行限幅（当 ErrMax/ErrMin 非零时生效）。
 *            3. 增量式计算：DeltaOut = Kp*(e(k)-e(k-1)) + Ki*Ts*e(k)。
 *            4. 对输出进行饱和处理（钳位到 [OutMin, OutMax]）。
 *            5. 抗积分饱和：仅当输出未饱和时更新积分累加器 Out。
 *            6. 保存当前误差供下次迭代使用。
 * @param[inout] pi   指向 PI 控制器结构体的指针（状态就地更新）。
 * @param[in]    ref  参考值 / 设定点。
 * @param[in]    fbk  反馈值 / 测量值。
 * @return       float  饱和后的 PI 控制器输出。
 */
#pragma CODE_SECTION(PI_ComputeIncremental_AntiWindup, ".TI.ramfunc")
float PI_ComputeIncremental_AntiWindup(PIController *pi, float ref, float fbk)
{
    float pi_out_temp;

    // 步骤1: 计算误差
    pi->Error1 = ref - fbk;

    // 步骤2: 误差限幅（当限幅值非零时生效）
    if (pi->ErrMax != 0.0f || pi->ErrMin != 0.0f)
    {
        if (pi->Error1 > pi->ErrMax)       pi->Error1 = pi->ErrMax;
        else if (pi->Error1 < pi->ErrMin)  pi->Error1 = pi->ErrMin;
    }

    // 步骤3: 增量式 PI 计算
    pi->DeltaOut = pi->Kp * (pi->Error1 - pi->Error2) + pi->Ki_Ts * pi->Error1;

    // 步骤4: 计算饱和前输出
    pi_out_temp = pi->Out + pi->DeltaOut;

    // 步骤5: 输出饱和处理
    float result;
    if (pi_out_temp > pi->OutMax)       result = pi->OutMax;
    else if (pi_out_temp < pi->OutMin)  result = pi->OutMin;
    else                                result = pi_out_temp;

    // 步骤6: 抗积分饱和 — 仅当未饱和时更新积分累加器
    if (pi_out_temp > pi->OutMax || pi_out_temp < pi->OutMin)
        pi->Out = result;
    else
        pi->Out = pi_out_temp;

    // 步骤7: 保存当前误差
    pi->Error2 = pi->Error1;

    return result;
}

//=================================================================================================
// PI_Init
//=================================================================================================
/**
 * @brief     初始化级联电压环和电流环的PI控制器参数。
 * @details   该函数配置外部电压环和内部电流环的比例(Kp)和积分(Ki)增益。它预先计算
 *            `Ki_Ts` 项 (Ki * 采样时间)，通过在控制中断服务程序中用一个预计算值替换
 *            乘法运算，来优化实时性能。它还为控制器设置了默认的参考值和操作限幅（最大/最小占空比）。
 * @param[inout] p  指向主Boost控制器结构体的指针。其 `ctr` 成员将被此函数填充。
 */
void PI_Init(BoostController *p)
{
    // 电流内环参数
//    p->ctr.I_Loop.Ki       = 2488.5f;
//    p->ctr.I_Loop.Ki_Ts    = p->ctr.I_Loop.Ki * p->pwm.Ts;
//    p->ctr.I_Loop.Kp       = 0.0566f;
    p->ctr.I_Loop.Ki       = 3155105.4373f / p->pwm.TBPRD;
    p->ctr.I_Loop.Ki_Ts    = p->ctr.I_Loop.Ki * p->pwm.Ts;
    p->ctr.I_Loop.Kp       = 832.1417f / p->pwm.TBPRD;

    // 电压外环参数
//    p->ctr.V_Loop.Ki       = 462.57f;
//    p->ctr.V_Loop.Ki_Ts    = p->ctr.V_Loop.Ki * p->pwm.Ts;
//    p->ctr.V_Loop.Kp       = 0.0736f;
    p->ctr.V_Loop.Ki       = 113.5965f;
    p->ctr.V_Loop.Ki_Ts    = p->ctr.V_Loop.Ki * p->pwm.Ts;
    p->ctr.V_Loop.Kp       = 0.4911f;

    // 电压外环限幅
    p->ctr.V_Loop.OutMax   = 9.0f;   // I_Ref 最大 9A
    p->ctr.V_Loop.OutMin   = 0.0f;
    p->ctr.V_Loop.ErrMax   = 5.0f;   // 电压误差钳位 ±5V
    p->ctr.V_Loop.ErrMin   = -5.0f;

    // 电流内环限幅
    p->ctr.I_Loop.OutMax   = 0.7f;   // 最大占空比
    p->ctr.I_Loop.OutMin   = 0.1f;   // 最小占空比
    p->ctr.I_Loop.ErrMax   = 0.0f;   // 不限幅 (ErrMax=0 && ErrMin=0)
    p->ctr.I_Loop.ErrMin   = 0.0f;
}

//=================================================================================================
// Control_Init
//=================================================================================================
/**
 * @brief     控制器集中初始化。
 * @details   先设置控制模式、参考值与限幅，再初始化PWM参数、PI和SMC。
 *            该顺序保证PI使用有效的TBPRD/Ts，SMC使用有效的占空比限幅。
 *            所有控制相关的配置集中在此函数中，main.c 只需调用此函数。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
void Control_Init(BoostController *p)
{
    // 电流内环模式: INNER_LOOP_PI 或 INNER_LOOP_SMC
    p->flag.InnerLoopMode = INNER_LOOP_PI;
    // 电压外环模式: OUTER_LOOP_PI 或 OUTER_LOOP_SMC
    p->flag.OuterLoopMode = OUTER_LOOP_PI;
    // 控制模式: CTRL_MODE_DUAL_LOOP / CTRL_MODE_SINGLE_LOOP / CTRL_MODE_OPEN_LOOP
    p->flag.CtrlMode      = CTRL_MODE_DUAL_LOOP;
    // 软启动开关: SOFTSTART_ENABLE 或 SOFTSTART_DISABLE
    p->flag.SoftStartEn   = SOFTSTART_ENABLE;

    // ADC 标定系数: 物理量 = Gain * ADC电压 + Offset
    p->adc.IL_Gain     = -3.6458f;
    p->adc.IL_Offset   = 6.1978f;
    p->adc.Vout_Gain   = 16.311f;
    p->adc.Vout_Offset = -0.7392f;
    p->adc.Vin_Gain    = 0.0f;     // 预留
    p->adc.Vin_Offset  = 0.0f;
    p->adc.Iout_Gain   = 0.0f;     // 预留
    p->adc.Iout_Offset = 0.0f;

    // 初始控制值和限幅
    p->ctr.V_Ref    = 0.0f;
    p->ctr.I_Ref    = 0.0f;
    p->ctr.u        = 0.0f;
    p->ctr.MAX_Duty = 0.7f;
    p->ctr.MIN_Duty = 0.1f;

    // 控制器参数初始化，调用顺序不可随意调整
    PWM_Values_Init(p);
    PI_Init(p);
    SMC_Init(p);
}

//=================================================================================================
// SMC_sat
//=================================================================================================
/**
 * @brief     SMC饱和函数，用于消除抖振。
 * @details   在边界层内部线性插值，在边界层外部硬切换。
 *            当 |x| < 1 时返回 x (线性区域)，
 *            否则返回 sign(x) (饱和区域)。
 * @param[in] x  归一化后的滑模面 (S / Phi)
 * @return    float  饱和函数输出, 范围 [-1, 1]
 */
static inline float SMC_sat(float x)
{
    if (x > 1.0f)       return 1.0f;
    else if (x < -1.0f) return -1.0f;
    else                return x;
}

static inline float SMC_Clamp(float value, float minValue, float maxValue)
{
    if (value > maxValue)       return maxValue;
    else if (value < minValue)  return minValue;
    else                        return value;
}

//=================================================================================================
// SMC_Init
//=================================================================================================
/**
 * @brief     初始化电流内环和电压外环SMC参数。
 * @details   两个控制器使用独立状态。电压SMC参数仅为低功率初始调试值，
 *            必须根据实际Cout、负载和允许电流重新标定。
 *            K值越大响应越快但抖振越严重；
 *            Phi值越大抖振越小但稳态误差越大。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
void SMC_Init(BoostController *p)
{
    // 电流内环SMC：输出Duty，保持现有参数
    p->ctr.I_SMC_Loop.K       = 0.05f;           // 切换增益: ±5%占空比调节能力
    p->ctr.I_SMC_Loop.Phi     = 0.5f;            // 边界层: ±0.5A电流误差内线性控制
    p->ctr.I_SMC_Loop.Lambda  = 0.0f;
    p->ctr.I_SMC_Loop.Integral = 0.0f;
    p->ctr.I_SMC_Loop.IntegralMax = 0.0f;
    p->ctr.I_SMC_Loop.IntegralMin = 0.0f;
    p->ctr.I_SMC_Loop.D_eq    = 0.0f;            // 等效控制，运行时计算
    p->ctr.I_SMC_Loop.U_sw    = 0.0f;
    p->ctr.I_SMC_Loop.RawOut  = p->ctr.MIN_Duty;
    p->ctr.I_SMC_Loop.S       = 0.0f;            // 滑模面
    p->ctr.I_SMC_Loop.Out     = p->ctr.MIN_Duty; // 起始最小占空比
    p->ctr.I_SMC_Loop.OutMax  = p->ctr.MAX_Duty; // 与PI内环一致
    p->ctr.I_SMC_Loop.OutMin  = p->ctr.MIN_Duty; // 与PI内环一致

    // 电压外环SMC：输出I_Ref，以下参数仅供低功率初调
    p->ctr.V_SMC_Loop.K       = 100.0f;         // 初始趋近增益，需重新标定
    p->ctr.V_SMC_Loop.Phi     = 1.0f;           // 电压边界层，单位V
    p->ctr.V_SMC_Loop.Lambda  = 50.0f;          // 积分滑模面系数，单位1/s
    p->ctr.V_SMC_Loop.Integral = 0.0f;
    p->ctr.V_SMC_Loop.IntegralMax = 0.1f;       // 电压误差积分上限，单位V*s
    p->ctr.V_SMC_Loop.IntegralMin = -0.1f;
    p->ctr.V_SMC_Loop.D_eq    = 0.0f;           // 等效电感电流参考
    p->ctr.V_SMC_Loop.U_sw    = 0.0f;
    p->ctr.V_SMC_Loop.RawOut  = 0.0f;
    p->ctr.V_SMC_Loop.S       = 0.0f;
    p->ctr.V_SMC_Loop.Out     = 0.0f;
    p->ctr.V_SMC_Loop.OutMax  = 2.4f;           // 初始电流参考上限，单位A
    p->ctr.V_SMC_Loop.OutMin  = 0.0f;

    p->ctr.VoltageLoopCounter = 0U;
    p->ctr.VoltageLoopDivider = 20U;            // 40kHz控制调用分频到约2kHz
    p->ctr.VoltageLoopTs = p->pwm.Ts * 0.5f * (float)p->ctr.VoltageLoopDivider;
    p->ctr.Cout = 1.0e-3f;                      // 初始占位值1mF，需按实际硬件修改
}

//=================================================================================================
// SMC_Reset
//=================================================================================================
/**
 * @brief     复位SMC控制器的内部状态。
 * @details   用于软启动入口或模式切换时，确保控制器从确定状态开始。
 *            等效控制D_eq清零，滑模面S清零，输出复位到最小占空比。
 * @param[inout] smc  指向SMC控制器结构体的指针。
 */
void SMC_Reset(SMController *smc)
{
    smc->Integral = 0.0f;
    smc->D_eq     = 0.0f;
    smc->U_sw     = 0.0f;
    smc->RawOut   = smc->OutMin;
    smc->S        = 0.0f;
    smc->Out      = smc->OutMin;
}

//=================================================================================================
// SMC_Compute
//=================================================================================================
/**
 * @brief     执行SMC电流内环的一个计算步骤。
 * @details   滑模面 S = IL_ref - IL (电流误差)。
 *            等效控制 D_eq = 1 - Vin/Vout (Boost变换器稳态占空比)。
 *            总控制律 D = D_eq + K * sat(S/Phi)，输出钳位到 [OutMin, OutMax]。
 *
 *            sat(x) = x (|x|<=1), sign(x) (|x|>1)。
 *            边界层Phi使控制器在滑模面附近连续调节，消除抖振。
 *
 * @param[inout] smc    指向SMC控制器结构体的指针 (状态就地更新)。
 * @param[in]    IL_ref 电流参考值 (来自电压外环PI)。
 * @param[in]    IL     电感电流测量值。
 * @param[in]    Vin    输入电压测量值 (用于等效控制计算)。
 * @param[in]    Vout   输出电压测量值 (用于等效控制计算)。
 * @return       float  饱和后的SMC控制器输出 (占空比)。
 */
#pragma CODE_SECTION(SMC_Compute, ".TI.ramfunc")
float SMC_Compute(SMController *smc, float IL_ref, float IL, float Vin, float Vout)
{
    float phi;
    float result;

    // 步骤1: 计算滑模面 (电流误差)
    smc->S = IL_ref - IL;

    // 步骤2: 计算等效控制 D_eq = 1 - Vin/Vout (Boost变换器稳态关系)
    // Vin/Vout无效时令D_eq=0，避免除零或错误的等效占空比
    if ((Vout > 0.5f) && (Vin > 0.0f) && (Vin < Vout))
    {
        smc->D_eq = 1.0f - Vin / Vout;
    }
    else
    {
        smc->D_eq = 0.0f;
    }

    // 步骤3: 合成总控制输出: D = D_eq + K * sat(S/Phi)
    phi = (smc->Phi > 1.0e-6f) ? smc->Phi : 1.0e-6f;
    smc->U_sw = smc->K * SMC_sat(smc->S / phi);
    smc->RawOut = smc->D_eq + smc->U_sw;

    // 步骤4: 输出限幅
    result = SMC_Clamp(smc->RawOut, smc->OutMin, smc->OutMax);

    smc->Out = result;
    return result;
}

//=================================================================================================
// VoltageSMC_Compute (私有)
//=================================================================================================
/**
 * @brief     计算Boost电压外环SMC，输出电感电流参考I_Ref。
 * @details   使用上一控制周期Duty和p->adc.Iout。当前Iout尚未接入实际采样，
 *            因此该控制器默认不启用，参数仅用于低功率初步验证。
 */
#pragma CODE_SECTION(VoltageSMC_Compute, ".TI.ramfunc")
static float VoltageSMC_Compute(SMController *smc,
                                float Vref,
                                float Vout,
                                float Iout,
                                float Duty,
                                float Cout,
                                float Ts)
{
    float error;
    float oldIntegral;
    float oneMinusDuty;
    float phi;
    float controlGain;

    if ((Cout <= 0.0f) || (Ts <= 0.0f))
    {
        return smc->Out;
    }

    error = Vref - Vout;
    oldIntegral = smc->Integral;
    smc->Integral += error * Ts;
    smc->Integral = SMC_Clamp(smc->Integral,
                              smc->IntegralMin,
                              smc->IntegralMax);

    smc->S = error + smc->Lambda * smc->Integral;

    oneMinusDuty = 1.0f - Duty;
    oneMinusDuty = SMC_Clamp(oneMinusDuty, 0.05f, 1.0f);

    smc->D_eq = (Iout + Cout * smc->Lambda * error) / oneMinusDuty;
    controlGain = Cout / oneMinusDuty;
    phi = (smc->Phi > 1.0e-6f) ? smc->Phi : 1.0e-6f;
    smc->U_sw = controlGain * smc->K * SMC_sat(smc->S / phi);
    smc->RawOut = smc->D_eq + smc->U_sw;
    smc->Out = SMC_Clamp(smc->RawOut, smc->OutMin, smc->OutMax);

    // 输出饱和且误差仍推动积分向饱和方向发展时，撤销本次积分
    if (((smc->RawOut > smc->OutMax) && (error > 0.0f)) ||
        ((smc->RawOut < smc->OutMin) && (error < 0.0f)))
    {
        smc->Integral = oldIntegral;
        smc->S = error + smc->Lambda * smc->Integral;
        smc->U_sw = controlGain * smc->K * SMC_sat(smc->S / phi);
        smc->RawOut = smc->D_eq + smc->U_sw;
        smc->Out = SMC_Clamp(smc->RawOut, smc->OutMin, smc->OutMax);
    }

    return smc->Out;
}

//=================================================================================================
// PWM_Values_Init
//=================================================================================================
/**
 * @brief     初始化PWM相关的参数，并配置硬件死区。
 * @details   根据CPU频率和期望的PWM频率，计算ePWM时基周期寄存器(TBPRD)的值。
 *            此函数还直接配置ePWM1模块的硬件死区寄存器。
 * @param[inout] p  指向主Boost控制器结构体的指针。其 `pwm` 成员将被此函数填充。
 */
void PWM_Values_Init(BoostController *p)
{
    p->pwm.CPU_Freq = 150000000.0f;
    p->pwm.PWM_Freq = 20000.0f;
    p->pwm.Ts = 1.0f / p->pwm.PWM_Freq;
    p->pwm.Duty = p->ctr.MIN_Duty; // 以最小占空比启动
    // 为增减计数模式计算TBPRD: F_cpu / (2 * F_pwm)
    p->pwm.TBPRD = p->pwm.CPU_Freq/(2.0f * p->pwm.PWM_Freq);
    p->pwm.AComp = p->pwm.Duty * p->pwm.TBPRD;
    p->pwm.BComp = p->pwm.Duty * p->pwm.TBPRD;
    p->pwm.DeadTime = 1; // 死区时间，单位为SYSCLKOUT周期
    EALLOW;
    EPwm1Regs.DBRED = (Uint16)(p->pwm.DeadTime * 150.0f);
    EPwm1Regs.DBFED = (Uint16)(p->pwm.DeadTime * 150.0f);
    EDIS;
}

//=================================================================================================
// Get_TBPRD
//=================================================================================================
/**
 * @brief     访问器(“Getter”)函数，用于安全地获取计算出的TBPRD值。
 * @details   该函数为模块的内部参数提供受控的、只读的访问，这是封装的良好实践。
 *            它允许其他模块（如ePWM初始化模块）获取必要的配置值，而无需直接访问内部数据结构。
 * @param[in] p  指向主Boost控制器结构体的指针。
 * @return    (float) 计算出的ePWM1时基周期寄存器(TBPRD)的值。
 */
float Get_TBPRD(BoostController *p)
{
    return p->pwm.TBPRD;
}

//=================================================================================================
// Process_Valley_Samples
//=================================================================================================
/**
 * @brief     处理在PWM周期波谷(TBCTR=0)时刻采样的ADC结果。
 * @details   该函数应在ADC SEQ1中断中调用。它读取原始的12位ADC转换结果，应用一阶IIR
 *            低通滤波器以减少噪声，将滤波后的值转换为物理单位（伏特、安培），并更新
 *            控制器句柄中相应的变量。
 * @param[inout] p  指向主Boost控制器结构体的指针。其 `adc` 成员将被更新。
 * @note      `static` 静态滤波器状态变量确保了每次调用之间的滤波连续性。
 */
#pragma CODE_SECTION(Process_Valley_Samples, ".TI.ramfunc")
void Process_Valley_Samples(BoostController *p)
{
    // 定义静态IIR滤波器状态变量
    static float TempIL_ = 0.0f;
    static float TempVo_  = 0.0f;
    const float ADC_Transform = 3.0f/4095.0f;
    // IL -> ADB0
    // Vo -> ADA0

    // 读取所有ADC结果
    float Raw_Result_0 = (float)(AdcRegs.ADCRESULT0 >> 4);
    float Raw_Result_1 = (float)(AdcRegs.ADCRESULT1 >> 4);
    float Raw_Result_2 = (float)(AdcRegs.ADCRESULT2 >> 4);
    float Raw_Result_3 = (float)(AdcRegs.ADCRESULT3 >> 4);

    // 滤波和单位转换后存入全局变量
    TempIL_ = (Raw_Result_0 * ADC_Transform) * IIR_ALPHA + (1.0f - IIR_ALPHA) * TempIL_;
    p->adc.IL = p->adc.IL_Gain * TempIL_ + p->adc.IL_Offset;

    TempVo_ = (Raw_Result_1 * ADC_Transform) * IIR_ALPHA + (1.0f - IIR_ALPHA) * TempVo_;
    p->adc.Vout = p->adc.Vout_Gain * TempVo_ + p->adc.Vout_Offset;

    p->adc.Vin = 0.0f;
    p->adc.Iout = 0.0f;
    LED2_TOGGLE;
}

//=================================================================================================
// Process_Peak_Samples
//=================================================================================================
/**
 * @brief     处理在PWM周期波峰(TBCTR=PRD)时刻采样的ADC结果。
 * @details   该函数应在ADC SEQ2中断中调用。与 Process_Valley_Samples 处理逻辑相同。
 * @param[inout] p  指向主Boost控制器结构体的指针。其 `adc` 成员将被更新。
 */
#pragma CODE_SECTION(Process_Peak_Samples, ".TI.ramfunc")
void Process_Peak_Samples(BoostController *p)
{
    // 定义静态IIR滤波器状态变量
    static float TempIL_ = 0.0f;
    static float TempVo_  = 0.0f;
    const float ADC_Transform = 3.0f/4095.0f;
    // IL -> ADB0
    // Vo -> ADA0

    // 读取所有ADC结果
    float Raw_Result_4 = (float)(AdcRegs.ADCRESULT4 >> 4);
    float Raw_Result_5 = (float)(AdcRegs.ADCRESULT5 >> 4);
    float Raw_Result_6 = (float)(AdcRegs.ADCRESULT6 >> 4);
    float Raw_Result_7 = (float)(AdcRegs.ADCRESULT7 >> 4);

    // 滤波和单位转换后存入全局变量
    TempIL_ = (Raw_Result_4 * ADC_Transform) * IIR_ALPHA + (1.0f - IIR_ALPHA) * TempIL_;
    p->adc.IL = p->adc.IL_Gain * TempIL_ + p->adc.IL_Offset;

    TempVo_ = (Raw_Result_5 * ADC_Transform) * IIR_ALPHA + (1.0f - IIR_ALPHA) * TempVo_;
    p->adc.Vout = p->adc.Vout_Gain * TempVo_ + p->adc.Vout_Offset;

    p->adc.Vin = 0.0f;
    p->adc.Iout = 0.0f;
    LED2_TOGGLE;
}

//=================================================================================================
// Adc_Sample_Process
//=================================================================================================
/**
 * @brief     ADC采样处理的通用包装函数。
 * @details   为其他模块（如定时器中断）提供一个统一的调用接口，使其无需关心
 *            具体是哪个采样实例（峰值或谷值）是主控制循环使用的。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(Adc_Sample_Process, ".TI.ramfunc")
void Adc_Sample_Process(BoostController *p)
{
    // 此函数可以作为一个包装器或别名。
    // 假设波谷采样是快速控制环路的主采样点。
    Process_Valley_Samples(p);
}

//=================================================================================================
// Control_Loop_Compute
//=================================================================================================
/**
 * @brief     控制环路计算。
 * @details   根据 CtrlMode、OuterLoopMode 和 InnerLoopMode 选择计算路径:
 *            - 开环: 无计算 (u 由 StateMRise 设定)
 *            - 单环: 电流内环 PI 或 SMC (I_Ref 由 StateMRise 设定)
 *            - 双环: 电压外环 PI/SMC → I_Ref → 电流内环 PI/SMC
 *            在 ADC ISR 中采样后调用，与 PWM_Update 配合使用。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(Control_Loop_Compute, ".TI.ramfunc")
void Control_Loop_Compute(BoostController *p)
{
    switch (p->flag.CtrlMode)
    {
        case CTRL_MODE_OPEN_LOOP:
            p->ctr.VoltageLoopCounter = 0U;
            break;

        case CTRL_MODE_SINGLE_LOOP:
            p->ctr.VoltageLoopCounter = 0U;
            if (p->flag.InnerLoopMode == INNER_LOOP_PI)
                p->ctr.u = PI_ComputeIncremental_AntiWindup(&p->ctr.I_Loop, p->ctr.I_Ref, p->adc.IL);
            else
                p->ctr.u = SMC_Compute(&p->ctr.I_SMC_Loop, p->ctr.I_Ref, p->adc.IL, p->adc.Vin, p->adc.Vout);
            break;

        case CTRL_MODE_DUAL_LOOP:
        default:
            if (p->flag.OuterLoopMode == OUTER_LOOP_PI)
            {
                p->ctr.VoltageLoopCounter = 0U;
                p->ctr.I_Ref = PI_ComputeIncremental_AntiWindup(&p->ctr.V_Loop, p->ctr.V_Ref, p->adc.Vout);
            }
            else
            {
                if (p->ctr.VoltageLoopDivider == 0U)
                {
                    p->ctr.VoltageLoopDivider = 1U;
                }

                p->ctr.VoltageLoopCounter++;
                if (p->ctr.VoltageLoopCounter >= p->ctr.VoltageLoopDivider)
                {
                    p->ctr.VoltageLoopCounter = 0U;
                    p->ctr.VoltageLoopTs = p->pwm.Ts * 0.5f * (float)p->ctr.VoltageLoopDivider;
                    p->ctr.I_Ref = VoltageSMC_Compute(&p->ctr.V_SMC_Loop,
                                                      p->ctr.V_Ref,
                                                      p->adc.Vout,
                                                      p->adc.Iout,
                                                      p->pwm.Duty,
                                                      p->ctr.Cout,
                                                      p->ctr.VoltageLoopTs);
                }
            }

            if (p->flag.InnerLoopMode == INNER_LOOP_PI)
                p->ctr.u = PI_ComputeIncremental_AntiWindup(&p->ctr.I_Loop, p->ctr.I_Ref, p->adc.IL);
            else
                p->ctr.u = SMC_Compute(&p->ctr.I_SMC_Loop, p->ctr.I_Ref, p->adc.IL, p->adc.Vin, p->adc.Vout);
            break;
    }
}

//=================================================================================================
// PWM_Update
//=================================================================================================
/**
 * @brief     根据PI控制器的输出Vc更新PWM的比较值（即占空比）。
 * @details   这是控制环路的最终执行步骤，它将控制变量`u`转换为硬件特定的比较值，
 *            以设定PWM的占空比。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(PWM_Update, ".TI.ramfunc")
void PWM_Update(BoostController *p)
{
    Uint16 CompareValue = 0;
    p->pwm.Duty = p->ctr.u;

    // 为Boost变换器的高边有效配置计算比较值
    CompareValue = (Uint16)(p->pwm.TBPRD * p->pwm.Duty);


    // 直接写入硬件寄存器
    EALLOW;
    EPwm1Regs.CMPA.half.CMPA = CompareValue;
    EDIS;
}

//=================================================================================================
// PWM_Disable
//=================================================================================================
/**
 * @brief     使用跳闸区(Trip-Zone)模块快速、可靠地禁用PWM输出。
 * @details   此函数强制一个单次跳闸事件，使ePWM输出进入预设的安全状态（通常是低电平）。
 *            这是在发生故障时关闭功率级最快、最可靠的方法。
 */
#pragma CODE_SECTION(PWM_Disable, ".TI.ramfunc")
void PWM_Disable(void) { EALLOW; EPwm1Regs.TZFRC.bit.OST = 1; EDIS; }

//=================================================================================================
// PWM_Enable
//=================================================================================================
/**
 * @brief     在跳闸事件后重新使能PWM输出。
 * @details   清除跳闸区模块中的单次跳闸标志，允许PWM恢复正常运行。
 */
#pragma CODE_SECTION(PWM_Enable, ".TI.ramfunc")
void PWM_Enable(void) { EALLOW; EPwm1Regs.TZCLR.bit.OST = 1; EDIS; }

//=================================================================================================
// ShortOff
//=================================================================================================
/**
 * @brief     检测并处理输出短路情况。
 * @details   这是一个软件保护程序。如果输出电流超过一个高阈值，同时输出电压低于一个
 *            低阈值，则判定为短路故障，禁用PWM，并将状态机转换到错误状态。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(ShortOff, ".TI.ramfunc")
void ShortOff(BoostController *p)
{
    #define I_OUT_SHORT_MAX 15.0f
    #define V_OUT_SHORT_MIN 7.5f
    if((p->adc.Iout > I_OUT_SHORT_MAX) && (p->adc.Vout < V_OUT_SHORT_MIN)) {
        PWM_Disable();
        p->flag.Error = Error_OUT_SHORT;
        p->flag.StateM  = Err;
    }
}

//=================================================================================================
// State_M
//=================================================================================================
/**
 * @brief     主状态机调度器。
 * @details   该函数充当一个分发器。根据 `p->flag.StateM` 的当前值，
 *            它会调用当前状态（Init, Wait, Rise, Run, Err）对应的处理函数。
 *            它应该从一个慢速定时器中断（例如Timer1）中被周期性地调用。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(State_M, ".TI.ramfunc")
void State_M(BoostController *p)
{
    if (p->flag.StateM != Rise)
    {
        p->flag.RiseInitialized = 0U;
    }

    switch(p->flag.StateM) {
        case Init: StateMInit(p); break;
        case Wait: StateMWait(p); break;
        case Rise: StateMRise(p); break;
        case Run:  StateMRun(p);  break;
        case Err:  StateMErr(p);  break;

        default:
            p->flag.StateM = Init; // 或者转到 Err
            break;
    }
}

//=================================================================================================
// SlowP
//=================================================================================================
/**
 * @brief     慢速软件保护任务的调度器。
 * @details   此函数调用所有基于时间的软件保护检测函数（过流、过压、欠压）。
 *            它被设计为以比主控制环路低的频率运行，通常与状态机一起在慢速定时器中断中调用。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(SlowP, ".TI.ramfunc")
void SlowP(BoostController *p)
{
    SwOCP(p);
    SwOVP(p);
    SwUVP(p);
}


/*
 * =================================================================================================
 * 私有函数实现
 * =================================================================================================
 * @简介
 * 以下是状态机和保护逻辑的内部辅助函数。它们不应被此文件之外的任何模块调用。
 */

//=================================================================================================
// ValInit (私有)
//=================================================================================================
/**
 * @brief     初始化/复位系统值，主要是清除故障标志。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(ValInit, ".TI.ramfunc")
static void ValInit(BoostController *p)
{
    p->flag.Error = Error_NOERR;
}

//=================================================================================================
// StateMInit (私有)
//=================================================================================================
/**
 * @brief     `Init` (初始化) 状态的处理函数。
 * @details   这是上电后的初始状态。它会复位故障标志，设置一个初始的LED模式以指示
 *            系统处于初始化阶段，然后立即转换到 `Wait` (等待) 状态。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(StateMInit, ".TI.ramfunc")
static void StateMInit(BoostController *p)
{
    ValInit(p);
    LED1_ON;
//    LED1_ON; LED2_OFF; LED3_OFF; LED4_OFF; LED5_OFF;
    p->flag.StateM = Wait;
}

//=================================================================================================
// StateMWait (私有)
//=================================================================================================
/**
 * @brief     `Wait` (等待) 状态的处理函数。
 * @details   初始化后，系统会在此状态下停留一段预设的时间（约1.5秒）。这允许电源轨和
 *            外部组件稳定下来。如果在此期间没有发生故障，它将转换到 `Rise` (软启动) 状态。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(StateMWait, ".TI.ramfunc")
static void StateMWait(BoostController *p)
{
    static unsigned int CntS = 0;
//    LED1_OFF; LED2_ON; LED3_OFF; LED4_OFF; LED5_OFF;
    CntS++;
    // 等待约300次调用 (在5ms的中断周期下，约为1.5秒)
    if((CntS > 300) && (p->flag.Error == Error_NOERR)) {
        CntS = 0; // 为下次进入此状态复位计数器
        p->flag.StateM = Rise; // 转换到软启动状态
    }
}

//=================================================================================================
// StateMRise辅助函数（私有）
//=================================================================================================
/**
 * @brief     处理Rise状态中的控制配置错误。
 * @details   立即关闭PWM、记录配置错误，并将主状态机切换到Err状态。
 */
#pragma CODE_SECTION(StateMControlConfigError, ".TI.ramfunc")
static void StateMControlConfigError(BoostController *p)
{
    // 先关闭PWM，再记录错误并进入故障状态
    PWM_Disable();
    setRegBits(p->flag.Error, Error_CTRL_MODE);
    p->flag.RiseInitialized = 0U;
    p->flag.StateM = Err;
}

/**
 * @brief     将指定变量按固定步长斜坡调整到目标值。
 * @details   同时支持上升和下降过程；到达或越过目标值时进行钳位并返回完成标志。
 * @return    1U表示已到达目标值，0U表示斜坡尚未完成。
 */
#pragma CODE_SECTION(StateMRiseRampToTarget, ".TI.ramfunc")
static Uint16 StateMRiseRampToTarget(float *value, float target, float step)
{
    float nextValue;

    // 步长无效时直接设置目标值，避免斜坡无法结束
    if (step <= 0.0f)
    {
        *value = target;
        return 1U;
    }

    // 根据当前值与目标值的关系选择上升或下降
    if (*value < target)
    {
        nextValue = *value + step;
        // 防止最后一步超过目标值
        *value = (nextValue < target) ? nextValue : target;
    }
    else if (*value > target)
    {
        nextValue = *value - step;
        // 防止最后一步低于目标值
        *value = (nextValue > target) ? nextValue : target;
    }

    // 返回本次更新后是否已经到达目标值
    return (*value == target) ? 1U : 0U;
}

/**
 * @brief     执行Rise状态的首次进入初始化。
 * @details   复位全部PI/SMC控制器，设置当前控制模式的安全初值，然后更新并使能PWM。
 * @return    1U表示初始化成功，0U表示控制模式配置错误。
 */
#pragma CODE_SECTION(StateMRiseEnter, ".TI.ramfunc")
static Uint16 StateMRiseEnter(BoostController *p)
{
    // 清除各控制器的历史状态，保证每次软启动条件一致
    PI_Reset(&p->ctr.V_Loop);
    PI_Reset(&p->ctr.I_Loop);
    SMC_Reset(&p->ctr.I_SMC_Loop);
    SMC_Reset(&p->ctr.V_SMC_Loop);
    p->ctr.VoltageLoopCounter = 0U;

    // 先设置所有控制模式共用的安全初值
    p->ctr.V_Ref = RISE_V_REF_START;
    p->ctr.I_Ref = 0.0f;
    p->ctr.u = p->ctr.MIN_Duty;

    // 单环模式需要从非零电流参考开始
    switch (p->flag.CtrlMode)
    {
        case CTRL_MODE_DUAL_LOOP:
        case CTRL_MODE_OPEN_LOOP:
            break;

        case CTRL_MODE_SINGLE_LOOP:
            p->ctr.I_Ref = RISE_I_REF_START;
            break;

        default:
            StateMControlConfigError(p);
            return 0U;
    }

    // 将安全占空比写入PWM后再使能输出
    PWM_Update(p);
    PWM_Enable();
    p->flag.RiseInitialized = 1U;
    return 1U;
}

/**
 * @brief     更新当前控制模式对应的软启动变量。
 * @details   双环、单环和开环模式分别更新V_Ref、I_Ref和占空比u。
 *            禁用软启动时直接写入目标值。
 * @return    1U表示已到达目标值，0U表示尚未完成或发生配置错误。
 */
#pragma CODE_SECTION(StateMRiseUpdate, ".TI.ramfunc")
static Uint16 StateMRiseUpdate(BoostController *p, Uint16 rampEnabled)
{
    float *value;
    float target;
    float step;

    // 根据控制模式选择需要调整的变量、目标值和步长
    switch (p->flag.CtrlMode)
    {
        case CTRL_MODE_DUAL_LOOP:
            value = &p->ctr.V_Ref;
            target = RISE_V_TARGET;
            step = RISE_V_REF_STEP;
            break;

        case CTRL_MODE_SINGLE_LOOP:
            value = &p->ctr.I_Ref;
            target = RISE_I_TARGET;
            step = RISE_I_REF_STEP;
            break;

        case CTRL_MODE_OPEN_LOOP:
            value = &p->ctr.u;
            target = RISE_D_TARGET;
            step = RISE_D_STEP;
            break;

        default:
            StateMControlConfigError(p);
            return 0U;
    }

    // 禁用软启动时直接跳到当前模式的目标值
    if (rampEnabled == 0U)
    {
        *value = target;
        return 1U;
    }

    // 启用软启动时按配置步长逐次逼近目标值
    return StateMRiseRampToTarget(value, target, step);
}

//=================================================================================================
// StateMRise（私有）
//=================================================================================================
/**
 * @brief     `Rise`（软启动）状态处理函数。
 * @details   首次进入时初始化控制器和PWM；之后根据软启动配置更新当前模式的参考值。
 *            到达目标值后清除入口标志，并将主状态机切换到Run状态。
 * @param[inout] p  指向Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(StateMRise, ".TI.ramfunc")
static void StateMRise(BoostController *p)
{
    Uint16 rampDone;

    // 每次重新进入Rise状态时只执行一次安全初始化
    if (p->flag.RiseInitialized == 0U)
    {
        StateMRiseEnter(p);
        return;
    }

    // 根据软启动配置选择斜坡更新或直接设置目标值
    if (p->flag.SoftStartEn == SOFTSTART_ENABLE)
    {
        rampDone = StateMRiseUpdate(p, 1U);
    }
    else if (p->flag.SoftStartEn == SOFTSTART_DISABLE)
    {
        rampDone = StateMRiseUpdate(p, 0U);
    }
    else
    {
        StateMControlConfigError(p);
        return;
    }

    // 配置错误函数可能已经将状态切换到Err，避免继续进入Run
    if (p->flag.StateM != Rise)
    {
        return;
    }

    // 当前模式到达目标值后结束软启动
    if (rampDone != 0U)
    {
        p->flag.RiseInitialized = 0U;
        p->flag.StateM = Run;
    }
}

//=================================================================================================
// StateMRun (私有)
//=================================================================================================
/**
 * @brief     `Run` (运行) 状态的处理函数。
 * @details   这是变换器的正常工作状态。高速控制环路（在ADC中断中）正在主动调节输出。
 *            此函数本身可以用于处理那些只应在正常运行时发生的低频任务，例如状态通信
 *            或根据外部命令调整操作。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(StateMRun, ".TI.ramfunc")
static void StateMRun(BoostController *p)
{
//    LED1_OFF; LED2_OFF; LED3_OFF; LED4_ON; LED5_OFF; // 指示正常运行状态
    // 在运行状态下，PI控制器应在定时器中断中持续运行。
    // 此函数可用于处理任何特定于运行状态的、以较慢速率检查的逻辑。
}

//=================================================================================================
// StateMErr (私有)
//=================================================================================================
/**
 * @brief     `Err` (错误) 状态的处理函数。
 * @details   当任何保护机制检测到故障时，系统进入此状态。首要动作是立即禁用PWM以保护
 *            硬件。系统将保持在此状态（锁定故障），直到复位或执行特定的恢复操作。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(StateMErr, ".TI.ramfunc")
static void StateMErr(BoostController *p)
{
//    LED1_OFF; LED2_OFF; LED3_OFF; LED4_OFF; LED5_ON; // 指示错误状态
    PWM_Disable();
    // 在实际应用中，您可能会在此处添加故障处理逻辑，
    // 例如锁定故障、延迟后尝试重启等。
}

//=================================================================================================
// SwOCP (私有)
//=================================================================================================
/**
 * @brief     实现带延时的软件过流保护。
 * @details   检查电感电流(`IL`)是否超过定义阈值。为了防止由短暂瞬变引起的误触发，
 *            故障条件必须持续一定数量的调用次数（即一个时间延迟）后，才会触发故障。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(SwOCP, ".TI.ramfunc")
static void SwOCP(BoostController *p)
{
    #define MAX_OCP_VAL 15.0f
    static unsigned int OCPCnt = 0; // 用于故障持续性计数的计数器
    if((p->adc.IL > MAX_OCP_VAL) && (p->flag.StateM == Run)) {
        OCPCnt++;
        if(OCPCnt > 100) { // 如果条件持续超过100次调用
            OCPCnt = 0;
            PWM_Disable();
            setRegBits(p->flag.Error, Error_OCP); // 设置OCP故障标志
            p->flag.StateM = Err; // 转换到错误状态
        }
    } else {
        OCPCnt = 0; // 如果条件不满足，则复位计数器
    }
}

//=================================================================================================
// SwOVP (私有)
//=================================================================================================
/**
 * @brief     实现带延时的软件过压保护。
 * @details   检查输出电压(`Vout`)是否超过定义阈值。使用时间延迟以避免在瞬态事件期间
 *            发生错误触发。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(SwOVP, ".TI.ramfunc")
static void SwOVP(BoostController *p)
{
    #define MAX_OVP_VAL 150.0f
    static unsigned int OVPCnt = 0;
    if (p->adc.Vout > MAX_OVP_VAL) {
        OVPCnt++;
        if(OVPCnt > 20)
        { // OVP可能更危险，所以延迟较短
            OVPCnt = 0;
            PWM_Disable();
            setRegBits(p->flag.Error, Error_OVP);
            p->flag.StateM = Err;
        }
    } else {
        OVPCnt = 0;
    }
}

//=================================================================================================
// SwUVP (私有)
//=================================================================================================
/**
 * @brief     实现带延时的软件欠压保护。
 * @details   检查在`Run`状态下，输出电压(`Vout`)是否低于定义阈值。这可能表示负载丢失
 *            或其他系统问题。时间延迟可以防止在大的负载阶跃期间发生误触发。
 * @param[inout] p  指向主Boost控制器结构体的指针。
 */
#pragma CODE_SECTION(SwUVP, ".TI.ramfunc")
static void SwUVP(BoostController *p)
{
    #define MIN_UVP_VAL 25.0f
    static unsigned int UVPCnt = 0;
    if ((p->adc.Vout < MIN_UVP_VAL) && (p->flag.StateM == Run)) {
        UVPCnt++;
        if(UVPCnt > 100) {
            UVPCnt = 0;
            PWM_Disable();
            setRegBits(p->flag.Error, Error_UVP);
            p->flag.StateM = Err;
        }
    } else {
        UVPCnt = 0;
    }
}

void boost_converter_Init(BoostController *p)
{
    memset(p, 0, sizeof(BoostController));
}
