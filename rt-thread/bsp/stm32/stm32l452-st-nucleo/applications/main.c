/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2023-12-03     Meco Man     support nano version
 */

#include <board.h>
#include <rtthread.h>
#include <drv_gpio.h>
#ifndef RT_USING_NANO
#include <rtdevice.h>
#endif /* RT_USING_NANO */

/* defined the LED2 pin: PA5 */
#define LED2_PIN    GET_PIN(A, 5)

extern void spwm_init(void);

int main(void)
{
    rt_pin_mode(LED2_PIN, PIN_MODE_OUTPUT);
    rt_kprintf("hello world! RT-Thread v5.3.0 on STM32L452 Nucleo, COM7 OK.\n");

    spwm_init();
    rt_kprintf("Type 'midiplay' to convert /music.mid and play, or 'builtin' for demo score.\n");

    while (1)
    {
        rt_pin_write(LED2_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED2_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}
