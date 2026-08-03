/*
 * boost_app.c
 *
 * Top-level initialization, scheduler startup and background tasks.
 */

#include "boost_app.h"
#include "adc.h"
#include "timer1.h"
#include "epwm.h"
#include "led.h"
#include "dac.h"
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"
#include <stdint.h>
#include <string.h>

#define CPU_FREQ_MHZ               150.0f
#define SUPERVISORY_PERIOD_US      5000.0f

extern Uint16 RamfuncsLoadStart;
extern Uint16 RamfuncsLoadSize;
extern Uint16 RamfuncsRunStart;

static BoostController boost_converter;

static void MCU_Init(void);
static void Interrupt_Init(void);
static void BoostControl_Init(BoostController *p_boost);
static void Board_Init(Uint16 pwm_tbprd);

BoostController *GetBoostHandle(void)
{
    return &boost_converter;
}

/**
 * @brief  Initialize the complete Boost application without starting its time bases.
 */
void BoostApp_Init(void)
{
    MCU_Init();
    Interrupt_Init();

    /*
     * Control_Init() currently writes ePWM dead-band registers through
     * PWM_Values_Init(), so freeze the ePWM time base before control setup.
     */
    EPWM_TimeBase_Freeze();
    BoostControl_Init(&boost_converter);
    Board_Init((Uint16)Get_TBPRD(&boost_converter));
}

/**
 * @brief  Start application time bases and enable CPU interrupts.
 * @note   The PWM power output remains disabled by the one-shot trip.
 */
void BoostScheduler_Start(void)
{
    // Keep the power stage disabled while starting the scheduler.
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

/**
 * @brief  Run non-real-time background services.
 */
void BoostApp_BackgroundTask(void)
{
#if DAC_DEBUG_ENABLE
    DacDebug_Service(&DAC_Debug_1);
#endif
}

/**
 * @brief  Initialize the DSP core and copy time-critical functions to RAM.
 */
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

/**
 * @brief  Initialize the CPU and PIE interrupt infrastructure.
 */
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

/**
 * @brief  Initialize the Boost software object and control parameters.
 */
static void BoostControl_Init(BoostController *p_boost)
{
    boost_converter_Init(p_boost);
    Control_Init(p_boost);
}

/**
 * @brief  Configure board peripherals while keeping all time bases stopped.
 */
static void Board_Init(Uint16 pwm_tbprd)
{
    LED_Init();
    Timer1_Init(CPU_FREQ_MHZ, SUPERVISORY_PERIOD_US);
    EPWM1_Init(pwm_tbprd);

    // Disable both ePWM outputs before enabling any interrupt source.
    PWM_Disable();
    ADC_Init();

#if DAC_DEBUG_ENABLE
    TLV5620_Init();
    DacDebug_Init(&DAC_Debug_1, 0.0f, 0.7f, 20U, 0U, 0U);
#endif
}
