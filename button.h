/*------------------------------------------------------------------------------
Name:       button.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       23-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Button decoding routines
------------------------------------------------------------------------------*/

#ifndef BUTTON_H
#define BUTTON_H

#include "timer.h"

// Total number of buttons handled by these routines

#define NUM_BUTTONS             8

// "Short" button press (debounce) delay

#define BUTTON_SHORT_DELAY      MS_TO_TICKS(50)

// "Long" button press delay

#define BUTTON_LONG_DELAY       MS_TO_TICKS(1000)

// Amount of time button pattern must be stable to qualify as a "chord"

#define BUTTON_CHORD_DELAY      MS_TO_TICKS(750)

//------------------------------------------------------------------------------

// Button status bitmap

typedef union {
    uint8_t all;
    struct {
        uint8_t button0     : 1;
        uint8_t button1     : 1;
        uint8_t button2     : 1;
        uint8_t button3     : 1;
        uint8_t button4     : 1;
        uint8_t button5     : 1;
        uint8_t left_button : 1;
        uint8_t right_button: 1;
    };
} button_t;

//------------------------------------------------------------------------------

// Public/exported functions

// Reset button scanner, clear all button status registers

void reset_buttons(void);

// Enable or disable button scanning

void button_enable(uint8_t enable);
 
// Read un-debounced button status

button_t read_button_state(void);

// Read debounced button status

button_t read_button_debounced(void);

// Read button pattern held longer than BUTTON_CHORD_DELAY

button_t read_button_chord(void);
button_t reset_button_chord(void);

// Read latched state of buttons held for longer than BUTTON_SHORT_DELAY

button_t read_buttons_pressed(void);

// Read and reset latched state of buttons held for longer than
// BUTTON_SHORT_DELAY

button_t reset_buttons_pressed(void);

// Read latched state of buttons released after being held for longer than
// BUTTON_SHORT_DELAY

button_t read_buttons_released(void);

// Read and reset latched state of buttons released after being held for longer
// than BUTTON_SHORT_DELAY

button_t reset_buttons_released(void);

// Read latched state of buttons held for at least BUTTON_SHORT_DELAY but
// released before being held for longer than BUTTON_LONG_DELAY

button_t read_short_buttons(void);

// Read and reset latched state of buttons held for at least BUTTON_SHORT_DELAY
// but released before being held for longer than BUTTON_LONG_DELAY

button_t reset_short_buttons(void);

// Read latched state of buttons held for longer than BUTTON_LONG_DELAY

button_t read_long_buttons(void);

// Read and reset latched state of buttons held for longer than
// BUTTON_LONG_DELAY

button_t reset_long_buttons(void);

// Button scanning routine, updates all button status and latch registers,
// typically called from a periodic timer interrupt

void button_scan(void);

#endif
