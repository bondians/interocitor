/*------------------------------------------------------------------------------
Name:       clock.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       24-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Time & date managment functions
------------------------------------------------------------------------------*/

#include <inttypes.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

#include "clock.h"

//------------------------------------------------------------------------------

static time_t time;
static date_t date;

static uint8_t run;

//------------------------------------------------------------------------------

const uint8_t days_month[] PROGMEM =
    {00, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
//       Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec

/******************************************************************************
 *
 ******************************************************************************/

void hour_24_to_12(uint8_t hour_24, uint8_t *hour_12, uint8_t *am_pm)
{
    *am_pm = (hour_24 >= 12);

    *hour_12 = hour_24;
    if (hour_24 == 0) {
        *hour_12 = 12;
    }
    else if (hour_24 > 12) {
        *hour_12 -= 12;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void get_time_12(time_t *t, uint8_t *am_pm)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        *t = time;
    }

    hour_24_to_12(t->hour, &(t->hour), am_pm);
}

/******************************************************************************
 *
 ******************************************************************************/

void get_time_24(time_t *t)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        *t = time;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void set_time_12(time_t *t, uint8_t am_pm)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        time = *t;

        time.hour--;
        if (am_pm) {
            time.hour += 12;
        }
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void set_time_24(time_t *t)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        time = *t;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void get_date(date_t *d)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        *d = date;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void set_date(date_t *d)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        date = *d;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t days_in_month(uint8_t month, uint16_t year)
{
    uint8_t ret;

    ret = pgm_read_byte(&days_month[month]);

    if ((month == 2) &&
        ((year & 0x0003) == 0) &&
        ((year % 100) != 0)) {
        ret++;
    }

    return ret;
}

/******************************************************************************
 *
 ******************************************************************************/

void time_date_init(void)
{
    time_t time;
    date_t date;

    time.hour = 12;
    time.minute = 0;
    time.second = 0;
    set_time_24(&time);

    date.day = 1;
    date.month = 1;
    date.year = 2009;
    set_date(&date);
}    

/******************************************************************************
 *
 ******************************************************************************/

void clock_run(uint8_t run_flag)
{
    run = run_flag;
}

/******************************************************************************
 *
 ******************************************************************************/

void time_date_update(void)
{
    if (!run) {
        return;
    }

    time.second++;
    if (time.second >= 60) {
        time.second = 0;
 
        time.minute++;
        if (time.minute >= 60) {
            time.minute = 0;
 
            time.hour++;
            if (time.hour >= 24) {
                time.hour = 0;
 
                date.day++;
                if (date.day > days_in_month(date.month, date.year)) {
                    date.month++;
                    if (date.month > 12) {
                        date.month = 1;
                        date.year++;
                    }
                }
            }
        }
    }
}
