/*------------------------------------------------------------------------------
Name:       player.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       17-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168/328

Content:    Simple single-note music player, loosely based on IBM/GW BASIC
            interpreter PLAY strings
------------------------------------------------------------------------------*/

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include "portdef.h"
#include "player.h"

#define debug_out(x) { while (!(UCSR0A & BM(UDRE0))); UDR0 = x; }

// Constants

#define OCTAVES             10
#define NOTES_PER_OCTAVE    12
#define NUM_BOOKMARKS       10

#define DEFAULT_TEMPO       120
#define DEFAULT_BEAT        4

// Player commands and modifiers

#define is_separator(ch)    ((ch == ' ') || (ch == ':'))
#define is_digit(ch)        ((ch >= '0') && (ch <= '9'))

#define is_note(ch)         ((ch >= 'A') && (ch <= 'G'))
#define is_rest(ch)         ((ch == 'R') || (ch == ','))
#define is_repeat_note(ch)  (ch == '!')

#define is_octave_mod(ch)   ((ch >= '0') && (ch <= '9'))
#define is_flat(ch)         (ch == '-')
#define is_natural(ch)      ((ch == 'N') || (ch == '='))
#define is_sharp(ch)        ((ch == '+') || (ch == '#'))
#define is_dotted(ch)       (ch == '.')
#define is_triplet(ch)      (ch == '/')
#define is_tied(ch)         ((ch == '|') || (ch == ','))
#define is_staccato(ch)     (ch == '^')
#define is_whole(ch)        (ch == 'W')
#define is_half(ch)         (ch == 'H')
#define is_quarter(ch)      (ch == 'Q')
#define is_8th(ch)          (ch == 'I')
#define is_16th(ch)         (ch == 'S')
#define is_32nd(ch)         (ch == 'Y')

#define is_octave_up(ch)    (ch == '>')
#define is_octave_down(ch)  (ch == '<')
#define is_octave_cmd(ch)   (ch == 'O')
#define is_ratio_cmd(ch)    (ch == 'M')
#define is_volume_cmd(ch)   (ch == 'V')
#define is_tempo_cmd(ch)    (ch == 'T')
#define is_transpose_cmd(ch) (ch == 'P')
#define is_key_cmd(ch)      (ch == 'K')
#define is_bookmark(ch)     (ch == '[')
#define is_goto_mark(ch)    (ch == ']')
#define is_reset_cmd(ch)    (ch == '*')

// Note modifier bitflags

#define MOD_DOTTED          0x01
#define MOD_TRIPLET         0x02
#define MOD_TIED            0x04
#define MOD_STACCATO        0x08

// Accidentals

#define ACCIDENTAL_FLAT     -1
#define ACCIDENTAL_NATURAL  0
#define ACCIDENTAL_SHARP    +1

// Note sizes

#define NOTE_WHOLE          1
#define NOTE_HALF           2
#define NOTE_QUARTER        4
#define NOTE_8TH            8
#define NOTE_16TH           16
#define NOTE_32ND           32

// Player run states

#define PLAYER_STOP         0
#define PLAYER_RUN          1
#define PLAYER_INIT         2

// Type definitions

typedef enum {          // Player execution states
    STATE_RESET,        //   Reset player to defaults
    STATE_GET_NOTE,     //   Get note or command
    STATE_GET_MODIFIER, //   Get note modifiers (length, octave, accidentals)
    STATE_START_NOTE,   //   Start playing note
    STATE_WAIT_NOTE,    //   Wait for note to finish
    STATE_START_REST,   //   Start playing rest
    STATE_WAIT_REST,    //   Wait for rest to finish
    STATE_GET_DIGIT,    //   Get single-digit parameter for command
    STATE_GET_NUMBER,   //   Get multi-digit parameter for command (init)
    STATE_GET_NUMBER_2, //   Get parameter digits until non-digit encountered
    STATE_SET_OCTAVE,   //   Complete "set octave" command
    STATE_SET_NOTE_RATIO, // Complete "set note/rest ratio" command
    STATE_SET_VOLUME,   //   Complete "set volume" command
    STATE_SET_TRANSPOSITION, // Complete "set transposition" command
    STATE_SET_KEY,      //   1st stage of "set key" command
    STATE_SET_KEY_2,    //   Fetch sharp/flat notes for "set key" command
    STATE_SET_TEMPO,    //   1st stage of "set tempo" command (beat unit fetched)
    STATE_SET_TEMPO_2,  //   Complete "set tempo" command (beats/min fetched)
    STATE_SET_BOOKMARK, //   1st stage of "set bookmark" command (bookmark # fetched)
    STATE_SET_BOOKMARK_2, // Complete "set bookmark" command (repeat count fetched)
    STATE_GOTO_BOOKMARK,//   Complete "goto bookmark" command 
    STATE_STOP          //   Stop playing (mute output)
} player_state_t;


typedef struct {        // Note data
    uint16_t period;    //   Timer period (output compare value)
    prescale_t prescale;//   Prescaler selection
} note_t;

typedef struct {        // Player bookmark info
    uint8_t *position;  //   Bookmark position in string
    uint8_t repeat;     //   # times to repeat
} bookmark_t;

// Module (private) variables

static uint16_t         player_timer;
static uint8_t          *player_ptr;
static player_space_t   player_mem_space;
static uint8_t          player_enable;
static bookmark_t       bookmark[NUM_BOOKMARKS];

//-----------------------------------------------------------------------------

// Note table

// Note frequencies are calculated by applying the formula:
//   Note[n] = Note[n-1] * 2 ^ (1/12)     2 ^ (1/12) = 1.059463094
// Or equivalently:
//   Note[n] = Note[0] * (2^(1/12))^n
//   Note[0] = Low C (C0) = 16.35 Hz
//
// If the 16-bit timer is used, almost any audible frequency - including all
// octave 0 notes - should be attainable even if running at 20 MHz.
// Use of a 8-bit timer is possible, but several parts of the code that deal
// with the timer subsystem will have to be modified.  Also, depending on
// what F_CPU is, some of the lowest octave notes may not be attainable, even
// when the prescaler is set to max (f/1024).
//
// Consider this example for 8-bit timer use with F_CPU = 12000000:
//
// Octave 0 is not entirely attainable when F_CPU = 12000000, because:
//   F_CPU / 21.82 (F0) / 1024 (max prescale) / 2 - 0.5 ~= 268
//   268 exceeds 8-bit timer resolution of 255
//   F#0 is attainable with a period of 252
//
// Period is the reciprocal of frequency, and for any of the AVR timers it is
// calculated as:
//
//   F_CPU / frequency / prescaler / 2 - 0.5
//
// Must divide by 2 since timer inverts output on each compare match, so
// one complete on-off cycle is two timer output compare match events.
//  Subtraction of 0.5 is done for rounding purposes.  Normally, one would ADD
// 0.5 to the result to 'round up', but the output period of the timer is
// actually what is put in the output compare register PLUS 1, so by 'rounding
// down' this requirement is satisfied while still selecting a period that is
// close to ideal as the timer resolution will allow.
//
// Tonal quality is limited by the timer resolution and prescaler selections,
// so some notes - particularily those in octave 6 and 7 - may sound somewhat
// flat or sharp if a 8-bit timer is used.

#define NOTE_PERIOD(freq, prescale)     (F_CPU / freq / prescale / 2 - 0.5)
#define NOTE_FAULT_(freq, prescale)     (255)

static const note_t note_table[OCTAVES][NOTES_PER_OCTAVE] PROGMEM =
{
    {
        {.period = NOTE_PERIOD(16.35,  8), .prescale = PRESCALE_8   },  // C0
        {.period = NOTE_PERIOD(17.32,  8), .prescale = PRESCALE_8   },  // C#0
        {.period = NOTE_PERIOD(18.35,  8), .prescale = PRESCALE_8   },  // D0
        {.period = NOTE_PERIOD(19.44,  8), .prescale = PRESCALE_8   },  // D#0
        {.period = NOTE_PERIOD(20.60,  8), .prescale = PRESCALE_8   },  // E0
        {.period = NOTE_PERIOD(21.82,  8), .prescale = PRESCALE_8   },  // F0
        {.period = NOTE_PERIOD(23.12,  8), .prescale = PRESCALE_8   },  // F#0
        {.period = NOTE_PERIOD(24.50,  8), .prescale = PRESCALE_8   },  // G0
        {.period = NOTE_PERIOD(25.95,  8), .prescale = PRESCALE_8   },  // G#0
        {.period = NOTE_PERIOD(27.50,  8), .prescale = PRESCALE_8   },  // A0
        {.period = NOTE_PERIOD(29.13,  8), .prescale = PRESCALE_8   },  // A#0
        {.period = NOTE_PERIOD(30.86,  8), .prescale = PRESCALE_8   },  // B0
    },
    {
        {.period = NOTE_PERIOD(32.70,  8), .prescale = PRESCALE_8   },  // C1
        {.period = NOTE_PERIOD(34.65,  8), .prescale = PRESCALE_8   },  // C#1
        {.period = NOTE_PERIOD(36.71,  8), .prescale = PRESCALE_8   },  // D1
        {.period = NOTE_PERIOD(38.89,  8), .prescale = PRESCALE_8   },  // D#1
        {.period = NOTE_PERIOD(41.20,  8), .prescale = PRESCALE_8   },  // E1
        {.period = NOTE_PERIOD(43.65,  8), .prescale = PRESCALE_8   },  // F1
        {.period = NOTE_PERIOD(46.25,  8), .prescale = PRESCALE_8   },  // F#1
        {.period = NOTE_PERIOD(49.00,  8), .prescale = PRESCALE_8   },  // G1
        {.period = NOTE_PERIOD(51.91,  8), .prescale = PRESCALE_8   },  // G#1
        {.period = NOTE_PERIOD(55.00,  8), .prescale = PRESCALE_8   },  // A1
        {.period = NOTE_PERIOD(58.27,  8), .prescale = PRESCALE_8   },  // A#1
        {.period = NOTE_PERIOD(61.73,  8), .prescale = PRESCALE_8   },  // B1
    },
    {
        {.period = NOTE_PERIOD(65.41,  8), .prescale = PRESCALE_8   },  // C2
        {.period = NOTE_PERIOD(69.30,  8), .prescale = PRESCALE_8   },  // C#2
        {.period = NOTE_PERIOD(73.42,  8), .prescale = PRESCALE_8   },  // D2
        {.period = NOTE_PERIOD(77.78,  8), .prescale = PRESCALE_8   },  // D#2
        {.period = NOTE_PERIOD(82.41,  8), .prescale = PRESCALE_8   },  // E2
        {.period = NOTE_PERIOD(87.31,  8), .prescale = PRESCALE_8   },  // F2
        {.period = NOTE_PERIOD(92.50,  8), .prescale = PRESCALE_8   },  // F#2
        {.period = NOTE_PERIOD(98.00,  8), .prescale = PRESCALE_8   },  // G2
        {.period = NOTE_PERIOD(103.8,  8), .prescale = PRESCALE_8   },  // G#2
        {.period = NOTE_PERIOD(110.0,  8), .prescale = PRESCALE_8   },  // A2
        {.period = NOTE_PERIOD(116.5,  8), .prescale = PRESCALE_8   },  // A#2
        {.period = NOTE_PERIOD(123.5,  1), .prescale = PRESCALE_1   },  // B2
    },
    {
        {.period = NOTE_PERIOD(130.8,  1), .prescale = PRESCALE_1   },  // C3
        {.period = NOTE_PERIOD(138.6,  1), .prescale = PRESCALE_1   },  // C#3
        {.period = NOTE_PERIOD(146.8,  1), .prescale = PRESCALE_1   },  // D3
        {.period = NOTE_PERIOD(155.6,  1), .prescale = PRESCALE_1   },  // D#3
        {.period = NOTE_PERIOD(164.8,  1), .prescale = PRESCALE_1   },  // E3
        {.period = NOTE_PERIOD(174.6,  1), .prescale = PRESCALE_1   },  // F3
        {.period = NOTE_PERIOD(185.0,  1), .prescale = PRESCALE_1   },  // F#3
        {.period = NOTE_PERIOD(196.0,  1), .prescale = PRESCALE_1   },  // G3
        {.period = NOTE_PERIOD(207.7,  1), .prescale = PRESCALE_1   },  // G#3
        {.period = NOTE_PERIOD(220.0,  1), .prescale = PRESCALE_1   },  // A3
        {.period = NOTE_PERIOD(233.1,  1), .prescale = PRESCALE_1   },  // A#3
        {.period = NOTE_PERIOD(246.9,  1), .prescale = PRESCALE_1   },  // B3
    },
    {
        {.period = NOTE_PERIOD(261.6,  1), .prescale = PRESCALE_1   },  // C4
        {.period = NOTE_PERIOD(277.2,  1), .prescale = PRESCALE_1   },  // C#4
        {.period = NOTE_PERIOD(293.7,  1), .prescale = PRESCALE_1   },  // D4
        {.period = NOTE_PERIOD(311.1,  1), .prescale = PRESCALE_1   },  // D#4
        {.period = NOTE_PERIOD(329.6,  1), .prescale = PRESCALE_1   },  // E4
        {.period = NOTE_PERIOD(349.2,  1), .prescale = PRESCALE_1   },  // F4
        {.period = NOTE_PERIOD(370.0,  1), .prescale = PRESCALE_1   },  // F#4
        {.period = NOTE_PERIOD(392.0,  1), .prescale = PRESCALE_1   },  // G4
        {.period = NOTE_PERIOD(415.3,  1), .prescale = PRESCALE_1   },  // G#4
        {.period = NOTE_PERIOD(440.0,  1), .prescale = PRESCALE_1   },  // A4
        {.period = NOTE_PERIOD(466.2,  1), .prescale = PRESCALE_1   },  // A#4
        {.period = NOTE_PERIOD(493.9,  1), .prescale = PRESCALE_1   },  // B4
    },
    {
        {.period = NOTE_PERIOD(523.3,  1), .prescale = PRESCALE_1   },  // C5
        {.period = NOTE_PERIOD(554.4,  1), .prescale = PRESCALE_1   },  // C#5
        {.period = NOTE_PERIOD(587.3,  1), .prescale = PRESCALE_1   },  // D5
        {.period = NOTE_PERIOD(622.3,  1), .prescale = PRESCALE_1   },  // D#5
        {.period = NOTE_PERIOD(659.3,  1), .prescale = PRESCALE_1   },  // E5
        {.period = NOTE_PERIOD(698.5,  1), .prescale = PRESCALE_1   },  // F5
        {.period = NOTE_PERIOD(740.0,  1), .prescale = PRESCALE_1   },  // F#5
        {.period = NOTE_PERIOD(784.0,  1), .prescale = PRESCALE_1   },  // G5
        {.period = NOTE_PERIOD(830.6,  1), .prescale = PRESCALE_1   },  // G#5
        {.period = NOTE_PERIOD(880.0,  1), .prescale = PRESCALE_1   },  // A5
        {.period = NOTE_PERIOD(932.3,  1), .prescale = PRESCALE_1   },  // A#5
        {.period = NOTE_PERIOD(987.8,  1), .prescale = PRESCALE_1   },  // B5
    },
    {
        {.period = NOTE_PERIOD(1047,   1), .prescale = PRESCALE_1   },  // C6
        {.period = NOTE_PERIOD(1109,   1), .prescale = PRESCALE_1   },  // C#6
        {.period = NOTE_PERIOD(1175,   1), .prescale = PRESCALE_1   },  // D6
        {.period = NOTE_PERIOD(1245,   1), .prescale = PRESCALE_1   },  // D#6
        {.period = NOTE_PERIOD(1319,   1), .prescale = PRESCALE_1   },  // E6
        {.period = NOTE_PERIOD(1397,   1), .prescale = PRESCALE_1   },  // F6
        {.period = NOTE_PERIOD(1480,   1), .prescale = PRESCALE_1   },  // F#6
        {.period = NOTE_PERIOD(1568,   1), .prescale = PRESCALE_1   },  // G6
        {.period = NOTE_PERIOD(1661,   1), .prescale = PRESCALE_1   },  // G#6
        {.period = NOTE_PERIOD(1760,   1), .prescale = PRESCALE_1   },  // A6
        {.period = NOTE_PERIOD(1865,   1), .prescale = PRESCALE_1   },  // A#6
        {.period = NOTE_PERIOD(1976,   1), .prescale = PRESCALE_1   },  // B6
    },
    {
        {.period = NOTE_PERIOD(2093,   1), .prescale = PRESCALE_1   },  // C7
        {.period = NOTE_PERIOD(2217,   1), .prescale = PRESCALE_1   },  // C#7
        {.period = NOTE_PERIOD(2349,   1), .prescale = PRESCALE_1   },  // D7
        {.period = NOTE_PERIOD(2489,   1), .prescale = PRESCALE_1   },  // D#7
        {.period = NOTE_PERIOD(2637,   1), .prescale = PRESCALE_1   },  // E7
        {.period = NOTE_PERIOD(2794,   1), .prescale = PRESCALE_1   },  // F7
        {.period = NOTE_PERIOD(2960,   1), .prescale = PRESCALE_1   },  // F#7
        {.period = NOTE_PERIOD(3136,   1), .prescale = PRESCALE_1   },  // G7
        {.period = NOTE_PERIOD(3322,   1), .prescale = PRESCALE_1   },  // G#7
        {.period = NOTE_PERIOD(3520,   1), .prescale = PRESCALE_1   },  // A7
        {.period = NOTE_PERIOD(3729,   1), .prescale = PRESCALE_1   },  // A#7
        {.period = NOTE_PERIOD(3951,   1), .prescale = PRESCALE_1   },  // B7
    },
    {
        {.period = NOTE_PERIOD(4186,   1), .prescale = PRESCALE_1   },  // C8
        {.period = NOTE_PERIOD(4434,   1), .prescale = PRESCALE_1   },  // C#8
        {.period = NOTE_PERIOD(4698,   1), .prescale = PRESCALE_1   },  // D8
        {.period = NOTE_PERIOD(4978,   1), .prescale = PRESCALE_1   },  // D#8
        {.period = NOTE_PERIOD(5274,   1), .prescale = PRESCALE_1   },  // E8
        {.period = NOTE_PERIOD(5588,   1), .prescale = PRESCALE_1   },  // F8
        {.period = NOTE_PERIOD(5920,   1), .prescale = PRESCALE_1   },  // F#8
        {.period = NOTE_PERIOD(6272,   1), .prescale = PRESCALE_1   },  // G8
        {.period = NOTE_PERIOD(6645,   1), .prescale = PRESCALE_1   },  // G#8
        {.period = NOTE_PERIOD(7040,   1), .prescale = PRESCALE_1   },  // A8
        {.period = NOTE_PERIOD(7459,   1), .prescale = PRESCALE_1   },  // A#8
        {.period = NOTE_PERIOD(7902,   1), .prescale = PRESCALE_1   },  // B8
    },
    {
        {.period = NOTE_PERIOD(8372,   1), .prescale = PRESCALE_1   },  // C9
        {.period = NOTE_PERIOD(8870,   1), .prescale = PRESCALE_1   },  // C#9
        {.period = NOTE_PERIOD(9397,   1), .prescale = PRESCALE_1   },  // D9
        {.period = NOTE_PERIOD(9956,   1), .prescale = PRESCALE_1   },  // D#9
        {.period = NOTE_PERIOD(10548,  1), .prescale = PRESCALE_1   },  // E9
        {.period = NOTE_PERIOD(11175,  1), .prescale = PRESCALE_1   },  // F9
        {.period = NOTE_PERIOD(11840,  1), .prescale = PRESCALE_1   },  // F#9
        {.period = NOTE_PERIOD(12544,  1), .prescale = PRESCALE_1   },  // G9
        {.period = NOTE_PERIOD(13290,  1), .prescale = PRESCALE_1   },  // G#9
        {.period = NOTE_PERIOD(14080,  1), .prescale = PRESCALE_1   },  // A9
        {.period = NOTE_PERIOD(14917,  1), .prescale = PRESCALE_1   },  // A#9
        {.period = NOTE_PERIOD(15804,  1), .prescale = PRESCALE_1   },  // B9
    }
};

/******************************************************************************
 * beeper_init()
 *
 * Initialize tone generator (timer)
 *
 * Inputs:  None
 *
 * Returns: Nothing
 *
 * Notes:   16-bit TIMER1 is used for the purpose of generating tones in this
 *          application.  A different timer may be used but all functions
 *          in this library, along with the prescaler selections in note_table
 *          will need to be modified.
 ******************************************************************************/

void beeper_init(void)
{
    // Stop timer 1 during init
    // Disable timer 1 interrupts

    TCCR1B &= (uint8_t) ~PRESCALE_STOP;
    TCNT1 = 0;
    TIMSK1 = 0;

    // Set initial frequency to 440 Hz

    OCR1A = F_CPU / 1 / 440 / 2;

    // Timer 1 configuration:
    // Toggle OC1A on compare match and reset counter
    // Keep counter stopped, use beep_period() to enable

    TCCR1A = _BV(COM1A0);                   // Toggle OC1A on compare match
    TCCR1B = _BV(WGM12);                    // CTC mode, reset when count == OCR1A
    DDR(BEEPER_PORT) |= _BV(BEEPER_PIN);
    BEEPER_PORT &= (uint8_t) ~_BV(BEEPER_PIN);
}

/******************************************************************************
 * beep_period(period, prescale)
 *
 * Turn on the tone generator
 *
 * Inputs:  period      Timer period
 *          prescale    Timer prescaler selection (from prescale_t enum)
 *
 * Returns: Nothing
 *
 * Notes:   Output frequency = F_CPU / selected prescaler / (period+1) / 2
 ******************************************************************************/

void beep_period(uint16_t period, prescale_t prescale)
{
    // Stop counter & reset it

    TCCR1B &= (uint8_t) ~PRESCALE_STOP;
    TCNT1 = 0;

    // Set period & prescaler

#if CS10 != 0
    prescale <<= CS10;
#endif

    OCR1A = period;
    TCCR1B |= prescale & (0x07 << CS10);
}

/******************************************************************************
 * beep_mute(mute)
 *
 * Turn off tone generator (stop timer)
 *
 * Inputs:  mute        Mutes output if nonzero
 *
 * Returns: Nothing
 ******************************************************************************/

void beep_mute(uint8_t mute)
{
    if (mute) {
        TCCR1B &= (uint8_t) ~PRESCALE_STOP;
    }
}

/******************************************************************************
 * beep_gain(gain)
 *
 * Set output gain (volume)
 *
 * Inputs:  gain        Output gain/volume setting to apply
 *
 * Returns: Nothing
 *
 * Notes:   This function is provided for those designs that provide a means by
 *          which the output volume can be set.  This typically requires some
 *          sort of external hardware support.  If such hardware is not
 *          available, this function should be declared empty.
 ******************************************************************************/

void beep_gain(uint8_t gain)
{
}

/******************************************************************************
 * uint8_t next_player_char()
 *
 * Fetch next character from the play string in the selected memory space
 *
 * Inputs:  None passed in - uses these local module variables:
 *          player_mem_space    Memory space containing player music string,
 *                              as defined in the player_space_t enum.
 *          player_ptr          Pointer to player music string.  Incremented to
 *                              point to the next character in the string on
 *                              exit.
 *
 * Returns: Character pointed to by player_ptr on entry, converted to uppercase.
 ******************************************************************************/

static uint8_t next_player_char(void)
{
    uint8_t data;

    if (player_mem_space == PLAYER_MEM_RAM) {
        data = *player_ptr;
    }
    else if (player_mem_space == PLAYER_MEM_PGM) {
        data = pgm_read_byte(player_ptr);
    }
    else if (player_mem_space == PLAYER_MEM_EEPROM) {
        data = eeprom_read_byte(player_ptr);
    }
    else {
        data = 0;
    }

    if ((data >= 'a') && (data <= 'z')) {
        data &= (uint8_t) ~0x20;
    }

    player_ptr++;

    return data;
}

/******************************************************************************
 * uint8_t find_bookmark(search_mark)
 *
 * Locate bookmark(s) in the player music string
 *
 * Inputs:  search_mark     The bookmark # to search for in the player string.
 *                          If the value 0xFF is passed in, ALL bookmarks in
 *                          the player string are found and their positions
 *                          recorded in bookmark[].
 *
 *          In addition, the following local module variables are used:
 *
 *          bookmark[]      This array records the positions (pointers to) the
 *                          bookmark(s) searched for and the repeat count
 *                          associated with it.  If all bookmarks are pre-
 *                          searched before the music string starts playing,
 *                          some play-time execution overhead can be saved.
 *          player_mem_space
 *          player_ptr      These variables are used by the next_player_char()
 *                          function which is called within this function.
 *                          The value of player_ptr is preserved and reset to
 *                          its original value upon exit.
 *
 * Returns: Pointer to position in player music string where the specified
 *          <search_mark> bookmark was found, or NULL if not found.  Will
 *          also return NULL if a all-bookmark search is requested.
 ******************************************************************************/

static uint8_t * find_bookmark(uint8_t search_mark)
{
    uint8_t found_mark;                 // Nonzero when bookmark <search_mark> found
    uint8_t ch;                         // Character fetched from play string
    uint8_t mark;                       // Bookmark # being processed
    uint16_t repeat;                    // Repeat count for current bookmark
    uint8_t *save_ptr;                  // Saved [start] address of play string
    uint8_t *return_ptr;                // Function return value (pointer to bookmark)

    // Save the current player string address so it can be restored when bookmark
    // scanning is completed.

    save_ptr = player_ptr;
    found_mark = 0;                     // Bookmark not found yet

    // Scan play string for bookmark(s)

    do {
        ch = next_player_char();
        if (is_bookmark(ch)) {          // Bookmark lead-in character found
            repeat = 0;
            mark = next_player_char();  // Fetch bookmark #

            if (is_separator(mark)) {   // Assume bookmark #0 if seperator
                mark = 0;
            }
            else if (is_digit(ch)) {    // Convert bookmark # to integer
                mark -= '0';
            }
            else {                      // Skip analysis if bookmark # invalid
                continue;
            }

            ch = next_player_char();    // Fetch 1st digit of repeat count
            if (is_separator(ch)) {     // Skip past possible seperator
                ch = next_player_char();
            }

            while (is_digit(ch)) {      // Convert repeat count to integer
                ch -= '0';
                repeat = repeat * 10 + ch;
                ch = next_player_char();
            }
            player_ptr--;               // Last char not a digit, back up

            if (repeat > 0xFE) {        // Valid repeat counts are 0..254
                repeat = 0xFE;          //   Force to maximum if > 254
            }
            if (! repeat) {             // Repeat count of 0 is "infinite"
                repeat = 0xFF;          //   0xFF = infinite, never decremented
            }

            // Save bookmark data

            found_mark = (mark == search_mark);
            if (found_mark || (search_mark == 0xFF)) {
                bookmark[mark].repeat = repeat;
                bookmark[mark].position = player_ptr;
            }
        }
    } while (ch && !found_mark);

    // Return address of play string immediately following bookmark spec
    // Restore play string pointer to position it was at upon entry

    return_ptr = player_ptr;
    player_ptr = save_ptr;

    // Return NULL if bookmark #<search_mark> not found

    if (! found_mark) {
        return_ptr = 0;
    }

    return return_ptr;
}

/******************************************************************************
 * player_start(*str, mem_space)
 *
 * Start playing a music string (initialize player)
 *
 * Inputs:  *str        Pointer to the music string to play
 *          mem_space   The memory space in which <str> resides, as selected
 *                      from the player_space_t enum.  A music string may
 *                      reside in data RAM (1), program memory space (2), or
 *                      in EEPROM (3).
 *
 *          In addition, the following local module variables are used:
 *
 *          player_ptr          Set to <str>; points to string to play.
 *          player_mem_space    Set to <mem_space>
 *          player_enable       Set to 2, forcing the player to initialize
 *                              its internal variables and start playing
 *                              the specified music string.
 *          bookmark[]          This routine pre-scans for bookmarks in the
 *                              player string via the find_bookmark() function.
 *
 * Returns: Nothing
 *
 * Notes:   The player_service() function must be called periodically, typically
 *          from a timer interrupt, for the music <str> to be played back.
 ******************************************************************************/

void player_start(const char *str, player_space_t mem_space)
{
    uint8_t index;

    // Set player string index and memory space to values passed in

    player_stop();                      // Stop playing previous string
    player_ptr = (uint8_t *) str;       // Typecast prevents compiler warning
    player_mem_space = mem_space;

    // Clear all bookmarks

    for (index = 0; index < NUM_BOOKMARKS; index++) {
        bookmark[index].repeat = 0;
        bookmark[index].position = 0;
    }

    // Scan play string for bookmarks and initialize them.
    // This pre-scan allows bookmarks to be forward-referenced without having
    // to perform time-consuming bookmark scanning during playback.

    find_bookmark(0xFF);

    // Allow playback to start

    player_enable = PLAYER_INIT;
}

/******************************************************************************
 * player_stop()
 *
 * Stop playback of the current music string being played (if any)
 *
 * Inputs:  None
 *
 * Returns: Nothing
 ******************************************************************************/

void player_stop(void)
{
    player_enable = PLAYER_STOP;
    beep_mute(1);
    beep_period(0xFF, PRESCALE_STOP);
}

/******************************************************************************
 * uint8_t player_is_stopped()
 *
 * Determine if the present music string is still in the process of being
 * played back.
 *
 * Inputs:  None
 *
 * Returns: Nonzero value if player is idle.
 ******************************************************************************/

uint8_t player_is_stopped(void)
{
    return player_enable == PLAYER_STOP;
}

/******************************************************************************
 * player_service()
 *
 * Plays a music string
 *
 * Must be called periodically at the rate defined by PLAYER_TICKS_PER_SECOND
 * which is set in "player.h"
 ******************************************************************************/

// C major:                                A  B  C  D  E  F  G
static int8_t c_major_scale[7] PROGMEM = { 9,11, 0, 2, 4, 5, 7};

#define NOTE_IS_REST    0xFF

void player_service(void)
{
    static player_state_t state;        // Player execution state
    static player_state_t next_state;   // State to advance to when done with present state
    static int8_t note;                 // Note #
    static int8_t octave;               // Octave #
    static int8_t accidental;           // Accidental: -1=Flat 0=Natural +1=Sharp
    static int8_t transposition;        // # of halfsteps to transpose notes up
    static uint8_t note_size;           // Note size fraction; e.g. 4=quarter note
    static uint8_t size_modifier;       // Note size modifier flags: 0:Dotted 1:Triplet 2:Tied 3:Staccato
    static uint8_t note_rest_ratio;     // Note-to-rest ratio, in 8ths
    static uint16_t whole_note_period;  // # ticks in a whole note
    static uint16_t note_period;        // Note play period, PLAYER_TICKS_PER_SECOND units
    static uint16_t rest_period;        // Rest (silence) period, PLAYER_TICKS_PER_SECOND units
    static int8_t scale[7];             // Note letter-to-number conversion, adjusted for key signature

    int8_t ch;                          // Character fetched from play string
    int8_t digit;                       // Integerized single-digit parameter
    uint16_t number;                    // Integerized multi-digit parameter
    note_t *n;                          // Pointer to tone generator note data

    // Exit player if stopped

    if (player_enable == PLAYER_STOP) {
        return;
    }

    // <number> and <digit> init not really needed, but prevents compiler
    // from complaining

    number = 0;
    digit = 0;
    ch = 0;

    player_timer++;

    // Interpret and execute play string
    // Use of do { } while(1) allows use of <break> and <continue> to facilitate
    // state machine execution.

    do {

        // RESET state
        // Resets all player variables to their default state.
        // This state is forced as the first state to be executed when
        // a new play string is defined using the start_player() function.

        if ((state == STATE_RESET) ||
            (player_enable == PLAYER_INIT)) {
            for (digit = 0; digit < 7; digit++) {
                scale[digit] = pgm_read_byte(&c_major_scale[digit]);
            }
            note = 0;
            octave = 4;
            accidental = ACCIDENTAL_NATURAL;
            transposition = 0;
            note_size = 4;
            size_modifier = 0;
            note_rest_ratio = 7;
            whole_note_period = (uint16_t) (PLAYER_TICKS_PER_SECOND * 60U * (uint32_t) DEFAULT_BEAT / DEFAULT_TEMPO);
            player_timer = 0;
            beep_period(0xFF, PRESCALE_STOP);
            beep_mute(0);
            beep_gain(5);
            player_enable = PLAYER_RUN;
            state = STATE_GET_NOTE;
            continue;
        }

        // GET NOTE state
        // This is the base state for the player.  Characters are fetched from
        // the play string, and interpreted as notes or commands.

        else if (state == STATE_GET_NOTE) {
            ch = next_player_char();
            if (is_separator(ch)) {
                continue;
            }
            else if (is_note(ch)) {
                ch -= 'A';
                note = scale[ch];
                accidental = ACCIDENTAL_NATURAL;
                state = STATE_GET_MODIFIER;
                next_state = STATE_START_NOTE;
                continue;
            }
            else if (is_rest(ch)) {
                note = NOTE_IS_REST;
                state = STATE_GET_MODIFIER;
                next_state = STATE_START_NOTE;
                continue;
            }
            else if (is_repeat_note(ch)) {
                state = STATE_START_NOTE;
                continue;
            }
            else if (is_octave_cmd(ch)) {
                state = STATE_GET_DIGIT;
                next_state = STATE_SET_OCTAVE;
                continue;
            }
            else if (is_octave_up(ch)) {
                if (octave < (OCTAVES - 1)) {
                    octave++;
                }
                continue;
            }
            else if (is_octave_down(ch)) {
                if (octave > 0) {
                    octave--;
                }
                continue;
            }
            else if (is_ratio_cmd(ch)) {
                state = STATE_GET_DIGIT;
                next_state = STATE_SET_NOTE_RATIO;
                continue;
            }
            else if (is_volume_cmd(ch)) {
                state = STATE_GET_DIGIT;
                next_state = STATE_SET_VOLUME;
                continue;
            }
            else if (is_transpose_cmd(ch)) {
                state = STATE_GET_NUMBER;
                next_state = STATE_SET_TRANSPOSITION;
            }
            else if (is_key_cmd(ch)) {
                state = STATE_SET_KEY;
                continue;
            }
            else if (is_tempo_cmd(ch)) {
                // Assume quarter note gets 1 beat
                note_size = DEFAULT_BEAT;
                size_modifier = 0;
                ch = next_player_char();
                // Use default note size if not specified
                if (is_separator(ch) || is_digit(ch)) {
                    player_ptr--;
                    state = STATE_GET_NUMBER;
                    next_state = STATE_SET_TEMPO_2;
                }
                // Get a note size
                else {
                    state = STATE_GET_MODIFIER;
                    next_state = STATE_SET_TEMPO;
                }
                continue;
            }
            else if (is_bookmark(ch)) {
                state = STATE_GET_DIGIT;
                next_state = STATE_SET_BOOKMARK;
                continue;
            }
            else if (is_goto_mark(ch)) {
                state = STATE_GET_DIGIT;
                next_state = STATE_GOTO_BOOKMARK;
                continue;
            }
            else if (is_reset_cmd(ch)) {
                state = STATE_RESET;
                continue;
            }
            else if (!ch) {
                player_ptr--;
            }
            // Character not recognized, stop player
            state = STATE_STOP;
            continue;
        }

        // GET MODIFIER state
        // This state is entered after a note or rest value has been parsed.
        // Note time and pitch modifiers are fetched and processed in this
        // state.

        else if (state == STATE_GET_MODIFIER) {
            ch = next_player_char();
            if (is_separator(ch)) {
                state = next_state;
                continue;
            }
            else if (is_octave_mod(ch)) {
                octave = ch - '0';
                continue;
            }
            else if (is_dotted(ch)) {
                size_modifier |= MOD_DOTTED;
                continue;
            }
            else if (is_triplet(ch)) {
                size_modifier |= MOD_TRIPLET;
                continue;
            }
            else if (is_tied(ch)) {
                size_modifier |= MOD_TIED;
                continue;
            }
            else if (is_staccato(ch)) {
                size_modifier |= MOD_STACCATO;
                continue;
            }
            else if (is_flat(ch)) {
                accidental = ACCIDENTAL_FLAT;
                continue;
            }
            else if (is_natural(ch)) {
                accidental = ACCIDENTAL_NATURAL;
                continue;
            }
            else if (is_sharp(ch)) {
                accidental = ACCIDENTAL_SHARP;
                continue;
            }
            else if (is_whole(ch)) {
                note_size = NOTE_WHOLE;
                size_modifier = 0;
                continue;
            }
            else if (is_half(ch)) {
                note_size = NOTE_HALF;
                size_modifier = 0;
                continue;
            }
            else if (is_quarter(ch)) {
                note_size = NOTE_QUARTER;
                size_modifier = 0;
                continue;
            }
            else if (is_8th(ch)) {
                note_size = NOTE_8TH;
                size_modifier = 0;
                continue;
            }
            else if (is_16th(ch)) {
                note_size = NOTE_16TH;
                size_modifier = 0;
                continue;
            }
            else if (is_32nd(ch)) {
                note_size = NOTE_32ND;
                size_modifier = 0;
                continue;
            }
            else {
                player_ptr--;
                state = next_state;
                continue;
            }
        }

        // START NOTE state
        // Entered following the completion of a note or rest specification.
        // Calculates note frequency and timing parameters and configures
        // beeper (unless note is a rest).  Advances to WAIT NOTE or WAIT REST
        // state as appropriate.

        else if (state == STATE_START_NOTE) {
            rest_period = whole_note_period;
            // If dotted flag set, note period is 50% longer
            if (size_modifier & MOD_DOTTED) {
                rest_period += rest_period >> 1;
            }
            // If triplet flag set, note period is divided by 3
            if (size_modifier & MOD_TRIPLET) {
                rest_period /= 3;
            }
            // Divide base (whole note) period by note size
            // e.g. if quarter note, divide by 4
            rest_period /= note_size;
            // Note # 0xFF designates a rest
            if (note == NOTE_IS_REST) {
                state = STATE_START_REST;
                continue;
            }
            // Calculate note play time and rest time
            // The ratio of note to rest time is specified by <note_rest_ratio>
            // with a fixed dividend of 8
            // e.g. if note_rest_ratio = 5, play note for 5/8 of note time and
            // rest for 3/8 of note time
            // If note is tied or staccato, ignore note/rest ratio setting and
            // force it to 8 (tied) or 2 (staccato).
            if (size_modifier & MOD_TIED) {
                digit = 8;
            }
            else if (size_modifier & MOD_STACCATO) {
                digit = 2;
            }
            else {
                digit = note_rest_ratio;
            }
            note_period = (rest_period * digit) >> 3;
            rest_period -= note_period;
            // Adjust note tone to be played by accidental and transposition factors
            // <digit> is used as a temporary to hold the adjusted note tone
            // <ch> is temporarily used to hold adjusted octave
            digit = note + accidental + transposition;
            ch = octave;
            // Check for note over/underflow
            if (digit < 0) {
                digit += NOTES_PER_OCTAVE;
                ch--;
            }
            else if (digit >= NOTES_PER_OCTAVE) {
                digit -= NOTES_PER_OCTAVE;
                ch++;
            }
            // Check for octave over/underflow
            if (ch < 0) {
                ch = 0;
            }
            else if (ch >= OCTAVES) {
                ch = OCTAVES - 1;
            }
            // Fetch timer period and prescaler from note table
            n = (note_t *) &note_table[ch][digit];
            number = pgm_read_word(&n->period);
            digit = pgm_read_byte(&n->prescale);
            // Start playing note and advance to wait state
            beep_period(number, digit);
            state = STATE_WAIT_NOTE;
            continue;
        }

        // WAIT NOTE state
        // Entered following the initiation of note playback (beeper
        // turned on).  Idles player until <note_period> ticks have
        // elapsed.

        else if (state == STATE_WAIT_NOTE) {
            // Wait until note play period has elapsed
            if (player_timer < note_period) {
                break;
            }
            // Reset tick counter
            // Subtract time from it instead of zeroing it in case some ticks
            // were "missed" due to insufficiently fast polling.  Doing this
            // ensures that tempo is maintained, even if note/rest timing is
            // less than precise.
            player_timer -= note_period;
            // If the note has a rest period, advance to the rest state.
            // Otherwise, skip rest state and resume note/command scanning.
            if (rest_period) {
                state = STATE_START_REST;
            }
            else {
                state = STATE_GET_NOTE;
            }
            continue;
        }

        // START REST state
        // Entered following note playback or when a rest is requested.
        // Silences beeper output and sets up for rest delay.

        if (state == STATE_START_REST) {
            // Start of note rest period
            // Turn off beeper timer to silence it
            beep_period(0xFF, PRESCALE_STOP);
            // Advance to wait-for-rest state
            state = STATE_WAIT_REST;
            continue;
        }

        // WAIT REST state
        // Idles player until <rest_period> ticks have elapsed.
        // Usually entered when a rest ('R') is specified, or
        // following note playback to implement the silence period
        // between notes as specified by the <note_rest_ratio>.

        else if (state == STATE_WAIT_REST) {
            // Wait until rest period has elapsed
            if (player_timer < rest_period) {
                break;
            }
            // Reset tick counter
            // Subtract time from it instead of zeroing it in case some ticks
            // were "missed" due to insufficiently fast polling.  Doing this
            // ensures that tempo is maintained, even if note/rest timing is
            // less than precise.
            player_timer -= rest_period;
            // Resume note/command scanning
            state = STATE_GET_NOTE;
            continue;
        }

        // GET DIGIT state
        // Fetches a single ASCII decimal character and converts it to
        // an integer representation in <digit>.  Seperator characters
        // are interpreted as '0'.

        else if (state == STATE_GET_DIGIT) {
            // Assume no errors: Advance to state that will utilize digit
            state = next_state;
            // Get digit character
            digit = next_player_char();
            // Assume '0' for digit if seperator character
            if (is_separator(digit)) {
                digit = 0;
            }
            // If digit, convert it to integer
            else if (is_digit(digit)) {
                digit -= '0';
            }
            // If not a seperator or digit, play string is misformed.
            // Abort playback
            else {
                state = STATE_STOP;
            }
            continue;
        }

        // GET NUMBER state
        // Setup to fetch a multi-digit ASCII decimal value and convert it
        // to an integer representation.  Skips over any optional seperator
        // character.

        else if (state == STATE_GET_NUMBER) {
            // Clear number accumulator
            number = 0;
            // Skip past (optional) data separator
            ch = next_player_char();
            if (is_separator(ch)) {
                ch = next_player_char();
            }
            // Advance to state that will decode the multi-digit number
            state = STATE_GET_NUMBER_2;
            continue;
        }

        // GET NUMBER 2 state
        // Entered following execution of the GET NUMBER state
        // Fetches ASCII decimal digits and decimal-shifts them into the
        // <number> accumulator.  Exit from this state occurs when a
        // non-digit character is encountered.

        else if (state == STATE_GET_NUMBER_2) {
            while (is_digit(ch)) {
                ch -= '0';
                number = (number * 10) + ch;
                ch = next_player_char();
            }
            // If not a valid digit, end of number has been reached.
            // Back up pointer so non-digit char is processed as a note/command.
            // Advance to state that will utilize numeric parameter
            player_ptr--;
            state = next_state;
            continue;
        }

        // SET OCTAVE state
        // Entered after the octave value (single digit #) has been fetched.
        // Sets <octave> to the value specified.

        else if (state == STATE_SET_OCTAVE) {
            octave = digit;
            state = STATE_GET_NOTE;
            continue;
        }

        // SET NOTE RATIO state
        // Entered after the note ratio (single-digit #) has been fetched.
        // Sets <note_rest_ratio> to the value specified.

        else if (state == STATE_SET_NOTE_RATIO) {
            // Bound ratio to maximum
            if (digit > 8) {
                digit = 8;
            }
            // Set note/rest ratio and resume note/command scanning
            note_rest_ratio = digit;
            state = STATE_GET_NOTE;
            continue;
        }

        // SET VOLUME state
        // Entered after the volume level (single-digit #) has been fetched.
        // Sets the programmable gain amp to the gain value (0-7) specified.

        else if (state == STATE_SET_VOLUME) {
            // If specified volume is 0, mute speaker
            if (digit == 0) {
                beep_mute(1);
            }
            // Volume > 0, set PGA to selected volume
            else {
                digit--;                // 1..8 -> 0..7
                if (digit > 7) {        // Bound to maximum setting
                    digit = 7;
                }
                beep_gain(digit);       // Set volume
                beep_mute(0);           // Un-mute speaker
            }
            // Resume note/command scanning
            state = STATE_GET_NOTE;
            continue;
        }

        // SET TRANSPOSITION state
        // Entered after the transposition factor (multi-digit #) has been
        // fetched.  Sets the note transposition factor to the value
        // specified.

        else if (state == STATE_SET_TRANSPOSITION) {
            // Set new transposition value if it is within a one-octave range.
            // Otherwise, force it to 0
            if (number < NOTES_PER_OCTAVE) {
                transposition = number;
            }
            else {
                transposition = 0;
            }
            // Resume note/command scanning
            state = STATE_GET_NOTE;
            continue;
        }

        // SET KEY state

        else if (state == STATE_SET_KEY) {
            for (digit = 0; digit < 7; digit++) {
                scale[digit] = pgm_read_byte(&c_major_scale[digit]);
            }
            accidental = ACCIDENTAL_SHARP;  // Default: Sharps
            state = STATE_SET_KEY_2;
            continue;
        }

        // SET KEY 2 state

        else if (state == STATE_SET_KEY_2) {
            ch = next_player_char();
            if (is_note(ch)) {
                ch -= 'A';
                digit = pgm_read_byte(&c_major_scale[ch]);
                digit += accidental;
                scale[ch] = digit;
                continue;
            }
            else if (is_flat(ch)) {
                accidental = ACCIDENTAL_FLAT;
                continue;
            }
            else if (is_natural(ch)) {
                accidental = ACCIDENTAL_NATURAL;
                continue;
            }
            else if (is_sharp(ch)) {
                accidental = ACCIDENTAL_SHARP;
                continue;
            }
            player_ptr--;
            state = STATE_GET_NOTE;
            continue;
        }

        // SET TEMPO state
        // Entered after the beat unit (WHQISY) has been fetched, but before
        // the beats/min parameter is fetched.

        else if (state == STATE_SET_TEMPO) {
            // Upon entry to this state, the beat unit <note_size> has been
            // fetched.  Now fetch beats/min parameter.
            state = STATE_GET_NUMBER;
            next_state = STATE_SET_TEMPO_2;
            continue;
        }

        // SET TEMPO 2 state
        // Entered following the fetch of the beat unit (WHQISY) and beats/min
        // (multi-digit #) specification.
        // Sets <whole_note_period> based on this information, on which all
        // note timing is based.

        else if (state == STATE_SET_TEMPO_2) {
            // Use 120 BPM if no BPM rate specified
            if (!number) {
                number = DEFAULT_TEMPO;
            }
            // Both note unit (<note_size>, <size_modifier>) and beats/min
            // (<number>) parameters have been fetched
            // Calculate whole note period based on these parameters
            whole_note_period =
                (uint16_t) (PLAYER_TICKS_PER_SECOND * 60U * (uint32_t) note_size / number);
           // Add 50% to beat time if dotted
            if (size_modifier & MOD_DOTTED) {
                whole_note_period += whole_note_period >> 1;
            }
            // One-thrid of beat time if tripleted
            if (size_modifier & MOD_TRIPLET) {
                whole_note_period /= 3;
            }
            // Resume note/command scanning
            state = STATE_GET_NOTE;
            continue;            
        }

        // SET BOOKMARK state
        // Entered following the fetch of the bookmark number, but before
        // the repeat count is fetched.

        else if (state == STATE_SET_BOOKMARK) {
            // Upon entry to this state, the bookmark # has been fetched
            // and is in <digit>.
            // Set up to fetch repeat count
            state = STATE_GET_NUMBER;
            // After repeat count is fetched, finalize bookmark setting
            next_state = STATE_SET_BOOKMARK_2;
            continue;
        }

        // SET BOOKARMK 2 state
        // Entered following the fetch of the bookmark number and repeat count
        // The current play string pointer along with the specified repeat
        // count are stored in the specified bookmark record.  This information
        // is used by the GOTO BOOKMARK state/command.

        else if (state == STATE_SET_BOOKMARK_2) {
            // Both bookmark # <digit> and repeat count <number> have
            // been fetched.  Set bookmark based on these parameters,
            // along with the present value of <player_ptr>.
            if (number > 0xFE) {        // Bound repeat count to 8-bit max - 1
                number = 0xFE;
            }
            if (! number) {             // Repeat count of 0 means "infinite"
                number = 0xFF;
            }
            bookmark[digit].repeat = number;
            bookmark[digit].position = player_ptr;
            // Resume note/command scanning
            state = STATE_GET_NOTE;
            continue;
        }

        // GOTO BOOKMARK state
        // Enered following the fetch of a bookmark # (digit)
        // Sets playback pointer to the specified bookmark location if the
        // bookmark's repeat counter is nonzero.

        else if (state == STATE_GOTO_BOOKMARK) {
            // Bookmark # has been fetched
            // Jump to bookmark position if:
            //   - It is defined (e.g. not NULL)
            //   - Bookmark repeat count is not 0
            if (bookmark[digit].repeat && bookmark[digit].position) {
                // Decrement repeat count if it is not 'infinite'
                if (bookmark[digit].repeat != 0xFF) {
                    bookmark[digit].repeat--;
                }
                player_ptr = bookmark[digit].position;
            }
            // Resume note/command processing at new position
            state = STATE_GET_NOTE;
            continue;
        }

        // STOP state
        // This state will be entered when the end of the play string is
        // encountered or a parsing error occurs.

        else if (state == STATE_STOP) {
            player_stop();
            state = STATE_RESET;
            break;
        }

        // Unknown state - force to STOP

        else {
            state = STATE_STOP;
            continue;
        }
    } while (1);
}

/*******************************************************************************
Notes and note modifiers:

A..G            Base note to play; marks start of new note
R or ,          Rest; marks start of new note
+ # = N -       Accidentals: Sharp (+,#), Natural (=,N) or Flat (-)
0..9            Octave
W H Q I S Y     Note times: [W]hole [H]alf [Q]uarter e[I]ghth [S]ixteenth
                thirt[Y]seconth
.               Dotted: Adds 50% to note duration
/               Triplet: Note time is divided by 3
| or _          Tied: Ignore note/rest ratio, play note for entire note time
^               Staccato: Ignore note/rest ratio, play note for 1/4 note time
                  (e.g. temporarily set note/rest ratio to 2)

Note time and octave are persistent; if not explicity specified for a given
note, the octave and note time of the previous note played will be used.

Accidentals do not persist; they affect only the note specification they
are immediately associated with.

Note modifiers (dotted, triplet, tie, staccato) are semi-persistent; they
remain in effect for subsequent notes unless a note length specifier (WHQISY)
is encountered, in which case all modifiers are cleared.  For this reason,
such modifiers should be specified for a note AFTER the base note time -
for example, "CQ." will play a dotted quarter note, whereas "C.Q" will play
a simple quarter note - the dotting is ignored.

--------------------------------------------------------------------------------

Scoring commands
These do not result in note playback, but control how the player operates

: or <space>    NOOP: Marks end of note or command specification (optional)

!               Repeat last note played (including accidentals and modifiers)

<               Decrement default octave by 1

>               Increment default octave by 1

On              Set default octave for subsequent notes to <n>

Mn              Set note-to-rest ratio to <n>/8
                0 = Rest for entire note time, no note will be played
                1 = Play note for 1/8 time, rest 7/8 of time
                ...
                7 = Play note for 7/8 time, rest 1/8 of time
                8 = Play note for entire note time, no rest between notes
                9 = Same as 8

Tx:yyy:         Set tempo: x=Beat unit (WHQISY), yyy=beats/minute
                e.g. "TQ:90:" Quarter-note gets 1 beat, 90 beats/minute

Pnn:            Set transposition, nn=# half-steps to transpose up
                e.g. "P11:" Transpose all subsequent notes up 11 half-steps

Knnn...:        Set key, nnn... is a list of notes and accidentals
                To set key to A major (F# minor): "K+FCG:" (F, C & G are sharped)
                To set key to Eb major (C minor): "K-BEA:" (B, E & A are flatted)
                "K:" will reset key to C major (no flats/sharps)
                Accidentals may occur anywhere in the string; e.g. "K-C+G:" will
                flatten C and sharpen G.

Vn              Set volume to n (0-7)
                e.g. "V5:" Set volume to 5

[n:rrr:         Set bookmark <n> (0-9) with repeat count <r>
                e.g. "[3:12:" Set bookmark #3, repeat up to 12 times

]n              Decrements bookmark counter <n> and jumps to selected bookmark
                if its repeat count (before decrementing) is > 0

*               Reset player

*******************************************************************************/
