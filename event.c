/*------------------------------------------------------------------------------
Name:       event.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       24-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Event queue management
------------------------------------------------------------------------------*/

#include <inttypes.h>
#include <avr/io.h>
#include <util/atomic.h>

#include "portdef.h"
#include "timer.h"
#include "rotary.h"
#include "button.h"
#include "timer.h"
#include "event.h"

//------------------------------------------------------------------------------


static volatile uint8_t event_queue_head;
static volatile uint8_t event_queue_tail;
static event_t event_queue[EVENT_QUEUE_SIZE];

/******************************************************************************
 *
 ******************************************************************************/

void clear_events(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        event_queue_head = 0;
        event_queue_tail = 0;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void add_event(event_id event, uint8_t data)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        event_queue[event_queue_head].event = event;
        event_queue[event_queue_head].data = data;

        event_queue_head++;
        if (event_queue_head >= EVENT_QUEUE_SIZE) {
            event_queue_head = 0;
        }

        if (event_queue_head == event_queue_tail) {
            event_queue_tail++;
        }
    }
}

/******************************************************************************
 *
 ******************************************************************************/

static void scan_for_events()
{
    button_t pressed, released, bshort, blong, debounced;
    int8_t index;
    uint8_t mask;
    event_id event;

    pressed = reset_buttons_pressed();
    released = reset_buttons_released();
    bshort = reset_short_buttons();
    blong = reset_long_buttons();
    debounced = read_button_debounced();

    // Scan buttons for events
    // Pressed, released, short and long press

    event = 0;
    mask = 0x01;
    for (index = 0; index < 8; index++) {
        if (pressed.all & mask) {
            add_event(event + BUTTON0_PRESSED, debounced.all);
        }
        if (released.all & mask) {
            add_event(event + BUTTON0_RELEASED, debounced.all);
        }
        if (bshort.all & mask) {
            add_event(event + BUTTON0_SHORT, debounced.all);
        }
        if (blong.all & mask) {
            add_event(event + BUTTON0_LONG, debounced.all);
        }
        event += 4;
        mask <<= 1;
    }

    // Check for button chords

    pressed = reset_button_chord();
    if (pressed.all) {
        add_event(BUTTON_CHORD, pressed.all);
    }

    // Check right rotary encoder

    index = right_rotary_relative();
    if (index) {
        add_event(RIGHT_ROTARY_MOVED, index);
    }

    // Check left rotary encoder

    index = left_rotary_relative();
    if (index) {
        add_event(LEFT_ROTARY_MOVED, index);
    }

    // Check event timers

    mask = timer_status();
    index = 0;
    while (mask) {
        if (mask & 0x01) {
            timer_expired(index, 1);            // Resets timer-expiration flag
            add_event(TIMER_EXPIRED, index);
        }
        index++;
        mask >>= 1;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_pressed_event(event_id event)
{
    return ((event < LAST_BUTTON_EVENT) &&
            !((event - BUTTON0_PRESSED) & 0x03));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_released_event(event_id event)
{
    return ((event < LAST_BUTTON_EVENT) &&
            !((event - BUTTON0_RELEASED) & 0x03));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_short_event(event_id event)
{
    return ((event < LAST_BUTTON_EVENT) &&
            !((event - BUTTON0_SHORT) & 0x03));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_long_event(event_id event)
{
    return ((event < LAST_BUTTON_EVENT) &&
            !((event - BUTTON0_LONG) & 0x03));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_chord_event(event_id event)
{
    return (event == BUTTON_CHORD);
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_button_event(event_id event)
{
    return ((event < LAST_BUTTON_EVENT) || (event == BUTTON_CHORD));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_left_rotary_event(event_id event)
{
    return (event == LEFT_ROTARY_MOVED);
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_right_rotary_event(event_id event)
{
    return (event == RIGHT_ROTARY_MOVED);
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_rotary_event(event_id event)
{
    return ((event == LEFT_ROTARY_MOVED) || (event == RIGHT_ROTARY_MOVED));
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t is_timer_event(event_id event)
{
    return ((event == TIMER_EXPIRED) || (event == ONE_SECOND_ELAPSED));
}

/******************************************************************************
 *
 ******************************************************************************/

event_t get_next_event(uint8_t mask)
{
    event_t event;

    do {
        scan_for_events();

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            if (event_queue_head != event_queue_tail) {
                event = event_queue[event_queue_tail];
 
                event_queue_tail++;
                if (event_queue_tail >= EVENT_QUEUE_SIZE) {
                    event_queue_tail = 0;
                }
            }
            else {
                event.event = NO_EVENT;
                event.data = 0;
            }
        }
    } while (0);

    return event;
}

/******************************************************************************
 *
 ******************************************************************************/

event_t wait_next_event(uint8_t mask)
{
    while (event_queue_head == event_queue_tail) {
        scan_for_events();
    }

    return get_next_event(mask);
}

/******************************************************************************
 *
 ******************************************************************************/

event_t unget_next_event(void)
{
    event_t event;

    scan_for_events();

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (event_queue_head != event_queue_tail) {
            event = event_queue[event_queue_tail];
        }
        else {
            event.event = NO_EVENT;
            event.data = 0;
        }
    }

    return event;
}

