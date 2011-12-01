/*------------------------------------------------------------------------------
Name:       player.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       24-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168/328

Content:    Simple single-note music player, loosely based on IBM/GWBASIC
            interpreter PLAY strings
------------------------------------------------------------------------------*/

#ifndef BEEPER_H
#define BEEPER_H

// Invocation rate for player_service(), calls per second

#define PLAYER_TICKS_PER_SECOND  625

// Type definitions

typedef enum {              // Timer prescaler options
    PRESCALE_STOP = 0,      //   Timer stopped 
    PRESCALE_1 = 1,         //   F_CPU / 1
    PRESCALE_8 = 2,         //   F_CPU / 8
    PRESCALE_64 = 3,        //   F_CPU / 64
    PRESCALE_256 = 4,       //   F_CPU / 256
    PRESCALE_1024 = 5,      //   F_CPU / 1024
    PRESCALE_EXT_L = 6,     //   External, falling edge
    PRESCALE_EXT_H = 7      //   External, rising edge
} prescale_t;

typedef enum {              // Player string memory space fetch options
    PLAYER_MEM_RAM,         //   Fetch player string from RAM
    PLAYER_MEM_PGM,         //   Fetch player string from FLASH/program memory
    PLAYER_MEM_EEPROM,      //   Fetch player string from EEPROM
    PLAYER_MEM_UNKNOWN
} player_space_t;

//------------------------------------------------------------------------------

// Public (exported) functions:

// Turn on tone generator

void beep_period(uint16_t period, prescale_t prescale);

// Turn off tone generator (set output volume to 0)

void beep_mute(uint8_t mute);

// Set output volume

void beep_gain(uint8_t gain);

// Initialize tone generator (timer)

void beeper_init(void);

// Start playing a music string (init music player)

void player_start(const char *str, player_space_t mem_space);

// Stop playback of a music string in progress

void player_stop(void);

// Determine if the current music string has finished playing

uint8_t player_is_stopped(void);

// Player service, interprets and plays a music string
// Must be called periodically (typically from a timer interrupt)

void player_service(void);

#endif  // BEEPER_H
