#ifndef PORTDEF_H
#define PORTDEF_H

/*------------------------------------------------------------------------------
Name:       portdef.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       16-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Port and resource definitions for NixieClock
------------------------------------------------------------------------------*/

// Obligatory for a Deepbondi project

#define PICKLES            PSTR("Jalapeno Peppers")

// Useful macros for port manipulation

// Address of data direction register of port x

#define DDR(x)              (*(&x - 1))

// Address of input register of port x

#define PIN(x)              (*(&x - 2))

// Create bitmask from port pin # (or register bit #)

#define BM(x)               (1 << (x))

// Create an inverted bitmask from a port pin # (or register bit #)

#define INVBM(x)            (uint8_t) ~(1 << (x))

// Set (bring high) a output pin (or turn on pull-up for inputs)

#define BSET(x)             x ## _PORT |= (uint8_t) (1 << x ## _PIN)

// Clear (bring low) a output pin (or turn off pull-up for inputs)

#define BCLR(x)             x ## _PORT &= (uint8_t) ~(1 << x ## _PIN)

// Read the status of a input pin

#define PINREAD(x)          (PIN(x ## _PORT) & (1 << x ## _PIN))

//------------------------------------------------------------------------------

// Misc. configuration

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// Serial (RS232)

#define RXD_PORT            PORTD
#define RXD_PIN             0
#define TXD_PORT            PORTD
#define TXD_PIN             1

// Nixie driver (SPI)

#define DRIVER_DATA_PORT    PORTB
#define DRIVER_DATA_PIN     3
#define DRIVER_CLOCK_PORT   PORTB
#define DRIVER_CLOCK_PIN    5
#define DRIVER_LATCH_PORT   PORTB
#define DRIVER_LATCH_PIN    2
#define DRIVER_ENABLE_PORT  PORTC
#define DRIVER_ENABLE_PIN   0

// Rotary encoders

#define LEFT_A_PORT         PORTD
#define LEFT_A_PIN          4
#define LEFT_B_PORT         PORTD
#define LEFT_B_PIN          5
#define LEFT_BUTTON_PORT    PORTD
#define LEFT_BUTTON_PIN     2

#define RIGHT_A_PORT        PORTD
#define RIGHT_A_PIN         6
#define RIGHT_B_PORT        PORTD
#define RIGHT_B_PIN         7
#define RIGHT_BUTTON_PORT   PORTD
#define RIGHT_BUTTON_PIN    3

// Buttons

#define BUTTON0_PORT        PORTB
#define BUTTON0_PIN         4
#define BUTTON1_PORT        PORTC
#define BUTTON1_PIN         1
#define BUTTON2_PORT        PORTC
#define BUTTON2_PIN         2
#define BUTTON3_PORT        PORTC
#define BUTTON3_PIN         3
#define BUTTON4_PORT        PORTC
#define BUTTON4_PIN         4
#define BUTTON5_PORT        PORTC
#define BUTTON5_PIN         5

// Miscellaneous

#define BUTTON_ENABLE_PORT  PORTB
#define BUTTON_ENABLE_PIN   0

#define BEEPER_PORT         PORTB
#define BEEPER_PIN          1

#endif  // PORTDEF_H
