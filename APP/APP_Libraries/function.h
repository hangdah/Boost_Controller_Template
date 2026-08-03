/*
 * function.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 *  REVISED for modular, handle-based architecture (FIXED version).
 */

#ifndef APP_APP_LIBRARIES_FUNCTION_H_
#define APP_APP_LIBRARIES_FUNCTION_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"
#include "epwm.h"
#include "led.h"
#include <string.h>

#define _FLASHOK        1

#define IIR_ALPHA 0.001f           // 平滑系数，越小越平滑，响应越慢，IIR_ALPHA = (2*pi*f_cutoff)/f_sampling

/*****************************故障类型*****************/
#define     Error_NOERR         0x0000//无故障
#define     Error_OVP           0x0001//输出过压
#define     Error_OCP           0x0002//输出过流
#define     Error_OUT_SHORT     0x0004//输出短路
#define     Error_UVP           0x0008//输出欠压

#define     Error_CTRL_MODE     0x0010//控制模式或软启动配置错误

#define setRegBits(reg, mask)               (reg |= (unsigned int)(mask))

/*****************************状态机枚举量*****************/
typedef enum
{
    Init,//初始化
    Wait,//空闲等待
    Rise,//软启
    Run,//正常运行
    Err//故障
}STATE_M;

/*****************************电流内环模式枚举量*****************/
typedef enum
{
    INNER_LOOP_PI  = 0,  // PI 电流内环
    INNER_LOOP_SMC = 1   // SMC 电流内环
}INNER_LOOP_MODE;

/*****************************电压外环模式枚举量*****************/
typedef enum
{
    OUTER_LOOP_PI  = 0,  // PI 电压外环
    OUTER_LOOP_SMC = 1   // SMC 电压外环
}OUTER_LOOP_MODE;

/*****************************控制模式枚举量*****************/
typedef enum
{
    CTRL_MODE_DUAL_LOOP   = 0,  // 电压电流双环
    CTRL_MODE_SINGLE_LOOP = 1,  // 电流单环
    CTRL_MODE_OPEN_LOOP   = 2   // 开环
}CTRL_MODE;

/*****************************软启动使能枚举量*****************/
typedef enum
{
    SOFTSTART_ENABLE  = 0,  // 使能软启动
    SOFTSTART_DISABLE = 1   // 禁止软启动
}SOFTSTART_CTRL;

// --- 模块化重构核心: 句柄定义 ---

/*****************************标志FLAG*****************/
typedef struct
{
    unsigned int    StateM;         // 状态机标志
    unsigned int    Ctr;            // 控制器标志 (预留)
    unsigned int    InnerLoopMode;  // 电流内环模式: INNER_LOOP_PI / INNER_LOOP_SMC
    unsigned int    OuterLoopMode;  // 电压外环模式: OUTER_LOOP_PI / OUTER_LOOP_SMC
    unsigned int    CtrlMode;       // 控制模式: CTRL_MODE_DUAL_LOOP / SINGLE_LOOP / OPEN_LOOP
    unsigned int    SoftStartEn;    // 软启动使能: SOFTSTART_ENABLE / SOFTSTART_DISABLE
    unsigned int    Error;          // 故障标志
    unsigned int    RiseInitialized;// Rise状态入口初始化标志
} Flags;

/*****************************输出PWM信息*****************/
typedef struct
{
    float   PWM_Freq;
    float   CPU_Freq;
    float   Duty;
    float   TBPRD;
    float   AComp;
    float   BComp;
    float   DeadTime;
    float   Ts;
} PwmValues;

/*****************************采样变量结构体*****************/
typedef struct
{
    float   IL;             // 电感电流
    float   Iout;           // 输出电流平均值
    float   Vout;           // 输出电压
    float   Vin;            // 输入电压平均值
    float   IL_Gain;        // IL 标定增益
    float   IL_Offset;      // IL 标定偏置
    float   Vout_Gain;      // Vout 标定增益
    float   Vout_Offset;    // Vout 标定偏置
    float   Vin_Gain;       // Vin 标定增益 (预留)
    float   Vin_Offset;     // Vin 标定偏置 (预留)
    float   Iout_Gain;      // Iout 标定增益 (预留)
    float   Iout_Offset;    // Iout 标定偏置 (预留)
} AdcValues;

/*****************************PI控制器模块*****************/
typedef struct
{
    float   Kp, Ki, Ki_Ts;       // PI 参数 (Kp, Ki, Ki*Ts预乘)
    float   Error1, Error2;      // 当前误差 / 前一拍误差
    float   Out, DeltaOut;       // 积分累加器 / 增量输出
    float   OutMax, OutMin;      // 输出限幅
    float   ErrMax, ErrMin;      // 误差限幅 (ErrMax==0 且 ErrMin==0 时跳过误差钳位)
} PIController;

/*****************************SMC控制器模块*****************/
typedef struct
{
    float   K;                  // 切换增益
    float   Phi;                // 边界层厚度 (消除抖振)
    float   Lambda;             // 积分滑模面系数 (电压外环使用)
    float   Integral;           // 误差积分状态 (电压外环使用)
    float   IntegralMax;        // 积分上限
    float   IntegralMin;        // 积分下限
    float   D_eq;               // 等效控制量 (Duty 或 I_Ref)
    float   U_sw;               // 切换控制量
    float   RawOut;             // 限幅前输出
    float   S;                  // 滑模面
    float   Out;                // 控制器输出
    float   OutMax, OutMin;     // 输出限幅
} SMController;

/*****************************环路控制信息*****************/
typedef struct
{
    PIController V_Loop;         // 电压外环 PI
    PIController I_Loop;         // 电流内环 PI
    SMController I_SMC_Loop;     // 电流内环 SMC
    SMController V_SMC_Loop;     // 电压外环 SMC
    Uint16  VoltageLoopCounter;  // 电压SMC分频计数器
    Uint16  VoltageLoopDivider;  // 电压SMC分频系数
    float   VoltageLoopTs;       // 电压SMC实际执行周期 (s)
    float   Cout;                // Boost输出电容 (F，需按硬件标定)
    float   u;                    // 最终控制量输出 (写入 PWM 比较器)
    float   V_Ref;               // 电压参考值
    float   I_Ref;               // 电流参考值 (电压外环输出 → 电流内环输入)
    float   MAX_Duty, MIN_Duty;  // 占空比限幅
    float   DeadTime;
    float   MAX_DeadTime, MIN_DeadTime;
} ControlValues;

// 主句柄 (Handle): 封装了Boost变换器的所有状态和参数
typedef struct
{
    Flags           flag;
    PwmValues       pwm;
    AdcValues       adc;
    ControlValues   ctr;
} BoostController;


// --- 公共函数声明 ---
// 这些函数将是外部模块（如main.c, timer.c）与本模块交互的接口

// 初始化/设置

void Control_Init(BoostController *p);
void PWM_Values_Init(BoostController *p);
float Get_TBPRD(BoostController *p); // Getter函数，用于安全获取内部数据
void boost_converter_Init(BoostController *p);

// PI 控制器通用函数
void PI_Init(BoostController *p);
void PI_Reset(PIController *pi);
float PI_ComputeIncremental_AntiWindup(PIController *pi, float ref, float fbk);

// SMC 控制器函数
void SMC_Init(BoostController *p);
void SMC_Reset(SMController *smc);
float SMC_Compute(SMController *smc, float IL_ref, float IL, float Vin, float Vout);

// 中断中调用的高速函数
void Adc_Sample_Process(BoostController *p); // 这是个通用名称，用于 timer0
void Process_Valley_Samples(BoostController *p);
void Process_Peak_Samples(BoostController *p);
void Control_Loop_Compute(BoostController *p);
void PWM_Update(BoostController *p);

// 定时器中调用的慢速函数
void State_M(BoostController *p);
void SlowP(BoostController *p);
void ShortOff(BoostController *p);

// PWM硬件控制函数
void PWM_Disable(void);
void PWM_Enable(void);

#endif /* APP_APP_LIBRARIES_FUNCTION_H_ */
