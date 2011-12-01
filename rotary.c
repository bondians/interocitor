/*------------------------------------------------------------------------------
Name:       rotary.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       16-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Rotary encoder support
------------------------------------------------------------------------------*/

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "portdef.h"
#include "rotary.h"

//------------------------------------------------------------------------------

typedef union {
    uint8_t all;
    struct {
        uint8_t left_a  : 1;
        uint8_t left_b  : 1;
        uint8_t right_a : 1;
        uint8_t right_b : 1;
        uint8_t unused0 : 1;
        uint8_t unused1 : 1;
        uint8_t unused2 : 1;
        uint8_t unused3 : 1;
    };
} rotary_state_t;

//------------------------------------------------------------------------------

// Local (module) variables

static rotary_state_t previous;

static volatile int8_t left_position;
static volatile uint8_t left_movement;
static volatile int8_t right_position;
static volatile uint8_t right_movement;

/******************************************************************************
 *
 ******************************************************************************/

void rotary_init(void)
{
    // Disable pin interrupts during setup
    // This needs to change if encoder is not wired to Port D

    PCICR &= INVBM(PCIE2);

    previous.all = 0;
    left_position = 0;
    left_movement = 0;
    right_position = 0;
    right_movement = 0;

    // Configure pins associated with rotary encoders as inputs
    // Turn OFF pull-ups for these pins

    DDR(LEFT_A_PORT) &= INVBM(LEFT_A_PIN);
    DDR(LEFT_B_PORT) &= INVBM(LEFT_B_PIN);
    DDR(RIGHT_A_PORT) &= INVBM(RIGHT_A_PIN);
    DDR(RIGHT_B_PORT) &= INVBM(RIGHT_B_PIN);

    BCLR(LEFT_A);
    BCLR(LEFT_B);
    BCLR(RIGHT_A);
    BCLR(RIGHT_B);

    // Enable pin change interrupts for encoder 'B' inputs
    // Note: PCMSK2 is associated with Port D pins.  IF the encoder inputs are
    //       moved to a different port, the assignment below must be done
    //       to the appropriate PCMSKx register.

    PCMSK2 |= BM(LEFT_B_PIN) | BM(RIGHT_B_PIN);

    // Enable pin interrupts on port D
    // Note: This needs to be change if rotary encoder inputs are moved
    //       from port D to some other port

    PCICR |= BM(PCIE2);
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t rotary_status(void)
{
    return previous.all;
}

/******************************************************************************
 *
 ******************************************************************************/

int8_t left_rotary_relative(void)
{
    int8_t ret;

    ret = left_position;
    left_position = 0;

    return ret;
}

/******************************************************************************
 *
 ******************************************************************************/

int8_t left_rotary_absolute(void)
{
    return left_position;
}

/******************************************************************************
 *
 ******************************************************************************/

int8_t right_rotary_relative(void)
{
    uint8_t ret;

    ret = right_position;
    right_position = 0;

    return ret;
}

/******************************************************************************
 *
 ******************************************************************************/

int8_t right_rotary_absolute(void)
{
    return right_position;
}

/******************************************************************************
 * Pin Change (bank 2 - PORTD) ISR
 * Handles pin-change interrupts on Port D
 * Used for rotary encoder movement detection and logging
 ******************************************************************************/

ISR(PCINT2_vect, ISR_BLOCK)
{
    register rotary_state_t present;

    // Read present rotary encoder state
    // Note: if (PINREAD(x)) y = 1; is the most efficient way to do this, it
    // generates the tightest code when avr-gcc is used.

    present.all = 0;

    if (PINREAD(LEFT_A)) {
        present.left_a = 1;
    }
    if (PINREAD(LEFT_B)) {
        present.left_b = 1;
    }
    if (PINREAD(RIGHT_A)) {
        present.right_a = 1;
    }
    if (PINREAD(RIGHT_B)) {
        present.right_b = 1;
    }

    // Check for left encoder movement
    // LEFT_B toggles state every time encoder is moved
    //
    // Note: Must check if LEFT_B has changed state since last entry because
    // both left and right encoder movement will trigger the same interrupt.

    if (present.left_b != previous.left_b) {
        left_movement++;
        if (present.left_b) {
            if (present.left_a) {
                left_position++;    // CW rotation, BA = 11
            }
            else {
                left_position--;    // CCW rotation, BA = 10
            }
        }
        else {
            if (present.left_a) {
                left_position--;    // CCW rotation, BA = 01
            }
            else {
                left_position++;    // CW rotation, BA = 00
            }
        }
    }

    // Check for right encoder movement
    // RIGHT_B changes state every time encoder is moved
    //
    // Note: Must check if RIGHT_B has changed state since last entry because
    // both left and right encoder movement will trigger the same interrupt.

    if (present.right_b != previous.right_b) {
        right_movement++;
        if (present.right_b) {
            if (present.right_a) {
                right_position++;   // CW rotation, BA = 11
            }
            else {
                right_position--;   // CCW rotation, BA = 10
            }
        }
        else {
            if (present.right_a) {
                right_position--;   // CW rotation, BA = 01
            }
            else {
                right_position++;   // CCW rotation, BA = 00
            }
        }
    }

    previous = present;
}
