# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

TI C2000 (TMS320F28335) 微逆变器 Boost 升压变换器控制程序**模板**（`dah_boost_template`）。CCS 12.8.0 工程，TI CGT 22.6.1.LTS，COFF 格式，FPU32 硬件浮点。

## 构建与调试

- **IDE**: Code Composer Studio 12.8.0，打开 `.ccsproject` 即可导入工程
- **CCS 项目名**: `dah_boost_template`（见 `.project`）。注意 `.launches/dah_boost_v5_for_double_direction.launch` 是旧工程遗留的启动配置，名称不代表本工程
- **编译器**: TI C2000 CGT 22.6.1.LTS (`D:/apps/ccstudio/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS`)
- **输出**: `Debug/dah_boost_template.out`
- **调试器**: XDS100v1 USB JTAG (`targetConfigs/TMS320F28335.ccxml`，注意是 v1 不是 v2)
- **构建**: CCS IDE 中点 Build，或命令行在 `Debug/` 目录下执行 `make`（依赖 CCS 生成的 makefile）

### ⚠️ 链接配置当前不一致（重要）

- `.cproject` / `.ccsproject` 声明 `LINKER_COMMAND_FILE=28335_RAM_lnk.cmd`（RAM 调试），但**该文件不存在于工程中**
- 现有构建产物（`Debug/makefile` + `Debug/dah_boost_template.map`，构建于 2026-07-11）实际链接的是 `DSP2833x_Librsries/F28335.cmd`（Flash）+ `DSP2833x_Librsries/DSP2833x_Headers_nonBIOS.cmd` + `IQmath.lib`；map 显示 `.TI.ramfunc` 装载于 FLASHD(0x320000)、运行于 RAML0，`.text` 在 FLASHA，入口 0x3388ee → **最后一次实际构建是 Flash 部署**
- `_FLASHOK` 已在 `function.h:18` 定义为 `1`，`main.c` 中的 ramfunc memcpy 生效（与 Flash 链接一致）
- **后果**: 现在直接重新 Build 会按 `.cproject` 引用缺失的 `28335_RAM_lnk.cmd`，链接会失败。动手改代码前先定方案：
  - **Flash 部署**（与现有产物一致）：把工程链接器设置改为 `DSP2833x_Librsries/F28335.cmd`
  - **RAM 调试**：恢复/补齐 `28335_RAM_lnk.cmd`，并把 `_FLASHOK` 置 0
- `DSP2833x_Headers_nonBIOS.cmd`（外设寄存器映射）两种方式都必须留在链接列表内

## 代码架构

### 核心设计模式：句柄架构 (Handle-based Architecture)

全部控制状态封装在 `BoostController` 结构体（`APP/APP_Libraries/function.h:143-149`），包含四个子结构：

| 子结构 | 用途 |
|--------|------|
| `BoostFlags` | 状态机状态(`StateM`)、故障标志(`Error`)、控制标志 |
| `PwmValues` | PWM 频率/占空比/TBPRD/死区参数 |
| `AdcValues` | 采样值：`IL`(电感电流)、`Vout`(输出电压)、`Vin`(输入电压)、`Iout`(输出电流) + 标定系数 |
| `ControlValues` | 控制信号：`V_Loop`/`I_Loop` (`PIController`)、`I_SMC_Loop` (`SMController`)、`V_Ref`/`I_Ref`/`u`、占空比限幅 |

`APP/boost_app.c` 中定义全局静态实例 `boost_converter`，通过 `GetBoostHandle()` getter 供各中断服务函数访问（adc.c, timer0.c, timer1.c）。

### 控制策略

**控制模式选择**（枚举定义于 `function.h:41-54`，当前值在 `Control_Init()` 中设置，`APP/function.c:171`）:

| 模式 | 枚举值 | `CtrlMode` | 说明 |
|------|--------|-----------|------|
| 双闭环 (默认) | `CTRL_MODE_DUAL_LOOP` | 0 | 电压外环 PI → 电流内环 (PI 或 SMC) |
| 电流单环 | `CTRL_MODE_SINGLE_LOOP` | 1 | 仅电流内环，I_Ref 由软启斜坡直接给定 |
| 开环 | `CTRL_MODE_OPEN_LOOP` | 2 | 占空比由软启斜坡直接给定，无反馈 |

电流内环可选两种算法（`InnerLoopMode`）:
- `INNER_LOOP_PI` = 0（当前默认）：增量式 PI
- `INNER_LOOP_SMC` = 1：滑模控制（已实现，未激活）

切换方式：修改 `Control_Init()` 中 `p->flag.CtrlMode` 和 `p->flag.InnerLoopMode` 的赋值。

**双闭环 PI 控制**（外环电压，内环电流）:
- `PI_ComputeIncremental_AntiWindup()`: 通用增量式 PI + 抗积分饱和（仅当输出未饱和时才更新积分累加器），电压外环和电流内环共用
- 在 `adc_isr()` 中调用 `Control_Loop_Compute()`: 根据 `CtrlMode` / `InnerLoopMode` 选择计算路径
- 双环模式: `I_Ref = PI_Compute(...V_Loop, V_Ref, Vout)` → `u = PI_Compute(...I_Loop, I_Ref, IL)`
- 每个 PI 环的参数和状态封装在 `PIController` 结构体中（`ctr.V_Loop` / `ctr.I_Loop`）
- `PWM_Update()`: 将 `u` 写入 `EPwm1Regs.CMPA.half.CMPA`（CMPA = TBPRD × duty）

**SMC 滑模控制器**（已实现，定义于 `SMController` 结构体）:
- 控制律: `D = D_eq + K * sat(S / Phi)`
- `D_eq = 1 - Vin/Vout`（Boost 稳态等效控制；Vout < 0.5V 时置 0 防除零），`S = IL_ref - IL`（滑模面=电流误差）
- `sat()` 为边界层饱和函数，在 ±Phi 范围内线性，范围外硬切换，消除抖振
- 参数: `K=0.05`, `Phi=0.5`, 输出限幅 [0.1, 0.7]
- 关键函数: `SMC_Init()` / `SMC_Reset()` / `SMC_Compute()`（均位于 `APP/function.c`）
- 注意: Vin 当前恒为 0（未采样，见 ADC 通道映射），SMC 需要 Vin 传感器接入才能正确计算 D_eq

**双点采样** (ePWM 触发 ADC):
- 谷值采样: SEQ1, SOCA 在 TBCTR=0 触发 → `Process_Valley_Samples()`（读 RESULT0-3）
- 峰值采样: SEQ2, SOCB 在 TBCTR=PRD 触发 → `Process_Peak_Samples()`（读 RESULT4-7）
- **两个 SEQ 中断都会完整执行 采样 → `Control_Loop_Compute()` → `PWM_Update()`**，即每个 PWM 周期快速环跑 2 次
- ADC 结果经 IIR 低通滤波 (`IIR_ALPHA=0.001f`) 和线性标定转为物理量（物理量 = Gain × ADC电压 + Offset）
- 同步采样模式 (`SMODE_SEL=1`)：每个 CONV 同时采 A/B 两路，A 路结果 → RESULTn，B 路结果 → RESULTn+4

**ADC 通道映射** (同步采样，SEQ1 与 SEQ2 通道选择相同):

| 转换位 | 通道 (同步对) | 物理量 | ADCRESULT | 状态 |
|--------|--------------|--------|-----------|------|
| CONV00/04 | A0 / B0 | IL (电感电流) | RESULT0/4 | **已使用**（谷值读 RESULT0，峰值读 RESULT4） |
| CONV01/05 | A1 / B1 | Vout (输出电压) | RESULT1/5 | **已使用** |
| CONV02/06 | A0 / B0 | (同 IL，冗余) | RESULT2/6 | 已读取、已滤波，但未使用 |
| CONV03/07 | A1 / B1 | (同 Vout，冗余) | RESULT3/7 | 已读取、已滤波，但未使用 |

`Vin` 和 `Iout` 在采样函数中被赋值为 `0.0f`，标定系数也为 0 — **这两个传感器尚未接入**。注意：`Iout` 恒为 0 使 `ShortOff()` 短路保护永远无法触发。

### 中断系统架构

| 中断源 | PIE/CPU | 周期 | 功能 |
|--------|---------|------|------|
| ADC SEQ1/SEQ2 | PIE Group1 INT6 | PWM 同步，每周期 2 次 (20kHz) | **快速环**: ADC 采样 → 双环 PI 计算 → PWM 更新 |
| CPU Timer1 | XINT13 (CPU) | 5ms (150MHz, 5000μs) | **慢速环**: 状态机 `State_M` |
| CPU Timer0 | PIE Group1 INT7 | 10ms 配置（main.c 未调用 `Timer0_Init`） | 备用快速环（未启用；ISR 仅调 `Adc_Sample_Process`=谷值采样，PI/PWM 更新被注释） |

中断优先级: ADC (CPU INT1) > Timer1 (CPU INT13)

**重要**: Timer1 ISR (`APP/timer1.c:67`) 中 `SlowP(p)` 被注释掉了，当前仅执行 `State_M(p)`。软件保护 OCP/OVP/UVP **未在中断中周期检测**（`SwOCP`/`SwOVP`/`SwUVP` 只被 `SlowP` 调用）。如需启用周期性软件保护，取消 `timer1.c:67` 的注释。

### 状态机

`StateM` 枚举（`function.h:32-39`）定义五个状态：

```
Init → Wait(1.5s延时) → Rise(参考斜坡) → Run(正常运行) → Err(故障锁死)
```

- **Init**: 清除故障标志，转 Wait
- **Wait**: 延时约 1.5s（300 次 × 5ms Timer1），等待电源稳定，转 Rise
- **Rise**: 按 `CtrlMode` 斜坡——双环 `V_Ref` 0→24V（步长 0.1V/5ms）；单环 `I_Ref` 0.6→2.4A；开环 `u` 0.1→0.3。首次进入时复位 PI/SMC 并使能 PWM，斜坡完成后转 Run
- **Run**: 正常运行，快速 PI 环在 ADC 中断中持续运行（当前 ISR 中无慢速保护）
- **Err**: 关闭 PWM，锁死（需复位恢复）

### 保护功能

| 保护 | 阈值 | 延时计数 | 检测位置 |
|------|------|----------|----------|
| 软件 OCP | IL > 15A (Run态) | 100 次 (500ms) | `SwOCP()` via `SlowP()` |
| 软件 OVP | Vout > 150V | 20 次 (100ms) | `SwOVP()` via `SlowP()` |
| 软件 UVP | Vout < 25V (Run态) | 100 次 (500ms) | `SwUVP()` via `SlowP()` |
| 输出短路 | Iout > 15A && Vout < 7.5V | 立即 | `ShortOff()`（依赖 Iout 采样，当前无法触发） |
| 硬件 TZ | TZ1 引脚 + 软件 `TZFRC.OST` | 单次触发 | ePWM 硬件关断 (TZ_FORCE_LO) |

⚠️ `SlowP()` 和 `ShortOff()` 当前都**未被任何中断调用** → 软件保护实际不生效，目前仅靠硬件 TZ1。
所有软件保护触发后设置故障标志并转入 Err 状态，调用 `PWM_Disable()`（写 `TZFRC.bit.OST=1`）关闭 PWM；`PWM_Enable()` 清 `TZCLR.bit.OST`。

### 其他模块

- **MPPT** (`APP/mppt.c` + `mppt.h`): 扰动观察法 (P&O) 数据结构和函数，**未集成**到 Boost 控制环路
- **exit** (`APP/exit.c`): XINT1 外部中断/按键初始化（GPIO12），main.c 未调用
- **LED** (`APP/led.c` + `led.h`): GPIO64-68 (LED5-1) 与 GPIO10-11 (LED6-7)；Timer1 ISR 中 LED1 每 5ms 翻转，采样函数中 LED2 翻转

### 关键参数速查

- CPU 频率: 150MHz；PWM 频率: 20kHz（TBPRD = 150e6/(2×20e3) = 3750，增减计数模式）
- 目标电压 V_TARGET: 24V；占空比范围: [0.1, 0.7]
- PI 参数: `V_Ki=113.5965`, `V_Kp=0.4911`；`I_Ki=3155105.4373/TBPRD`, `I_Kp=832.1417/TBPRD`；`Ki_Ts=Ki*Ts` 预乘
- 电压环限幅: OutMax=9.0 (I_Ref 上限 9A), OutMin=0；误差钳位 ±5V
- 死区: `pwm.DeadTime=1`（意图单位为 SYSCLKOUT 周期），但实际写入 `DBRED=DBFED=150` 个 TBCLK → 约 1µs @150MHz（HSPCLKDIV=CLKDIV=1）
- 标定系数: IL_Gain=-3.6458, IL_Offset=6.1978；Vout_Gain=16.311, Vout_Offset=-0.7392

### 文件依赖关系

```
main.c
 └── boost_app.h → 应用初始化、调度启动和后台任务

boost_app.c
 ├── function.h → BoostController 定义和控制初始化
 ├── adc.h      → ADC_Init(), adc_isr()
 ├── timer1.h   → Timer1_Init(), Timer1_Start()
 ├── epwm.h     → EPWM1_Init() 和时基控制
 ├── led.h      → LED_Init()
 ├── dac.h      → DAC 调试初始化和后台服务
 └── sci.h      → SCI-A初始化和后台轮询通信

adc.c / timer0.c / timer1.c  → 通过 GetBoostHandle() 获取 BoostController 指针
function.c → 所有控制核心逻辑 (PI/SMC/保护/状态机/PWM更新)
exit.c     → XINT1 外部中断 (未使用)
```

### 编码注意事项

- 时间关键函数使用 `#pragma CODE_SECTION(func, ".TI.ramfunc")` 放置到 RAM 运行（Flash 构建时由 boost_app.c 的 memcpy 从 FLASHD 拷贝到 RAML0）
- 寄存器访问需 `EALLOW`/`EDIS` 保护
- ADC 结果低 4 位为无效位，需 `>> 4` 右移获取 12 位值
- COFF 格式（非 ELF），`.cproject` 中 `OUTPUT_FORMAT=COFF`
- 位域操作通过 TI 的位域结构体（如 `EPwm1Regs.CMPA.half.CMPA`），注意 `.half.` 用于 16 位访问
- 头文件 include 路径依赖外部目录 `D:\AAAstudy\DSPF28335_Program\DSP2833x_Libraries\DSP2833x_common\include` 和 `...\headers\include`（`.cproject` 中配置，本工程 `DSP2833x_Librsries/` 仅为板级源文件副本）
- 链接使用 `IQmath.lib` 与 `lrts2800_fpu32.lib`（FPU32）

## Notes

（预留）
