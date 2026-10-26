#include <stdint.h>
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

#define DWT_CTRL_REG    (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT_REG  (*(volatile uint32_t *)0xE0001004UL)
#define DWT_CYCCNT_EN   (1UL << 0)

void delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t cycles;

    /* SysTick belongs to FreeRTOS after the scheduler starts.  DWT CYCCNT is
       independent, so it is safe for the short timing used by software I2C. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CTRL_REG |= DWT_CYCCNT_EN;
    start = DWT_CYCCNT_REG;
    cycles = us * (SystemCoreClock / 1000000U);
    while ((uint32_t)(DWT_CYCCNT_REG - start) < cycles)
    {
    }
}

void delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    while (ms--)
    {
        delay_us(1000);
    }
}
