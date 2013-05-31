/*------------------------------------------------------------------------------
Name:       clock.h
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

#ifndef CLOCK_H
#define CLOCK_H

//------------------------------------------------------------------------------

// Time of day - used by get_time() and set_time()

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} time_t;

// Calendar date - used by get_date() and set_date()

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} date_t;

//------------------------------------------------------------------------------

// Public (exported) functions

void hour_24_to_12(uint8_t hour_24, uint8_t *hour_12, uint8_t *am_pm);

void get_time_12(time_t *t, uint8_t *am_pm);
void get_time_24(time_t *t);
void set_time_12(time_t *t, uint8_t am_pm);
void set_time_24(time_t *t);

void get_date(date_t *d);
void set_date(date_t *d);

uint8_t days_in_month(uint8_t month, uint16_t year);

void time_date_init(void);

void clock_run(uint8_t run_flag);

void time_date_update(void);

#endif
