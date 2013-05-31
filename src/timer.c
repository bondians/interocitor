/*------------------------------------------------------------------------------
Name:       timer.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       17-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    General purpose timing functions and RTC routines
------------------------------------------------------------------------------*/

#include <inttypes.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

#include "portdef.h"
#include "nixie.h"
#include "button.h"
#include "event.h"
#include "clock.h"
#include "player.h"
#include "timer.h"

#if TIMER0_PRESCALER == 1
  #define TIMER0_PRESCALER_BITS     BM(CS00)
#elif TIMER0_PRESCALER == 8
  #define TIMER0_PRESCALER_BITS     BM(CS01)
#elif TIMER0_PRESCALER == 64
  #define TIMER0_PRESCALER_BITS     BM(CS01) | BM(CS00)
#elif TIMER0_PRESCALER == 256
  #define TIMER0_PRESCALER_BITS     BM(CS02)
#elif TIMER0_PRESCALER == 1024
  #define TIMER0_PRESCALER_BITS     BM(CS02) | BM(CS00)
#else
  #define TIMER0_PRESCALER_BITS     0
  #warning Invalid timer 0 prescaler selected (must be 1, 8, 64, 256 or 1024)
#endif

//------------------------------------------------------------------------------

static uint16_t seconds_prescaler;

static volatile uint16_t timer_count[NUM_EVENT_TIMERS];
static uint16_t timer_period[NUM_EVENT_TIMERS];
static volatile uint8_t timer_flag;

/******************************************************************************
 *
 ******************************************************************************/

void timer_init(void)
{
    TCCR0B = 0;                         // Stop timer during init

    TCCR0A = BM(WGM01);                 // OC0x pins disabled, CTC mode (WGM02..0 = 010)
    TCNT0 = 0;                          // Reset timer counter
    OCR0A = TIMER0_PERIOD_TICKS - 1;    // 625x/sec interval when prescaler = f/256 w/16MHz clock
    OCR0B = 0;                          // Set some known value in OCR0B (not used)
    TIMSK0 = BM(OCIE0A);                // Enable output compare A interrupt
    TIFR0 = BM(OCF0B) | BM(OCF0A) | BM(TOV0); // Clear all timer interrupt flags

    seconds_prescaler = TIMER0_FREQUENCY;

    TCCR0B = TIMER0_PRESCALER_BITS;     // Select f/256 prescaler, start counter
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t timer_start(uint16_t period, uint8_t recurring)
{
    uint8_t timer_id;
    uint16_t count;

    for (timer_id = 0; timer_id < NUM_EVENT_TIMERS; timer_id++) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            count = timer_count[timer_id];
        }
        if (!count && !timer_period[timer_id]) {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
            {
                timer_count[timer_id] = period;
                if (recurring) {
                    timer_period[timer_id] = period;
                }
                timer_flag &= (uint8_t) ~(1 << timer_id);
            }
            return timer_id;
        }
    }

    return -1;
}

/******************************************************************************
 *
 ******************************************************************************/

void timer_stop(uint8_t timer_id)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        timer_count[timer_id] = 0;
        timer_period[timer_id] = 0;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void timer_restart(uint8_t timer_id, uint16_t period, uint8_t recurring)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        timer_count[timer_id] = period;
        if (recurring) {
            timer_period[timer_id] = period;
        }
        timer_flag &= (uint8_t) ~(1 << timer_id);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void timer_reset(uint8_t timer_id)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        timer_count[timer_id] = timer_period[timer_id];
        timer_flag &= (uint8_t) ~(1 << timer_id);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

uint16_t timer_read(uint8_t timer_id)
{
    uint16_t count;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        count = timer_count[timer_id];
    }

    return count;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t timer_expired(uint8_t timer_id, uint8_t reset)
{
    uint8_t flag;

    flag = 1 << timer_id;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        flag &= timer_flag;
        if (reset) {
            timer_flag &= (uint8_t) ~flag;
        }
    }

    if (flag) {
        flag = 1;
    }

    return flag;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t timer_status(void)
{
    return timer_flag;
}

/******************************************************************************
 *
 ******************************************************************************/

static void timer_update(void)
{
    register uint8_t index;
    register uint8_t mask;
    register uint16_t count;

    mask = 0x01;
    for (index = 0; index < NUM_EVENT_TIMERS; index++) {
        count = timer_count[index];
        if (count) {
            count--;
            if (!count) {
                count = timer_period[index];
                timer_flag |= mask;
            }
            timer_count[index] = count;
        }
        mask <<= 1;
    }
}


/******************************************************************************
 *
 ******************************************************************************/

ISR(TIMER0_COMPA_vect, ISR_BLOCK)
{

    // Operations performed every entry

    nixie_display_refresh();
    button_scan();
    timer_update();
    player_service();

    seconds_prescaler--;
    if (!seconds_prescaler) {

        // Operations performed once per second

        seconds_prescaler += TIMER0_FREQUENCY;

        time_date_update();
        add_event(ONE_SECOND_ELAPSED, 0);
    }
}
