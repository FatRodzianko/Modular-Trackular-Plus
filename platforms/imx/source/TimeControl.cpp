#include "TimeControl.h"
#include "clock_config.h"

#include "interruptsConfig.h"

volatile uint32_t msTicks;
volatile uint32_t cyclesTicks;
uint32_t syncTicks;

#define NEW_USEC_CALCULATION 1

void TimeControl::init()
{
    SysTick_Config(SystemCoreClock / 1000);

#if NEW_USEC_CALCULATION
    __NVIC_SetPriority(SysTick_IRQn, kTimeControlPriorityLevel);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cyclesTicks = DWT->CYCCNT;
    cpuCyclesInOneMs = 0xFFFFFFFF / (uint32_t)(SystemCoreClock / 1000000);
#endif
    cpuCyclesInOneUs = SystemCoreClock / 1000000;
}

uint32_t TimeControl::micros()
{
#if NEW_USEC_CALCULATION
    uint32_t smc, scc;
    do
    {
        __LDREXW(&syncTicks);
        smc = msTicks;
        scc = cyclesTicks;
    } while (__STREXW(1, &syncTicks));
    uint32_t cyccnt = DWT->CYCCNT;
    asm volatile("" : : : "memory");
    uint32_t ccdelta = cyccnt - scc;
    uint32_t frac = ((uint64_t)ccdelta * cpuCyclesInOneMs) >> 32;
    if (frac > 1000)
    {
        frac = 1000;
    }
    uint32_t usec = 1000 * smc + frac;
    return usec;
#else
    uint32_t count, current, istatus;

    __disable_irq();
    current = SysTick->VAL;
    count = msTicks;
    istatus = SCB->ICSR;
    __enable_irq();

    if ((istatus & SCB_ICSR_PENDSTSET_Msk) && current > 50)
    {
        count++;
    }
    current = ((SystemCoreClock / 1000) - 1) - current;

    return count * 1000 + current / (SystemCoreClock / 1000000);
#endif
}

uint32_t TimeControl::millis()
{
    volatile uint32_t ret = msTicks;
    return ret;
}

void TimeControl::delayMicroseconds(uint32_t usec)
{
    volatile uint32_t n = usec * 528;

    if (n == 0)
    {
        return;
    }

    __asm__ volatile("L_%=_delayMicroseconds:"
                     "\n\t"
                     "subs   %0, #1"
                     "\n\t"
                     "bne    L_%=_delayMicroseconds"
                     "\n"
                     : "+r"(n)
                     :);
}

void TimeControl::delay(uint32_t ms)
{
    uint32_t start = micros();

    if (ms > 0)
    {
        while (1)
        {
            while ((micros() - start) >= 1000)
            {
                ms--;
                if (ms == 0)
                {
                    return;
                }
                start += 1000;
            }
        }
    }
}

uint32_t TimeControl::clockCycles()
{
    return DWT->CYCCNT;
}

uint32_t millis()
{
    return timeControl.millis();
}

uint32_t micros()
{
    return timeControl.micros();
}

void delay(uint32_t valueMs)
{
    timeControl.delay(valueMs);
}

void delayMicroseconds(uint32_t valueUs)
{
    timeControl.delayMicroseconds(valueUs);
}

#ifdef MT_LOCKUP_RECORDER // MT: diagnostic builds: core/mods/lockupRecorder.cpp's side of the chip,
                          // and handlers that replace startup's, which spin for ever
#include "MIMXRT1051.h"

extern "C"
{
    void mtLockupFault(uint32_t kind, uint32_t* frame);

    uint32_t mtLockupCycles()
    {
        return DWT->CYCCNT;
    }

    uint32_t mtLockupAudioActive()
    {
        return NVIC_GetActive(DMA0_DMA16_IRQn);
    }

    uint32_t mtLockupAudioPending()
    {
        return NVIC_GetPendingIRQ(DMA0_DMA16_IRQn);
    }

    // A bus access with SNVS's clocks gated would fault, here of all places, so they're forced on.
    static inline void snvsClocksOn()
    {
        CCM->CCGR5 |= CCM_CCGR5_CG14_MASK | CCM_CCGR5_CG15_MASK;
    }

    uint32_t mtLockupSavedRead(uint32_t index)
    {
        snvsClocksOn();
        return SNVS->LPGPR[index];
    }

    void mtLockupSavedWrite(uint32_t index, uint32_t value)
    {
        snvsClocksOn();
        SNVS->LPGPR[index] = value;
    }

    void mtLockupFaultRegisters(uint32_t* out)
    {
        out[0] = SCB->CFSR;
        out[1] = SCB->HFSR;
        out[2] = SCB->BFAR;
        out[3] = SCB->MMFAR;
    }

    void mtLockupCleanAndReset()
    {
        SCB_CleanDCache();
        __NVIC_SystemReset();
        for (;;)
        {
            __asm volatile("nop");
        }
    }
}

#define MT_FAULT_HANDLER(name, kind)                                                                                   \
    extern "C" __attribute__((naked)) void name(void)                                                                  \
    {                                                                                                                  \
        __asm volatile("tst lr, #4\n"                                                                                  \
                       "ite eq\n"                                                                                      \
                       "mrseq r1, msp\n"                                                                               \
                       "mrsne r1, psp\n"                                                                               \
                       "movs r0, #" #kind "\n"                                                                         \
                       "b mtLockupFault\n");                                                                           \
    }

MT_FAULT_HANDLER(HardFault_Handler, 2)
MT_FAULT_HANDLER(MemManage_Handler, 3)
MT_FAULT_HANDLER(BusFault_Handler, 4)
MT_FAULT_HANDLER(UsageFault_Handler, 5)

extern "C"
{
    void mtLockupTick(uint32_t* frame);

    void timeControlTick(uint32_t* frame)
    {
        msTicks++;
        cyclesTicks = DWT->CYCCNT;
        mtLockupTick(frame);
    }

    __attribute__((naked)) void SysTick_Handler()
    {
        __asm volatile("mov r0, sp\n"
                       "b timeControlTick\n");
    }
}
#else
extern "C"
{
    void SysTick_Handler()
    {
        msTicks++;
        cyclesTicks = DWT->CYCCNT;
    }
}
#endif
