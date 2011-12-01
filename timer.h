/*------------------------------------------------------------------------------
Name:       timer.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       24-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    "Heartbeat" timing ISR, used for RTC, timing functions, and
            nixie display update.  Also includes generic timer functions
------------------------------------------------------------------------------*/

#ifndef TIMER_H
#define TIMER_H

// Number of timer "ticks" in one timer IRQ cycle
// (timer period)

#define TIMER0_PERIOD_TICKS 100

// Timer prescaler selection
// Must be one of: 1, 8, 64, 256, 1024

#define TIMER0_PRESCALER    256

// Timer frequency with selected period and prescaler (Hz)

#define TIMER0_FREQUENCY    (F_CPU / TIMER0_PRESCALER / TIMER0_PERIOD_TICKS)

// Number of general-purpose event timers to allocate

#define NUM_EVENT_TIMERS    8

//------------------------------------------------------------------------------

// Conversion macros

// Convert milliseconds to timer cycles

#define MS_TO_TICKS(x)      ((x) * TIMER0_FREQUENCY / 1000)

// Convert timer cycles to milliseconds

#define TICKS_TO_MS(x)      ((x) * 1000 / TIMER0_FREQUENCY)
 
//------------------------------------------------------------------------------

void timer_init(void);

uint8_t timer_start(uint16_t period, uint8_t recurring);
void timer_stop(uint8_t timer_id);
void timer_restart(uint8_t timer_id, uint16_t period, uint8_t recurring);
void timer_reset(uint8_t timer_id);
uint16_t timer_read(uint8_t timer_id);
uint8_t timer_expired(uint8_t timer_id, uint8_t reset);
uint8_t timer_status(void);

#endif  // TIMER_H

