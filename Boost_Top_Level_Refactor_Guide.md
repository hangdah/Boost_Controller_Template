# Boost 控制代码顶层重构指导

## 1. 任务背景

当前工程为基于 TI F28335 DSP 的同步整流 Boost 控制程序，主要参数如下：

- 额定功率：600 W
- 输入电压：30–60 V
- 输出电压：75 V
- 开关频率：160 kHz
- 电感：46 μH ±20%
- 控制程序包含 ADC、ePWM、Timer、LED、MPPT、DAC 调试及 Boost 控制器等模块

当前 `main.c` 中直接包含系统时钟、中断、控制器、外设、PWM 时基、Timer 和后台任务的全部初始化代码。此次重构的目标是简化 `main()`，建立清晰的顶层初始化和运行框架，同时保持现有底层驱动、控制算法及中断逻辑不被无关修改。

---

## 2. Codex 的任务

请先完整阅读工程代码，重点检查以下内容：

1. `main.c` 当前初始化顺序。
2. `BoostController` 的定义、初始化函数及访问方式。
3. `boost_converter_Init()`、`Control_Init()` 和 `Get_TBPRD()` 的具体实现。
4. `EPWM_TimeBase_Freeze()`、`EPWM_TimeBase_Start()`、`PWM_Enable()`、`PWM_Disable()` 的具体实现。
5. `Timer1_Init()`、`Timer1_Start()`、`Timer1_Freeze()` 的具体实现。
6. ADC、ePWM 和 Timer 中断服务函数的触发关系。
7. `DAC_DEBUG_ENABLE`、`DacDebug_Init()` 和 `DacDebug_Service()` 的使用方式。
8. `_FLASHOK` 条件下 RAM 函数搬运及 `InitFlash()` 的现有实现。
9. 工程中是否已经存在应用层、状态机层或板级初始化层，避免重复创建功能相同的模块。

在理解现有代码后，对顶层结构进行重构。

不要仅根据本说明中的函数名称直接覆盖代码；应以工程中的真实接口、类型和调用关系为准。

---

## 3. 本次重构目标

将 `main()` 简化为以下三个阶段：

```c
void main(void)
{
    BoostApp_Init(&boost_converter);
    BoostScheduler_Start();

    for (;;)
    {
        BoostApp_BackgroundTask(&boost_converter);
    }
}
```

顶层结构建议如下：

```text
main
├── BoostApp_Init
│   ├── MCU_Init
│   ├── Interrupt_Init
│   ├── BoostControl_Init
│   └── Board_Init
├── BoostScheduler_Start
└── BoostApp_BackgroundTask
```

本次重构优先在 `main.c` 内通过 `static` 函数完成，不要为了形式上的模块化创建大量新文件。

只有在工程已经存在合适的应用层文件，或 `main.c` 明显不适合继续承载这些函数时，才考虑将其拆分为 `boost_app.c/.h`。若决定新增文件，必须说明原因，并保持新增文件数量最少。

---

## 4. 顶层函数职责

### 4.1 `MCU_Init()`

负责 DSP 内核相关初始化，包括：

- 禁止全局中断；
- 初始化系统时钟和外设时钟；
- `_FLASHOK` 条件下搬运 RAM 函数；
- 初始化 Flash 等待状态。

建议顺序为：

```c
DINT;
InitSysCtrl();

#ifdef _FLASHOK
memcpy(...);
InitFlash();
#endif
```

要求：

- 检查工程链接文件和 TI 示例代码，确认 RAM 函数搬运参数类型正确；
- 不要修改链接符号名称；
- 不要重复执行已有的系统初始化；
- 不要在此函数中初始化 ADC、ePWM、Timer、LED 或 DAC。

---

### 4.2 `Interrupt_Init()`

负责 CPU 和 PIE 中断系统的基础初始化，包括：

```c
InitPieCtrl();
IER = 0x0000;
IFR = 0x0000;
InitPieVectTable();
```

必要时清除 PIE ACK：

```c
EALLOW;
PieCtrlRegs.PIEACK.all = 0xFFFF;
EDIS;
```

要求：

- 外设中断向量的映射仍由对应外设初始化函数负责，除非当前工程已有统一的中断注册方式；
- 不要在该函数中提前执行 `EINT`；
- 不要改变现有 ISR 的中断组、优先级和 PIE 配置，除非发现明确错误；
- 若发现中断标志清除顺序存在风险，应单独指出，不要顺手大改全部 ISR。

---

### 4.3 `BoostControl_Init(BoostController *p_boost)`

负责控制器软件对象和控制参数初始化，包括：

```c
boost_converter_Init(p_boost);
Control_Init(p_boost);
```

该函数只管理软件控制对象，不初始化硬件外设。

需要检查并确保初始化内容覆盖：

- ADC 采样变量；
- 控制参考值；
- PI、SMC 或其他控制器参数；
- 控制器历史误差和积分状态；
- 占空比上下限；
- 软启动相关变量；
- 状态机初始状态；
- 故障标志；
- 控制环分频计数器。

不要在没有确认现有结构体定义的情况下新增重复变量。

若需要新增或修改结构体成员，必须在结果中逐项说明：

- 变量名；
- 类型；
- 初始值；
- 使用位置；
- 新增原因。

---

### 4.4 `Board_Init(Uint16 pwm_tbprd)`

负责板级硬件外设配置，建议包括：

```c
EPWM_TimeBase_Freeze();

LED_Init();
Timer1_Init(CPU_FREQ_MHZ, SUPERVISORY_PERIOD_US);
EPWM1_Init(pwm_tbprd);
PWM_Disable();
ADC_Init();

#if DAC_DEBUG_ENABLE
TLV5620_Init();
DacDebug_Init(...);
#endif
```

设计要求：

1. 初始化 ePWM 前冻结 ePWM 时基。
2. `Board_Init()` 只完成配置，不允许功率级开始开关。
3. ePWM 初始化完成后明确调用 `PWM_Disable()`。
4. `Board_Init()` 不执行 `EINT`、`ERTM`。
5. `Board_Init()` 不执行 `EPWM_TimeBase_Start()` 和 `Timer1_Start()`。
6. 不要让 `Board_Init()` 依赖完整的 `BoostController`，优先仅传入真正需要的参数，例如 `pwm_tbprd`。
7. 如果 ADC 初始化必须依赖 ePWM SOC 配置，应保留正确的先后顺序。
8. 如果当前 `Timer1_Init()` 内部已经保持 Timer 停止，则删除顶层重复的 `Timer1_Freeze()`；否则保留必要的冻结操作并说明原因。
9. 保持现有 DAC 调试参数及条件编译逻辑不变，除非发现明确错误。

---

### 4.5 `BoostApp_Init(BoostController *p_boost)`

统一组织应用初始化，建议结构如下：

```c
static void BoostApp_Init(BoostController *p_boost)
{
    MCU_Init();
    Interrupt_Init();
    BoostControl_Init(p_boost);
    Board_Init(Get_TBPRD(p_boost));
}
```

要求：

- 因为 ePWM 初始化需要 `TBPRD`，控制器参数应先于 `Board_Init()` 初始化；
- 若实际工程中 `Get_TBPRD()` 不适合在此处调用，应根据真实代码调整，但要保持依赖关系明确；
- 初始化结束时，PWM 功率输出必须仍处于禁止状态；
- 初始化结束时，全局中断必须仍处于关闭状态。

---

### 4.6 `BoostScheduler_Start()`

负责启动控制程序的时间基准和中断系统，但不直接允许功率管输出。

建议包含：

```c
PWM_Disable();

IFR = 0x0000;

EALLOW;
PieCtrlRegs.PIEACK.all = 0xFFFF;
EDIS;

EPWM_TimeBase_Start();
Timer1_Start();

EINT;
ERTM;
```

要求：

1. 在启动时间基准前再次确认 PWM 输出处于禁止状态。
2. 启动 ePWM 时基不等于允许功率级输出。
3. `EINT` 和 `ERTM` 必须放在所有外设初始化完成之后。
4. 检查 ADC/ePWM 中断标志是否需要在启动前清除。
5. 各外设自身的中断标志优先由对应初始化函数清除。
6. 不要通过简单清零掩盖初始化顺序问题。
7. 若 Timer1 不是必须立即启动，应根据现有状态机逻辑判断，但不要擅自改变当前行为。

---

### 4.7 `BoostApp_BackgroundTask(BoostController *p_boost)`

负责低优先级、非确定周期的后台任务，例如：

- DAC SPI 数据发送；
- USART 上位机通信；
- 参数读取；
- 调试数据整理；
- LED 非实时刷新；
- 非实时诊断。

当前至少保留：

```c
#if DAC_DEBUG_ENABLE
DacDebug_Service(&DAC_Debug_1);
#endif
```

要求：

- 不要将电流环、电压环、SMC、PWM 更新或快速保护放入 `while (1)`；
- 不要在后台任务中执行具有严格实时周期要求的算法；
- 若参数暂未使用，使用 `(void)p_boost;` 避免编译警告；
- 不要加入阻塞式延时；
- 不要加入可能长期阻塞的串口发送。

---

## 5. 功率级使能接口

建议增加统一的功率级控制接口：

```c
void BoostPowerStage_Enable(void);
void BoostPowerStage_Disable(void);
```

参考职责：

```c
void BoostPowerStage_Enable(void)
{
    PWM_Enable();
}

void BoostPowerStage_Disable(void)
{
    PWM_Disable();
}
```

实际实现前必须检查：

- `PWM_Enable()` 是否会清除 Trip Zone；
- `PWM_Disable()` 是否通过 Trip Zone、AQ 强制或 GPIO 方式关闭输出；
- 同步整流上下管是否都能进入安全状态；
- 是否可能出现上下管直通；
- 禁止 PWM 后比较寄存器是否仍可能在下一周期产生脉冲；
- 故障锁存是否应该由普通使能函数自动清除。

安全要求：

1. 初始化流程中不得调用 `BoostPowerStage_Enable()`。
2. 只有状态机确认采样、电压、故障和软启动条件满足后，才允许使能功率级。
3. 故障路径必须能够直接调用 `BoostPowerStage_Disable()`。
4. 不要让多个模块直接散乱调用底层 `PWM_Enable()`、`PWM_Disable()`。
5. 如果当前工程尚无状态机，本次只建立接口，不要未经要求设计复杂状态机。

---

## 6. 实时任务分层

重构时保持以下运行层级。

### 6.1 高速控制任务

高速控制任务应位于 ADC 或 ePWM ISR 中，运行频率与现有控制设计一致，当前开关频率为 160 kHz。

典型内容包括：

```c
ADC_ResultUpdate(p_boost);
BoostFastProtection_Check(p_boost);
BoostControl_Run(p_boost);
PWM_Update(p_boost);
```

要求：

- 保持现有 ISR 入口和中断确认流程；
- 不要改变控制算法数学逻辑；
- 不要改变 PI、SMC、开环或双环的执行顺序；
- 不要把浮点计算无依据地改成定点；
- 不要在 ISR 中加入 SPI 阻塞发送、串口打印或动态内存分配；
- 若电压环采用分频运行，保持原有分频方式；
- 若发现 ISR 超时风险，只进行分析和说明，不在本次任务中大规模优化算法。

### 6.2 慢速监督任务

Timer1 可用于：

- 状态机；
- 软启动；
- 慢速保护；
- MPPT；
- LED 状态；
- 参数检查。

不要在不了解现有 Timer1 ISR 的情况下重新分配任务。

### 6.3 后台任务

`while (1)` 只执行不要求固定周期的任务。

---

## 7. 命名规范

优先使用以下名称：

```c
BoostApp_Init()
BoostScheduler_Start()
BoostApp_BackgroundTask()

MCU_Init()
Interrupt_Init()
Board_Init()
BoostControl_Init()

BoostPowerStage_Enable()
BoostPowerStage_Disable()
```

规范要求：

- 文件内私有函数使用 `static`；
- 公共接口才放入头文件；
- 使用 `void main(void)`，不要继续使用 `void main()`；
- 指针参数使用清晰名称，例如 `p_boost`；
- 常量使用宏或已有配置项，不要在多个位置散布 `150.0f`、`5000.0f` 等魔法数；
- 保持工程现有代码风格，不因“现代化”进行大规模格式重排；
- 不要修改与本任务无关的变量名、注释和文件结构。

建议配置宏：

```c
#define CPU_FREQ_MHZ               150.0f
#define SUPERVISORY_PERIOD_US      5000.0f
```

若工程中已有等价宏，直接复用，不要重复定义。

---

## 8. 明确禁止的修改

本次重构中禁止：

1. 重写 Boost 控制算法。
2. 修改 PI、SMC、MPPT 参数。
3. 改变 ADC 标定公式。
4. 改变 PWM 极性、死区、频率或同步方式。
5. 改变 ISR 触发源和中断优先级。
6. 随意修改结构体布局。
7. 为追求“模块化”创建大量 `.c/.h` 文件。
8. 引入动态内存分配。
9. 引入 C++ 写法。
10. 引入与 F28335 工程不兼容的标准库功能。
11. 删除现有条件编译宏。
12. 删除看似未使用但可能供中断或调试访问的全局对象。
13. 在未确认硬件安全逻辑前自动清除故障锁存。
14. 在初始化结束后自动使能 PWM 功率输出。
15. 对无关代码进行格式化或重命名。
16. 修改链接文件、编译选项和工程配置，除非重构无法编译且有明确依据。

---

## 9. 推荐实施步骤

请按以下顺序实施：

### 第一步：代码审查

先输出简短审查结果，说明：

- 当前 `main()` 的初始化流程；
- 各初始化函数的真实行为；
- ePWM 时基启动和 PWM 输出使能是否已经分离；
- Timer1 初始化后是否默认停止；
- 当前 ISR 和后台任务的职责；
- 本次预计修改哪些文件。

不要在完成审查前直接重构。

### 第二步：最小化顶层重构

优先只修改 `main.c`：

- 增加顶层 `static` 函数；
- 调整调用顺序；
- 简化 `main()`；
- 保持所有底层接口不变；
- 保持现有控制行为不变。

### 第三步：编译检查

检查：

- 函数声明和定义顺序；
- 头文件是否完整；
- `memcpy()` 是否已有正确声明；
- `Uint16`、`uint16_t` 类型转换是否符合原工程；
- 条件编译路径是否均可通过；
- 是否出现未使用参数或隐式函数声明；
- 是否出现重复符号；
- 是否意外改变全局变量可见性。

### 第四步：安全检查

确认：

- 初始化过程中 PWM 始终关闭；
- 启动 ePWM 时基不会自动放开引脚输出；
- 全局中断只在全部配置完成后开启；
- 故障状态不会被初始化流程错误清除；
- 上下管不会因默认比较值产生异常脉冲。

### 第五步：结果说明

完成修改后给出：

1. 修改的文件。
2. 新增的函数。
3. 删除或合并的重复初始化。
4. 初始化顺序的变化。
5. 是否新增或修改变量。
6. PWM 安全状态是否保持。
7. 编译或静态检查结果。
8. 仍需硬件验证的项目。

---

## 10. 验收标准

重构结果必须满足以下条件：

- `main()` 只保留应用初始化、调度启动和后台循环。
- 系统初始化、中断初始化、控制器初始化和板级初始化职责明确。
- 不改变现有控制算法行为。
- 不改变 PWM 参数、ADC 配置和中断周期。
- ePWM 时基启动与功率级使能明确分离。
- 初始化完成后 PWM 功率输出仍然禁止。
- `EINT`、`ERTM` 在所有外设初始化完成后执行。
- DAC 调试仍通过 `DAC_DEBUG_ENABLE` 控制。
- DAC SPI 服务仍位于后台循环。
- 不新增不必要文件。
- 不产生明显的重复初始化。
- 所有新增函数都有简洁、准确的注释。
- 编译无新增警告和错误。
- 修改范围仅限本次顶层重构所需内容。

---

## 11. 目标代码框架

以下代码仅表示目标结构，必须根据工程真实接口调整：

```c
static BoostController boost_converter;

static void MCU_Init(void);
static void Interrupt_Init(void);
static void BoostControl_Init(BoostController *p_boost);
static void Board_Init(Uint16 pwm_tbprd);
static void BoostApp_Init(BoostController *p_boost);
static void BoostScheduler_Start(void);
static void BoostApp_BackgroundTask(BoostController *p_boost);

BoostController *GetBoostHandle(void)
{
    return &boost_converter;
}

void main(void)
{
    BoostApp_Init(&boost_converter);
    BoostScheduler_Start();

    for (;;)
    {
        BoostApp_BackgroundTask(&boost_converter);
    }
}

static void BoostApp_Init(BoostController *p_boost)
{
    MCU_Init();
    Interrupt_Init();
    BoostControl_Init(p_boost);
    Board_Init(Get_TBPRD(p_boost));
}

static void MCU_Init(void)
{
    DINT;
    InitSysCtrl();

#ifdef _FLASHOK
    memcpy((uint16_t *)&RamfuncsRunStart,
           (uint16_t *)&RamfuncsLoadStart,
           (unsigned long)&RamfuncsLoadSize);
    InitFlash();
#endif
}

static void Interrupt_Init(void)
{
    InitPieCtrl();

    IER = 0x0000;
    IFR = 0x0000;

    InitPieVectTable();

    EALLOW;
    PieCtrlRegs.PIEACK.all = 0xFFFF;
    EDIS;
}

static void BoostControl_Init(BoostController *p_boost)
{
    boost_converter_Init(p_boost);
    Control_Init(p_boost);
}

static void Board_Init(Uint16 pwm_tbprd)
{
    EPWM_TimeBase_Freeze();

    LED_Init();
    Timer1_Init(CPU_FREQ_MHZ, SUPERVISORY_PERIOD_US);
    EPWM1_Init(pwm_tbprd);
    PWM_Disable();
    ADC_Init();

#if DAC_DEBUG_ENABLE
    TLV5620_Init();
    DacDebug_Init(&DAC_Debug_1, 0.0f, 0.7f, 20U, 0U, 0U);
#endif
}

static void BoostScheduler_Start(void)
{
    PWM_Disable();

    IFR = 0x0000;

    EALLOW;
    PieCtrlRegs.PIEACK.all = 0xFFFF;
    EDIS;

    EPWM_TimeBase_Start();
    Timer1_Start();

    EINT;
    ERTM;
}

static void BoostApp_BackgroundTask(BoostController *p_boost)
{
    (void)p_boost;

#if DAC_DEBUG_ENABLE
    DacDebug_Service(&DAC_Debug_1);
#endif
}
```

---

## 12. 给 Codex 的最终要求

请基于当前仓库实际代码完成重构，而不是机械复制示例。

开始修改前，先说明你的代码理解和计划修改范围。修改时坚持最小改动原则，不重构无关模块。完成后给出清晰的修改摘要，并明确指出所有新增或修改的变量。

如果示例结构与现有工程存在冲突，以现有工程的正确运行逻辑和硬件安全为优先，并说明为何偏离示例。

不得在没有明确依据的情况下开启 PWM、清除硬件故障锁存或改变控制算法。
