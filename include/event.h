/*------------------------------------------------------------------------------
Name:       event.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       24-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Event queue management

Note:       All public functions in this library are thread-safe
------------------------------------------------------------------------------*/

#ifndef EVENT_H
#define EVENT_H

// Size of event queue (number of pending events it can hold)

#define EVENT_QUEUE_SIZE    16

//------------------------------------------------------------------------------

// Event type masks

#define EM_PRESSED          (1 << 0)
#define EM_RELEASE          (1 << 1)
#define EM_SHORT            (1 << 2)
#define EM_LONG             (1 << 3)
#define EM_LEFTR            (1 << 4)
#define EM_RIGHTR           (1 << 5)
#define EM_CHORD            (1 << 6)
#define EM_TIMER            (1 << 7)

// Event types
//
// Note: The scan_for_events() function assumes a specific ordering of event
// types, at least for the single-button events.  Do not change the order in
// which event types are declared here without first checking and modifying
// the scan_for_events() function.

typedef enum {
    NO_EVENT,

    BUTTON0_PRESSED,
    BUTTON0_RELEASED,
    BUTTON0_SHORT,
    BUTTON0_LONG,
    BUTTON1_PRESSED,
    BUTTON1_RELEASED,
    BUTTON1_SHORT,
    BUTTON1_LONG,
    BUTTON2_PRESSED,
    BUTTON2_RELEASED,
    BUTTON2_SHORT,
    BUTTON2_LONG,
    BUTTON3_PRESSED,
    BUTTON3_RELEASED,
    BUTTON3_SHORT,
    BUTTON3_LONG,
    BUTTON4_PRESSED,
    BUTTON4_RELEASED,
    BUTTON4_SHORT,
    BUTTON4_LONG,
    BUTTON5_PRESSED,
    BUTTON5_RELEASED,
    BUTTON5_SHORT,
    BUTTON5_LONG,
    RIGHT_BUTTON_PRESSED,
    RIGHT_BUTTON_RELEASED,
    RIGHT_BUTTON_SHORT,
    RIGHT_BUTTON_LONG,
    LEFT_BUTTON_PRESSED,
    LEFT_BUTTON_RELEASED,
    LEFT_BUTTON_SHORT,
    LEFT_BUTTON_LONG,

    LAST_BUTTON_EVENT,      // Not an event, marks position in list

    BUTTON_CHORD,

    RIGHT_ROTARY_MOVED,
    LEFT_ROTARY_MOVED,

    TIMER_EXPIRED,
    ONE_SECOND_ELAPSED
} event_id;

// Event record

typedef struct {
    event_id event;
    union {
        uint8_t data;
        int8_t signed_data;
    };
} event_t;

//------------------------------------------------------------------------------

// Clear all pending events

void clear_events(void);

// Add a new event to the event queue

void add_event(event_id event, uint8_t data);

// Remove/return an event from the event queue

event_t get_next_event(uint8_t mask);

// Remove/return an event from the event queue.
// Wait for a new event to occur if the queue is empty (blocking call)

event_t wait_next_event(uint8_t mask);

// Return next event in event queue, but do not remove it

event_t unget_next_event(void);

// Event identification functions

uint8_t is_button_pressed_event(event_id event);
uint8_t is_button_released_event(event_id event);
uint8_t is_button_short_event(event_id event);
uint8_t is_button_long_event(event_id event);
uint8_t is_button_chord_event(event_id event);
uint8_t is_button_event(event_id event);
uint8_t is_left_rotary_event(event_id event);
uint8_t is_right_rotary_event(event_id event);
uint8_t is_rotary_event(event_id event);
uint8_t is_timer_event(event_id event);

#endif  // EVENT_H
