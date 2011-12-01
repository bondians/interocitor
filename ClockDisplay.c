/*------------------------------------------------------------------------------
Name:       ClockDisplay.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       29-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Clock display top level: Display time & date
------------------------------------------------------------------------------*/

#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
//#include <avr/eeprom.h>
#include <string.h>
#include <stdio.h>

#include "portdef.h"
#include "delay.h"
#include "serial.h"
#include "nixie.h"
#include "player.h"
#include "button.h"
#include "event.h"
#include "timer.h"
#include "clock.h"

//------------------------------------------------------------------------------

#define MIN_YEAR                2000
#define MAX_YEAR                2099

#define BLINK_LOW_INTENSITY     '1'
#define BLINK_HIGH_INTENSITY    '9'
#define NORMAL_INTENSITY        '9'

//------------------------------------------------------------------------------

extern FILE primary, secondary;

typedef enum {
    MODE_CLOCK_12,
    MODE_CLOCK_24,
    MODE_DATE
} clock_mode_t;

typedef enum {
    REPEAT_OFF,
    REPEAT_ON,
    REPEAT_INHIBIT
} repeat_mode_t;

typedef enum {
    SELECT_NONE = 0,
    SELECT_HOURS = 1,
    SELECT_MONTH = 1,
    SELECT_MINUTES = 2,
    SELECT_DAY = 2,
    SELECT_SECONDS = 3,
    SELECT_YEAR = 3,
    SELECT_SET = 4,
    SELECT_CANCEL = 5
} select_mode_t;

/******************************************************************************
 *
 ******************************************************************************/

uint8_t SetTime(uint8_t mode, time_t *time)
{
    event_t         event;
    select_mode_t   selected;
    uint8_t         blink;
    uint8_t         refresh;
    repeat_mode_t   repeat;
    uint8_t         hi, mi, si;
    uint8_t         hour, am_pm;
    button_t        button;
    uint8_t         blink_timer;
    uint8_t         repeat_timer;

    selected = SELECT_HOURS;
    blink = BLINK_LOW_INTENSITY;
    refresh = 1;
    repeat = REPEAT_OFF;

    // Allocate/init event timers for blinking & button repeat

    blink_timer = timer_start(MS_TO_TICKS(200), 1);
    repeat_timer = timer_start(MS_TO_TICKS(100), 1);

    do {
        // Display refresh
        // Updated when time is changed, or when blink timer expires
        // Blinks (low-high intensity) the current time element being set

        if (refresh) {
            refresh = 0;
            hi = (selected == SELECT_HOURS) ? blink : NORMAL_INTENSITY;
            mi = (selected == SELECT_MINUTES) ? blink : NORMAL_INTENSITY;
            si = (selected == SELECT_SECONDS) ? blink : NORMAL_INTENSITY;
            if (mode == MODE_CLOCK_12) {
                hour_24_to_12(time->hour, &hour, &am_pm);
                am_pm = am_pm ? 'X' : 'x';
            }
            else {
                hour = time->hour;
                am_pm = 'x';
            }
            fprintf_P(&primary, PSTR("\r*%c%2u~.*%c%02u~.*%c%02u*%c%c"),
                                hi, hour,
                                mi, time->minute,
                                si, time->second,
                                hi, am_pm);
        }

        event = wait_next_event(0);

        // Check for blink or autorepeat timer expiration

        if (event.event == TIMER_EXPIRED) {
            if (event.data == blink_timer) {
                blink = (blink == BLINK_LOW_INTENSITY) ? 
                        BLINK_HIGH_INTENSITY : BLINK_LOW_INTENSITY;
                refresh = 1;
            }

            // Auto-repeat button handling
            // Simulates button presses for the time-setting buttons

            else if ((event.data == repeat_timer) &&
                     (repeat == REPEAT_ON)) {
                button = read_button_debounced();
                if (button.button0) {
                    add_event(BUTTON0_PRESSED, 0);
                }
                if (button.button1) {
                    add_event(BUTTON1_PRESSED, 0);
                }
                if (button.button2) {
                    add_event(BUTTON2_PRESSED, 0);
                }
                if (button.button3) {
                    add_event(BUTTON3_PRESSED, 0);
                }
                if (button.button4) {
                    add_event(BUTTON4_PRESSED, 0);
                }
                if (button.button5) {
                    add_event(BUTTON5_PRESSED, 0);
                }
            }
        }

        // Button 0 : Decrement hours

        else if (event.event == BUTTON0_PRESSED) {
            time->hour--;
            if (time->hour & 0x80) {
                time->hour = 23;
            }
            selected = SELECT_HOURS;
            refresh = 1;
        }

        // Button 1 : Increment hours

        else if (event.event == BUTTON1_PRESSED) {
            time->hour++;
            if (time->hour >= 24) {
                time->hour = 0;
            }
            selected = SELECT_HOURS;
            refresh = 1;
        }

        // Button 2 : Decrement minutes

        else if (event.event == BUTTON2_PRESSED) {
            time->minute--;
            if (time->minute & 0x80) {
                time->minute = 59;
            }
            selected = SELECT_MINUTES;
            refresh = 1;
        }

        // Button 3 : Increment minutes

        else if (event.event == BUTTON3_PRESSED) {
            time->minute++;
            if (time->minute >= 60) {
                time->minute = 0;
            }
            selected = SELECT_MINUTES;
            refresh = 1;
        }

        // Button 4 : Decrement seconds

        else if (event.event == BUTTON4_PRESSED) {
            time->second--;
            if (time->second & 0x80) {
                time->second = 59;
            }
            selected = SELECT_SECONDS;
            refresh = 1;
        }

        // Button 5 : Increment seconds

        else if (event.event == BUTTON5_PRESSED) {
            time->second++;
            if (time->second >= 60) {
                time->second = 0;
            }
            selected = SELECT_SECONDS;
            refresh = 1;
        }

        // Button chord functions:
        // Button 0 + 1 : Reset hours (00 or 12am)
        // Button 2 + 3 : Reset minutes
        // Button 4 + 5 : Reset seconds
        // Button 0 + 5 : Reset time to 00:00:00 or 12:00:00am

        else if (event.event == BUTTON_CHORD) {
            if (event.data == 0x03) {       // Button 0+1
                time->hour = 0;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_HOURS;
                refresh = 1;
            }
            else if (event.data == 0x0C) {  // Button 2+3
                time->minute = 0;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_MINUTES;
                refresh = 1;
            }
            else if (event.data == 0x30) {  // Button 4+5
                time->second = 0;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_SECONDS;
                refresh = 1;
            }
            else if (event.data == 0x21) {  // Button 0+5
                time->hour = 0;
                time->minute = 0;
                time->second = 0;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_HOURS;
                refresh = 1;
            }
        }

        // Left rotary movement : Select time element (h,m,s)

        else if (event.event == LEFT_ROTARY_MOVED) {
            selected += event.data;
            if ((selected == SELECT_NONE) || (selected & 0x80)) {
                selected = SELECT_SECONDS;
            }
            else if (selected > SELECT_SECONDS) {
                selected = SELECT_HOURS;
            }
            blink = BLINK_LOW_INTENSITY;
            refresh = 1;
        }

        // Right rotary movement : Change selected time element

        else if (event.event == RIGHT_ROTARY_MOVED) {
            refresh = 1;

            // Inc/dec hours if selected

            if (selected == SELECT_HOURS) {
                time->hour += event.data;
                if (time->hour & 0x80) {
                    time->hour += 24;
                }
                else if (time->hour >= 24) {
                    time->hour -= 24;
                }
            }

            // Inc/dec minutes if selected

            else if (selected == SELECT_MINUTES) {
                time->minute += event.data;
                if (time->minute & 0x80) {
                    time->minute += 60;
                }
                else if (time->minute >= 60) {
                    time->minute -= 60;
                }
            }

            // Inc/dec seconds if selected

            else if (selected == SELECT_SECONDS) {
                time->second += event.data;
                if (time->second & 0x80) {
                    time->second += 60;
                }
                else if (time->second >= 60) {
                    time->second -= 60;
                }
            }
        }

        // Right rotary button : Set time & exit

        else if (event.event == RIGHT_BUTTON_PRESSED) {
            selected = SELECT_SET;
        }

        // Left rotary button : Cancel time setting

        else if (event.event == LEFT_BUTTON_PRESSED) {
            selected = SELECT_CANCEL;
        }

        // Enable repeat mode if any of the 6 digit buttons are
        // pressed for a 'long' time

        else if ((repeat == REPEAT_OFF) &&
                 ((event.event == BUTTON0_LONG) ||
                  (event.event == BUTTON1_LONG) ||
                  (event.event == BUTTON2_LONG) ||
                  (event.event == BUTTON3_LONG) ||
                  (event.event == BUTTON4_LONG) ||
                  (event.event == BUTTON5_LONG))) {
            repeat = REPEAT_ON;
        }

        // Disable repeat mode when any of the 6 digit buttons are released

        else if ((event.event == BUTTON0_RELEASED) ||
                 (event.event == BUTTON1_RELEASED) ||
                 (event.event == BUTTON2_RELEASED) ||
                 (event.event == BUTTON3_RELEASED) ||
                 (event.event == BUTTON4_RELEASED) ||
                 (event.event == BUTTON5_RELEASED)) {
            repeat = REPEAT_OFF;
        }


    } while (selected <= SELECT_SECONDS);

    // Deallocate event timers

    timer_stop(blink_timer);
    timer_stop(repeat_timer);

    return (selected == SELECT_SET);
}

/******************************************************************************
 *
 ******************************************************************************/

#define NO_REFRESH      0
#define DO_REFRESH      1
#define CHANGED_REFRESH 2

uint8_t SetDate(date_t *date)
{
    event_t         event;
    select_mode_t   selected;
    uint8_t         blink;
    uint8_t         refresh;
    repeat_mode_t   repeat;
    uint8_t         mi, di, yi;
    button_t        button;
    uint8_t         blink_timer;
    uint8_t         repeat_timer;

    selected = SELECT_MONTH;
    blink = BLINK_LOW_INTENSITY;
    refresh = DO_REFRESH;
    repeat = REPEAT_OFF;

    // Allocate/init event timers for blinking & button repeat

    blink_timer = timer_start(MS_TO_TICKS(200), 1);
    repeat_timer = timer_start(MS_TO_TICKS(100), 1);

    do {
        // Display refresh
        // Updated when date is changed, or when blink timer expires
        // Blinks (low-high intensity) the current date element being set

        if (refresh) {
            if (refresh == CHANGED_REFRESH) {
                di = days_in_month(date->month, date->year);
                if (date->day > di) {
                    date->day = di;
                }
            }
            refresh = NO_REFRESH;

            mi = (selected == SELECT_MONTH) ? blink : NORMAL_INTENSITY;
            di = (selected == SELECT_DAY) ? blink : NORMAL_INTENSITY;
            yi = (selected == SELECT_YEAR) ? blink : NORMAL_INTENSITY;
            fprintf_P(&primary, PSTR("\r*%c%02u*%c%02u*%c%02u"),
                                mi, date->month,
                                di, date->day,
                                yi, date->year % 100);
        }

        event = wait_next_event(0);

        // Check for blink or autorepeat timer expiration

        if (event.event == TIMER_EXPIRED) {
            if (event.data == blink_timer) {
                blink = (blink == BLINK_LOW_INTENSITY) ? 
                        BLINK_HIGH_INTENSITY : BLINK_LOW_INTENSITY;
                refresh = DO_REFRESH;
            }

            // Auto-repeat button handling
            // Simulates button presses for the date-setting buttons

            else if ((event.data == repeat_timer) &&
                     (repeat == REPEAT_ON)) {
                button = read_button_debounced();
                if (button.button0) {
                    add_event(BUTTON0_PRESSED, 0);
                }
                if (button.button1) {
                    add_event(BUTTON1_PRESSED, 0);
                }
                if (button.button2) {
                    add_event(BUTTON2_PRESSED, 0);
                }
                if (button.button3) {
                    add_event(BUTTON3_PRESSED, 0);
                }
                if (button.button4) {
                    add_event(BUTTON4_PRESSED, 0);
                }
                if (button.button5) {
                    add_event(BUTTON5_PRESSED, 0);
                }
            }
        }

        // Button 0 : Decrement month

        else if (event.event == BUTTON0_PRESSED) {
            date->month--;
            if (date->month < 1) {
                date->month = 12;
            }
            selected = SELECT_MONTH;
            refresh = CHANGED_REFRESH;
        }

        // Button 1 : Increment month

        else if (event.event == BUTTON1_PRESSED) {
            date->month++;
            if (date->month > 12) {
                date->month = 1;
            }
            selected = SELECT_MONTH;
            refresh = CHANGED_REFRESH;
        }

        // Button 2 : Decrement day

        else if (event.event == BUTTON2_PRESSED) {
            date->day--;
            if (date->day < 1) {
                date->day = days_in_month(date->month, date->year);
            }
            selected = SELECT_DAY;
            refresh = CHANGED_REFRESH;
        }

        // Button 3 : Increment day

        else if (event.event == BUTTON3_PRESSED) {
            date->day++;
            if (date->day > days_in_month(date->month, date->year)) {
                date->day = 1;
            }
            selected = SELECT_DAY;
            refresh = CHANGED_REFRESH;
        }

        // Button 4 : Decrement year

        else if (event.event == BUTTON4_PRESSED) {
            date->year--;
            if (date->year < MIN_YEAR) {
                date->year = MAX_YEAR;
            }
            selected = SELECT_YEAR;
            refresh = CHANGED_REFRESH;
        }

        // Button 5 : Increment year

        else if (event.event == BUTTON5_PRESSED) {
            date->year++;
            if (date->year > MAX_YEAR) {
                date->year = MIN_YEAR;
            }
            selected = SELECT_YEAR;
            refresh = CHANGED_REFRESH;
        }

        // Button chord functions:
        // Button 0 + 1 : Reset month
        // Button 2 + 3 : Reset day
        // Button 4 + 5 : Reset year
        // Button 0 + 5 : Reset date to 01/01/2000

        else if (event.event == BUTTON_CHORD) {
            if (event.data == 0x03) {       // Button 0+1
                date->month = 1;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_MONTH;
                refresh = CHANGED_REFRESH;
            }
            else if (event.data == 0x0C) {  // Button 2+3
                date->day = 1;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_DAY;
                refresh = CHANGED_REFRESH;
            }
            else if (event.data == 0x30) {  // Button 4+5
                date->year = MIN_YEAR;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_YEAR;
                refresh = CHANGED_REFRESH;
            }
            else if (event.data == 0x21) {  // Button 0+5
                date->month = 1;
                date->day = 1;
                date->year = MIN_YEAR;
                repeat = REPEAT_INHIBIT;
                selected = SELECT_MONTH;
                refresh = CHANGED_REFRESH;
            }
        }

        // Left rotary movement : Select date element (m,d,y)

        else if (event.event == LEFT_ROTARY_MOVED) {
            selected += event.data;
            if ((selected == SELECT_NONE) || (selected & 0x80)) {
                selected = SELECT_YEAR;
            }
            else if (selected > SELECT_YEAR) {
                selected = SELECT_MONTH;
            }
            blink = BLINK_LOW_INTENSITY;
            refresh = DO_REFRESH;
        }

        // Right rotary movement : Change selected date element

        else if (event.event == RIGHT_ROTARY_MOVED) {
            refresh = CHANGED_REFRESH;

            // Inc/dec month if selected

            if (selected == SELECT_MONTH) {
                date->month += event.data;
                if ((date->month < 1) || (date->month & 0x80)) {
                    date->month += 12;
                }
                else if (date->month > 12) {
                    date->month -= 12;
                }
            }

            // Inc/dec day if selected

            else if (selected == SELECT_DAY) {
                date->day += event.data;
                di = days_in_month(date->month, date->year);
                if ((date->day < 1) || (date->day & 0x80)) {
                    date->day = di;
                }
                else if (date->day > di) {
                    date->day = 1;
                }
            }

            // Inc/dec year if selected

            else if (selected == SELECT_YEAR) {
                date->year += event.data;
                if (date->year < MIN_YEAR) {
                    date->year = MAX_YEAR;
                }
                else if (date->year > MAX_YEAR) {
                    date->year = MIN_YEAR;
                }
            }
        }

        // Right rotary button : Set date & exit

        else if (event.event == RIGHT_BUTTON_PRESSED) {
            selected = SELECT_SET;
        }

        // Left rotary button : Cancel date setting

        else if (event.event == LEFT_BUTTON_PRESSED) {
            selected = SELECT_CANCEL;
        }

        // Enable repeat mode if any of the 6 digit buttons are
        // pressed for a 'long' time

        else if ((repeat == REPEAT_OFF) &&
                 ((event.event == BUTTON0_LONG) ||
                  (event.event == BUTTON1_LONG) ||
                  (event.event == BUTTON2_LONG) ||
                  (event.event == BUTTON3_LONG) ||
                  (event.event == BUTTON4_LONG) ||
                  (event.event == BUTTON5_LONG))) {
            repeat = REPEAT_ON;
        }

        // Disable repeat mode when any of the 6 digit buttons are released

        else if ((event.event == BUTTON0_RELEASED) ||
                 (event.event == BUTTON1_RELEASED) ||
                 (event.event == BUTTON2_RELEASED) ||
                 (event.event == BUTTON3_RELEASED) ||
                 (event.event == BUTTON4_RELEASED) ||
                 (event.event == BUTTON5_RELEASED)) {
            repeat = REPEAT_OFF;
        }


    } while (selected <= SELECT_YEAR);

    // Deallocate event timers

    timer_stop(blink_timer);
    timer_stop(repeat_timer);

    return (selected == SELECT_SET);
}

/******************************************************************************
 *
 ******************************************************************************/

void TerminalMode(void)
{
    event_t event;
    int16_t ch;

    printf_P(PSTR("\r\nTerminal mode ready.\r\n"));

    fprintf_P(&primary, PSTR("\v\f"));
 
    do {
        event = get_next_event(0);
        if (event.event == BUTTON1_PRESSED) {
            break;
        }

        ch = serial_in();

        if (ch >= 0) {
            if (ch == '\e') {
                break;
            }
            serial_out(ch);
            nixie_out(ch, &primary);
        }
    } while (1);

    printf_P(PSTR("\r\nTerminal mode exit\r\n"));
    nixie_out('\v', &primary);
}

/******************************************************************************
 *
 ******************************************************************************/

void ClockDisplay(void)
{
    event_t event;
    time_t time;
    date_t date;
    uint8_t am_pm;
    uint8_t set;
    clock_mode_t display_mode = MODE_CLOCK_24;
    clock_mode_t clock_mode = MODE_CLOCK_24;

//  nixie_out('\f',&primary);
    nixie_out('\f',&secondary);

    nixie_show_stream(&primary);
    nixie_crossfade_rate(1);

    do {
        switch (display_mode) {
            set = 'y';      // For future alarm annunciator
            case MODE_CLOCK_12 :
                get_time_12(&time, &am_pm);
                am_pm = am_pm ? 'X' : 'x';
                fprintf_P(&secondary, PSTR("\r~%2u.%02u.%02u%c%c"),
                    time.hour, time.minute, time.second, am_pm, set);
                break;

            case MODE_CLOCK_24 :
                get_time_24(&time);
                fprintf_P(&secondary, PSTR("\r~x%02u.%02u.%02u%c"),
                    time.hour, time.minute, time.second, set);
                break;

            case MODE_DATE :
                get_date(&date);
                date.year %= 100;
                fprintf_P(&secondary, PSTR("\r~`x%02u%02u%02u%c"),
                    date.month, date.day, date.year, set);
                break;
        };

        nixie_crossfade(&secondary);

        event = wait_next_event(0);

        if (event.event == BUTTON0_PRESSED) {
            display_mode = (display_mode == MODE_DATE) ?
                           clock_mode : MODE_DATE;
        }

        else if (event.event == BUTTON5_PRESSED) {
            if ((display_mode == MODE_CLOCK_12) ||
                (display_mode == MODE_CLOCK_24)) {
                if (clock_mode == MODE_CLOCK_12) {
                    clock_mode = MODE_CLOCK_24;
                    fprintf_P(&secondary, PSTR("\f  24"));
                }
                else {
                    clock_mode = MODE_CLOCK_12;
                    fprintf_P(&secondary, PSTR("\f  12"));
                }
                nixie_crossfade(&secondary);
                delay_ms(500);
            }
            display_mode = clock_mode;
        }

        else if (event.event == BUTTON1_LONG) {
            TerminalMode();
        }

        else if (event.event == RIGHT_BUTTON_LONG) {
            if (display_mode == MODE_DATE) {
                get_date(&date);
                set = SetDate(&date);
                if (set) {
                    set_date(&date);
                }
            }
            else {
                get_time_24(&time);
                set = SetTime(clock_mode, &time);
                if (set) {
                    set_time_24(&time);
                }
            }
        }
    } while (1);
}       
