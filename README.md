# dah_boost_template

基于 TI C2000 **TMS320F28335** 的 Boost 升压变换器数字控制工程模板。工程使用 Code Composer Studio（CCS）开发，围绕 ePWM、ADC、CPU Timer、Trip-Zone、SCI 和外部 DAC 搭建了完整的控制程序框架，适合用于 Boost 功率级的开环调试、闭环控制算法开发和实验平台验证。

> 本项目直接控制功率器件，错误的采样标定、PWM 极性、占空比或保护配置可能损坏硬件。首次运行请断开功率级或禁止 PWM 输出，确认波形和保护逻辑后，再使用限流电源进行低压测试。

## 工程特点

- 面向 TMS320F28335，系统时钟为 150 MHz，使用硬件 FPU32。
- ePWM1 工作在增减计数模式，默认开关频率为 20 kHz，占空比限制为 0.1～0.7。
- ePWM 在计数器谷值和峰值分别触发 ADC，实现每个 PWM 周期两次同步采样和控制计算。
- 支持开环、电流单环、电压电流双环三种控制结构。
- 电流内环和电压外环均提供 PI/滑模控制（SMC）选择，其中 PI 为默认环路算法。
- 开环、电流单环 PI 和电压电流双环 PI 均已在配套实验平台完成验证；SMC 尚未进行硬件验证。
- 提供上电等待、软启动、运行和故障锁定状态机。
- 使用 ePWM Trip-Zone 实现 PWM 强制关断，并预留软件过流、过压、欠压和短路保护。
- 支持 SCI-A 串口调试输出和 TLV5620 外部 DAC 波形观察。
- 提供扰动观察法（P&O）MPPT 算法模块，便于后续接入光伏控制，但当前未接入主控制流程。

## 当前默认配置

当前源码在 `APP/function.c` 的 `Control_Init()` 中配置为：

| 项目 | 默认值 |
| --- | --- |
| 主控制模式 | 电压电流双环 `CTRL_MODE_DUAL_LOOP` |
| 电流内环 | PI `INNER_LOOP_PI` |
| 电压外环 | PI `OUTER_LOOP_PI` |
| 软启动 | 启用 |
| 双环电压参考 | 12 V → 24 V，每 5 ms 增加 0.24 V |
| PWM 频率 | 20 kHz |
| 控制计算频率 | 约 40 kHz |
| 占空比范围 | 0.1～0.7 |
| Timer1 周期 | 5 ms |
| 上电等待时间 | 约 1.5 s |
| SCI-A | 启用，115200 baud |
| TLV5620 DAC 调试 | 关闭 |

启动后 PWM 首先由单次 Trip 事件保持关闭。状态机经过 `Init -> Wait -> Rise` 后才清除 Trip 并启用输出；在当前双环 PI 模式下，电压参考从 12 V 按每 5 ms 增加 0.24 V，最终到达 24 V。

## 软件架构

工程以 `BoostController` 结构体集中保存状态、PWM 参数、ADC 数据和控制器变量，各中断模块通过 `GetBoostHandle()` 获取同一个控制对象。

```text
main.c
  ├─ BoostApp_Init()
  │    ├─ DSP、PIE 和中断初始化
  │    ├─ 控制器及参数初始化
  │    └─ LED、Timer1、ePWM、ADC、SCI、DAC 初始化
  ├─ BoostScheduler_Start()
  │    ├─ 保持 PWM 关闭
  │    ├─ 启动 ePWM 时基和 Timer1
  │    └─ 开启全局中断
  └─ BoostApp_BackgroundTask()
       ├─ SCI 数据格式化与发送
       └─ DAC 数据发送

ADC 中断（PWM 同步）
  └─ 采样处理 -> 控制环计算 -> PWM 比较值更新

Timer1 中断（5 ms）
  └─ Init -> Wait -> Rise -> Run / Err 状态机
```

实时性要求较高的 ADC 中断、控制算法和状态机函数通过 `.TI.ramfunc` 段放到 RAM 中运行；Flash 构建时由启动代码完成复制。

## 主要功能

### 1. PWM 与同步采样

ePWM1 采用中心对齐 PWM：

- `TBCTR = 0` 时产生 SOCA，触发 ADC SEQ1，处理谷值采样；
- `TBCTR = TBPRD` 时产生 SOCB，触发 ADC SEQ2，处理峰值采样；
- 两次 ADC 中断都会执行采样、控制计算和 PWM 更新，因此快速控制路径的调用频率约为 40 kHz；
- ePWM1A/1B 使用硬件死区模块，当前 `DBRED` 和 `DBFED` 均写入 150 个 TBCLK，约为 1 μs；
- TZ1 配置为单次 Trip 输入，触发后 ePWM1A/1B 均被强制拉低。

### 2. ADC 数据处理

ADC 工作在双序列器、同步采样模式，当前控制流程实际使用：

| ADC 结果 | 物理量 | 用途 |
| --- | --- | --- |
| `ADCRESULT0` / `ADCRESULT4` | 电感电流 `IL` | 谷值/峰值电流采样 |
| `ADCRESULT1` / `ADCRESULT5` | 输出电压 `Vout` | 谷值/峰值电压采样 |
| `ADCRESULT2` / `ADCRESULT6` | 输入电压 `Vin` | 谷值/峰值输入电压采样 |
| `ADCRESULT3` / `ADCRESULT7` | 输出电流 `Iout` | 谷值/峰值输出电流采样 |

采样值经过一阶 IIR 滤波和线性标定后转换为物理量。标定关系为：

```text
物理量 = Gain × ADC 输入电压 + Offset
```

四路采样均配置了独立的线性标定参数，具体增益和偏置与传感器及调理电路相关，移植到其他硬件前必须重新校准。

### 3. 控制模式

在 `APP/function.c` 的 `Control_Init()` 中修改 `CtrlMode`、`InnerLoopMode` 和 `OuterLoopMode` 可切换控制策略。

| 控制模式 | 说明 | 软启动目标 |
| --- | --- | --- |
| `CTRL_MODE_OPEN_LOOP` | 不执行反馈控制，直接给定占空比 | `u: 0.1 -> 0.5` |
| `CTRL_MODE_SINGLE_LOOP` | 电流 PI 或 SMC 单环 | `I_Ref: 0.6 A -> 2.0 A` |
| `CTRL_MODE_DUAL_LOOP` | 电压外环生成电流参考，电流内环生成占空比 | `V_Ref: 12 V -> 24 V` |

PI 控制器采用增量式计算并带输出限幅。开环、电流单环 PI 和电压电流双环 PI 已在配套实验平台验证可用。SMC 已实现电流内环和电压外环，但尚未完成硬件验证，其模型参数、输出电容和控制增益必须结合实际硬件重新辨识和整定，不应直接用于功率实验。

### 4. 状态机与保护

主状态机由 CPU Timer1 每 5 ms 调用：

```text
Init -> Wait（约 1.5 s）-> Rise（软启动）-> Run
                                          |
                                          v
                                         Err
```

- `Init`：清除软件故障标志；
- `Wait`：等待外部电源和采样信号稳定；
- `Rise`：复位控制器，从安全初值开始使能 PWM 并执行斜坡；
- `Run`：正常运行；
- `Err`：强制关闭 PWM，并保持故障锁定。

工程包含软件 OCP、OVP、UVP 和输出短路检测函数，但当前 Timer1 中的 `SlowP(p)` 被注释，`ShortOff()` 也未被主流程调用，因此这些软件保护目前不会周期执行。现阶段可直接生效的是 ePWM TZ1 硬件关断；在真实功率级上使用前，必须补全并验证软件保护链路。

### 5. 调试接口

- **SCI-A**：GPIO35 为 TX，GPIO36 为 RX，默认 115200 baud。后台任务按 `Vin,Vout,IL,Iout` 顺序输出四列 CSV 数据。
- **TLV5620 DAC**：SPIA 使用 GPIO54（数据）和 GPIO56（时钟），GPIO26 控制 LOAD；当前默认关闭 DAC 调试。
- **MATLAB 上位机**：仓库根目录的 `BoostSerialMonitor.m` 可接收 SCI-A 四通道数据并实时绘制波形，后续计划独立整理为上位机项目。
- **LED**：GPIO64～68 和 GPIO10～11 用作板载状态指示；当前 Timer1 中断翻转 LED1，SCI 数据发送完成后翻转 LED6。

SCI 和 DAC 功能分别由 `APP/APP_Libraries/sci.h` 中的 `SCI_COMM_ENABLE`、`APP/APP_Libraries/dac.h` 中的 `DAC_DEBUG_ENABLE` 控制。

## 目录结构

```text
.
├─ main.c                         程序入口
├─ APP/
│  ├─ boost_app.c                 应用初始化、调度启动和后台任务
│  ├─ function.c                  控制算法、状态机、采样处理和保护
│  ├─ adc.c / epwm.c              ADC 与 ePWM 配置及中断
│  ├─ timer0.c / timer1.c         CPU 定时器模块
│  ├─ sci.c / dac.c               串口和外部 DAC 调试
│  ├─ mppt.c                      P&O MPPT 算法（尚未接入主流程）
│  ├─ led.c / exit.c              LED 与外部中断模块
│  └─ APP_Libraries/              应用层头文件
├─ DSP2833x_Librsries/            F28335 支持源码、链接文件和 IQmath 库
├─ targetConfigs/                 CCS 目标连接配置
├─ BoostSerialMonitor.m           MATLAB 串口波形显示脚本
├─ .project / .cproject           CCS 工程配置
└─ Debug/                         CCS 生成的构建产物（Git 已忽略）
```

## 开发环境

- Code Composer Studio 12.8.0
- TI C2000 Compiler 22.6.1.LTS
- 目标器件：TMS320F28335
- 输出格式：COFF，FPU32
- 调试连接：XDS100 USB JTAG（工程配置为 XDS100v1）

建议优先使用上述版本打开工程。其他 CCS 或编译器版本可能可以工作，但尚未验证，不建议仅为编译本项目而主动升级工具链。

## 使用方法

### 1. 准备依赖

安装 CCS 及 C2000 编译器，并准备 TI F2833x device-support 头文件。当前 `.cproject` 中包含本机绝对路径，例如：

```text
D:\AAAstudy\DSPF28335_Program\DSP2833x_Libraries\DSP2833x_common\include
D:\AAAstudy\DSPF28335_Program\DSP2833x_Libraries\DSP2833x_headers\include
```

在其他电脑上使用时，需要在 **Project Properties > Build > C2000 Compiler > Include Options** 中改为本机实际路径。不要直接修改 `Debug/` 下的自动生成 makefile。

### 2. 导入 CCS 工程

1. 打开 CCS。
2. 选择 **File > Import > CCS Projects**。
3. 选择本仓库根目录。
4. 导入工程 `dah_boost_template`。
5. 检查目标器件为 TMS320F28335，编译器为 TI C2000 22.6.1.LTS。

### 3. 检查链接配置

当前工程元数据声明的链接文件是 `28335_RAM_lnk.cmd`，但仓库中没有该文件；已有 Debug 构建记录实际使用的是：

```text
DSP2833x_Librsries/F28335.cmd
DSP2833x_Librsries/DSP2833x_Headers_nonBIOS.cmd
```

同时，`APP/APP_Libraries/function.h` 中 `_FLASHOK` 当前为 `1`，程序会复制 `.TI.ramfunc` 并调用 `InitFlash()`，这对应 Flash 部署方式。因此重新生成工程文件或更换电脑后，若链接时报找不到 `28335_RAM_lnk.cmd`，应先在 CCS 中确认部署方案：

- 使用当前 Flash 方案：将链接命令文件设置为仓库内的 `DSP2833x_Librsries/F28335.cmd`，并保留 `DSP2833x_Headers_nonBIOS.cmd`；
- 改为 RAM 调试：需要自行补充匹配的 RAM 链接文件，并将 `_FLASHOK` 设为 `0`。

不要混用 Flash 启动代码和 RAM 链接布局。

### 4. 编译

推荐在 CCS 中选择 **Project > Build Project**。也可在已配置好 CCS 编译环境的命令行中执行：

```powershell
gmake -C Debug all
```

生成文件通常为 `Debug/dah_boost_template.out`。由于 `Debug/` 中的 makefile 由 CCS 自动生成且包含本机路径，工程迁移后应先在 CCS 中重新生成构建文件。

### 5. 下载与调试

1. 连接 XDS100 和目标板，确认 `targetConfigs/TMS320F28335.ccxml` 与实际仿真器一致。
2. 断开或禁止功率级，先下载 `.out` 文件并运行。
3. 检查 ePWM1A/1B 的频率、极性、死区和 Trip-Zone 关断状态。
4. 给 ADC 输入安全的已知信号，核对原始码值、标定结果和串口数据。
5. 确认 PWM 默认保持关闭、TZ1 能可靠关断后，再允许状态机进入 `Rise`。
6. 使用低电压、限流电源逐步复现开环、单电流环 PI 或双环 PI 控制；SMC 应在单独完成参数整定和验证后再接入功率级。

## 调参入口

常用配置集中在 `APP/function.c`：

- `Control_Init()`：控制模式、软启动开关、ADC 标定系数和占空比限幅；
- `PI_Init()`：电压环和电流环 PI 参数；
- `SMC_Init()`：内外环 SMC 参数、外环分频系数和输出电容；
- `RISE_*` 宏：软启动初值、目标值和步长；
- `PWM_Values_Init()`：CPU 频率、PWM 频率和死区时间。

修改控制参数后应重新编译，并从禁用 PWM、低压限流的条件重新验证。不要直接照搬当前 PI/SMC 参数到不同的电感、电容、采样电路或功率等级。

## 已知限制

- SMC 控制尚未经过硬件验证，当前参数不能直接用于功率实验。
- 软件 OCP/OVP/UVP 调度当前未启用，不能替代硬件保护。
- MPPT、Timer0 和外部按键中断模块已提供，但没有接入当前主程序。
- ADC 标定系数、PI/SMC 参数、死区和保护阈值均与具体硬件相关，使用前必须重新确认。
- 工程包含机器相关的 CCS 绝对路径和不一致的链接配置，首次在新环境构建时需要修正。

## 免责声明

本项目主要用于学习、控制算法开发和实验验证，不构成可直接量产的电源固件。使用者应自行完成硬件互锁、故障保护、参数校准、绝缘与电气安全评估，并承担因使用本项目造成的风险。
