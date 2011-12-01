/*------------------------------------------------------------------------------
Name:       delay.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       23-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Basic software delay routines
------------------------------------------------------------------------------*/

#include <inttypes.h>

#include "delay.h"

/******************************************************************************
void short_delay(cyc4)

Usage:  Delay loop for small accurate delays: 16-bit counter, 4 cycles/loop

Inputs: cyc4        Number of 4-CPU-cycle units to delay (+overhead)

Return: (none)

Note:   Due to call overhead, delay may be a few cycles more than that
        specified by <cyc4>
******************************************************************************/

void short_delay(uint16_t cyc4)
{
    if (cyc4 > 2) {
        cyc4 -= 2;
        __asm__ __volatile__ (
    	    "1: sbiw %0,1" "\n\t"                  
    	    "brne 1b"                               // 4 cycles/loop
    	    : "=w" (cyc4)
    	    : "0" (cyc4)
    	);
    }
}

/******************************************************************************
void delay_us(us)

Usage:  Delay for a minimum of <us> microseconds

Inputs: us          Number of microseconds to delay (approximate)

Return: (none)

Note:   Due to overhead, delay may be a few microseconds more than that
        specified by <us>.
******************************************************************************/

void delay_us(uint16_t us)
{
#if F_CPU == 8000000UL
	short_delay(us);
	short_delay(us);
#elif F_CPU == 12000000UL
	short_delay(us);
	short_delay(us);
	short_delay(us);
#elif F_CPU == 16000000UL
	short_delay(us);
	short_delay(us);
	short_delay(us);
	short_delay(us);
#else
    #warning F_CPU not supported, delay_us() requires modification for accuracy
	short_delay(us);
#endif
}

/******************************************************************************
void delay_ms(ms)

Usage:  Delay for a minimum of <ms> microseconds

Inputs: ms          Number of milliseconds to delay (approximate)

Return: (none)

Note:   Due to overhead, delay may be a few microseconds more than that
        specified by <ms>.
******************************************************************************/

void delay_ms(uint16_t ms)
{
    while (ms) {
        delay_us(998);
        ms--;
    }
}
