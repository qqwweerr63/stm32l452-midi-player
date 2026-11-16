/*
 * Self-contained TIM1 PWM audio output for Playtune on STM32L452 Nucleo.
 * PA8 = TIM1_CH1.  TIM1 update ISR calls update() once per audio sample.
 * No CubeMX main.c dependency.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "stm32l4xx_hal.h"
#include "playtune.h"

#define FCK_TIM 80000000U   /* TIM1 clock = APB2 = 80 MHz */

static TIM_HandleTypeDef htim1;
extern void update(void);

void TIM1_UP_TIM16_IRQHandler(void)
{
    rt_interrupt_enter();

    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE) != RESET)
    {
        __HAL_TIM_CLEAR_IT(&htim1, TIM_IT_UPDATE);
        update();
    }

    rt_interrupt_leave();
}

void spwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};
    uint32_t period = MAX_OUTPUT_VALUT;   /* ARR = 4096-1 -> 19.53 kHz */

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA8 -> TIM1_CH1 */
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &gpio);

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = period - 1;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
    {
        rt_kprintf("[spwm] TIM1 PWM init failed\n");
        return;
    }

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK)
    {
        rt_kprintf("[spwm] TIM1 channel config failed\n");
        return;
    }

    HAL_NVIC_SetPriority(TIM1_UP_TIM16_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);

    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

    rt_kprintf("[spwm] TIM1 PWM ready, ARR=%d, sample rate=%d Hz\n",
               (int)(period - 1), (int)(FCK_TIM / period));
}

void spwm_setoutput(uint32_t val)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, val);
}

/* Stop/start the audio ISR.  Must be called around on-chip flash writes:
 * on STM32L4 the flash is unavailable for instruction fetch during program/
 * erase, and a 19.5 kHz TIM1 interrupt firing in that window corrupts the
 * write.  Disabling the update interrupt first makes config saves reliable. */
void spwm_audio_isr_disable(void)
{
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
}

void spwm_audio_isr_enable(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}
