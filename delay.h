#ifndef DELAY_H
#define DELAY_H

/*------------------------------------------------------------------------------
Name:       delay.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       23-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168/328

Content:    Basic software delay routines
------------------------------------------------------------------------------*/

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

//------------------------------------------------------------------------------

// Public/exported functions:

// Delay in 4 cycle units

void short_delay(uint16_t cyc4);

// Delay in microseconds

void delay_us(uint16_t us);

// Delay in milliseconds

void delay_ms(uint16_t ms);

#endif  // DELAY_H

