/*------------------------------------------------------------------------------
Name:       nixie.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       29-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    Nixie display driver routines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
Digit/segment layout for TubeClock - segement data array mapping

Offset   |0               9| |10             19|   |21             30|   |32             41|   |43             52| |53             62| |63
Digit    |    Leftmost 0   | |        1        |   |        2        |   |        3        |   |        4        | |   Rightmost 5   |
Segment  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 L 0 1 2 3 4 5 6 7 8 9 A 0 1 2 3 4 5 6 7 8 9 R 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 B
Byte #   |      0      | |      1      | |      2      | |      3      | |      4      | |      5      | |      6      | |      7      |
Bit #    7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0

SEGMENT key:
0..9 = Nixie tube emitter
L,R = Left/Right neon lamp "decimal point"
A,B = Aux driver output, not connected (well, not at present)
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
Display character mode output controls:

Segment control (displayable characters):

0..9    Turn on segment 0-9 at cursor
A..I    Turn on segments 0 & 1..9
a..i    Same as A..I
<space> Turn off all segments at cursor

Note: All segment-output characters will auto-advance the cursor if cursor
auto-advance is enabled.  Previously-enabled segments at the cursor are
overwritten by the new segment pattern specified by the character unless
overlay mode is enabled, in which case the new segment pattern is ORed into
the existing one.

Neon lamp and aux segment control:

<       Turn on left neon lamp
>       Turn on right neon lamp
(       Turn off left neon lamp
)       Turn off right neon lamp
`       Turn off both neon lamps
.       Turn on neon lamp to left of cursor
,       Turn on neon lamp to right of cursor

X,x     Turn on,off AUX segment A
Y,y     Turn on,off AUX segment B

Intensity controls:

[       Decrease intensity of subsequent segments output by 1
]       Increase intensity of subsequent segments output by 1
*n      Set intensity of subsequent segments output to <n>.  n = '0'..'9'.
        Command is ignored if <n> is not an ASCII digit character.
~       Set intensity to nominal level

Cursor control:

$       Normal cursor mode, auto-advance after digit output
#       Static cursor mode, cursor stays at current position
&       Normal output mode, previous segments overwritten by new
|       Overlay mode, new segments overlay existing ones

@n      Move cursor to digit position <n>.  n = '0'..'6'.
        Command is ignored if <n> does not represent a valid display position.
        Note: Display position '6' is 'off the right edge'
{       Disable cursor wrap mode.  Cursor can advance past right side of display.
        If cursor is in the off-right-edge position, subsequent displayable
        characters will not be displayed.  Attempts to move the cursor past the
        left edge of the display will be ignored.        
}       Enable cursor wrap mode.  Cursor will move to leftmost digit if advanced
        past the rightmost digit, or move to rightmost digit if backed up past
        the leftmost digit.

Next character behavior-modification controls:

!       Next character does NOT advance cursor (single overwrite mode)
_       Next character will overlay existing segments (single overlay mode)
^       Move cursor left 1 digit, activate single overlay mode

Note: "Single" controls affect the next DISPLAYABLE (e.g. segment-controlling)
character.  Characters that perform control functions that occur in the output
stream following a single-mode control character will NOT turn off the
single-mode function.

Cursor movement, display clear, miscellaneous:

^L \f   Clear display, move cursor to leftmost digit
^M \r   Move cursor to leftmost digit
^J \n   Clear display but do not move cursor
^H \b   Move cursor to the left one digit (decrement)
^I \t   Move cursor to the right one digit (increment)
^K \v   Partial display reset - set intensity to nominal, turn off overlay mode
        (single and global), enable cursor auto-advance.  Does not affect
        cursor position or clear display.

Any character output that is not in the above list will be ignored.

--------------------------------------------------------------------------------

Stream control structure (object) heirarchy:

FILE        I/O stream object from stdio library
    *put        Points to output function
    *get        Points to input function
    flags       I/O mode flags
    *udata      Device-specific data pointer -> nixie_control_t
        *segdata    Pointer to display segment intensity data array
        cursor      Virtual display cursor position (next digit/tube to update)
        intensity   Intensity level to apply to next displayable character
        state       Controls how next character output is interpreted
        control     Virtual display control and mode flags -> control_t
            no_cursor_inc   Do not auto-increment cursor
            single_no_inc   Do not auto-increment cursor for next char
            overlay         Overlay new segment pattern onto existing pattern
            single_overlay  Overlay segments for next char only
            no_cursor_wrap  Do not wrap cursor when it goes off left or right side
    
------------------------------------------------------------------------------*/

#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <stdio.h>

#include "portdef.h"
#include "nixie.h"

//------------------------------------------------------------------------------

// Nixie display control & status flags

typedef union {
    uint8_t all;
    struct {
        uint8_t crossfade_rate  : 2;    // Rate at which crossfade occurs (0=fastest)
        uint8_t crossfade_count : 2;    // Counter used for crossfade timing
        uint8_t unused4         : 1;
        uint8_t one_cycle_done  : 1;    // One full refresh PWM cycle completed
        uint8_t one_cycle_only  : 1;    // Refresh only one full PWM cycle, then stop
        uint8_t refresh_enable  : 1;    // Display refresh enable
    };
} nixie_control_t;

//------------------------------------------------------------------------------

// Pointer to 'active' (displayed) nixie segment intensity pattern array
static uint8_t *nixie_segment_ptr;

// Nixie display control & status flags
static volatile nixie_control_t nixie_control = {0b10000000};

//------------------------------------------------------------------------------

// Segment/drive line offsets for start of each nixie tube display element
// (left to right)

static const uint8_t nixie_digit_offset[] PROGMEM =
  {0, 10, 21, 32, 43, 53, 20, 42, 31, 63};
// 0   1   2   3   4   5  LL  RL  AA  AB
// LL,RL = Left/Right lamp "decimal points"
// AA,AB = Aux output A/B
/******************************************************************************
 * nixie_display_refresh()
 *
 * Perform nixie display intensity modulation
 *
 * Inputs:  None
 *
 * Returns: Nothing
 *
 * Notes:   This routine is intended to be called from a periodically timed
 *          interrupt.  The interrupt routine must run at a rate fast enough
 *          to prevent display flicker.  To meet this goal, this function
 *          should be called at a rate no less than:
 *          30 * (MAX_NIXIE_INTENSITY + 1)
 *          times per second.  The '30' value is suggested as a minimum full
 *          display update rate (full updates per second).  This routine
 *          performs only one SUB-cycle of the display refresh per entry,
 *          and one full refresh cycle consists of (MAX_NIXIE_INTENSITY + 1)
 *          sub-cycles.  The display quality will suffer if the full-cycle
 *          refresh rate is much less than 30x/second, and it is suggested
 *          that a higher rate be used if possible, within the limits of
 *          available execution time.
 ******************************************************************************/

void nixie_display_refresh(void)
{
    static uint8_t intensity_count;     // Intensity counter, goes from 0..MAX_NIXIE_INTENSITY, incremented every entry
    register uint8_t segment_index;     // Iterator to index segment intensity array, 0..NIXIE_SEGMENTS
    register uint8_t nixie_data;        // Data accumulator, sent to SPI, 8 bits of segment on/off data
    register uint8_t bit_mask;          // ORed into nixie_data if a given segment should be ON

    // Exit if display refresh disabled

    if (!nixie_control.refresh_enable) {
        return;
    }

    // Set initial values for the segment bit-on mask and SPI data accumulator

    bit_mask = 0x01;
    nixie_data = 0x00;

    // Build and shift out via SPI 64 bits (NIXIE_SEGMENTS) worth of
    // nixie segment on/off data

    for (segment_index = 0; segment_index < NIXIE_SEGMENTS; segment_index++) {

        // A given nixie display segment should be ON for this PWM sub-cycle
        // if its intensity setting is greater than the current sub-cycle
        // count, which cycles from 0..MAX_NIXIE_INTENSITY and is incremented
        // on every entry into the ISR.

        if (nixie_segment_ptr[segment_index] > intensity_count) {
            // Turn nixie segment on for this PWM subcycle
            nixie_data |= bit_mask;
        }

        if (bit_mask & 0x80) {

            // 8 bits of segment data have been processed
            // Send the data to the display driver via SPI, and reset data
            // accumulator and bit-on mask for the next 8 bits of data to
            // send out.

            while (!(SPSR & BM(SPIF))); // Wait for previous byte to finish shifting out
            SPDR = nixie_data;
            bit_mask = 0x01;
            nixie_data = 0x00;
        }

        else {

            // Advance bit-on mask for the next display segment

            bit_mask <<= 1;
        }
    }

    // Display driver has all 64 bits of data it needs
    // Pulse latch-data pin on display driver

    while (!(SPSR & BM(SPIF))); // Wait for last byte to finish shifting out
    BSET(DRIVER_LATCH);
    BCLR(DRIVER_LATCH);

    // Increment intensity counter for next display PWM sub-cycle
    // When this count exceeds the max intensity setting, reset it to 0

    intensity_count++;
    if (intensity_count >= MAX_NIXIE_INTENSITY) {
        intensity_count = 0;
        nixie_control.one_cycle_done = 1;

        // Suspend display refresh if one-cycle-only mode in effect
        // (Used for crossfade synchronization)

        if (nixie_control.one_cycle_only) {
            nixie_control.refresh_enable = 0;
        }
   }

}

/******************************************************************************
 * nixie_display_enable(enable)
 *
 * Enable or disable nixie display
 *
 * Inputs:  enable      Nonzero (TRUE) to enable nixie display
 *
 * Returns: Nothing
 *
 * Notes:   Display is blanked and display refresh suspended if <enable> is
 *          0/FALSE.
 ******************************************************************************/

void nixie_display_enable(uint8_t enable)
{
    if (enable) {
        nixie_control.one_cycle_only = 0;
        nixie_control.refresh_enable = 1;
        BSET(DRIVER_ENABLE);
    }
    else {
        nixie_control.refresh_enable = 0;
        BCLR(DRIVER_ENABLE);
    }
}

/******************************************************************************
 * clear_nixie_display(*segdata)
 *
 * Reset all segment data to 0 (off), clearing the (virtual) display
 *
 * Inputs:  *segdata    Pointer to an array containing nixie display segment
 *                      intensity data.  All segment data in the array will be
 *                      reset to 0 (off).
 *
 * Returns: Nothing
 ******************************************************************************/

static void clear_nixie_display(uint8_t *segdata)
{
    uint8_t count;

    for (count = NIXIE_SEGMENTS; count; count--) {
        *segdata = 0;
        segdata++;
    }
}

/******************************************************************************
 * nixie_control_init(*control)
 *
 * Initialize control data for a nixie display stream
 *
 * Inputs:  *control    Pointer to a nixie_stream_t control structure
 *                      that holds control and state information for a virtual
 *                      display.  This is the structure pointed to by the
 *                      <udata> field in a nixie display FILE structure, and
 *                      used/updated by the nixie_out() function.
 *
 * Returns: Nothing
 ******************************************************************************/

static void nixie_control_init(nixie_stream_t *control)
{
    clear_nixie_display(control->segdata);
    control->cursor = 0;
    control->intensity = MAX_NIXIE_INTENSITY;
    control->control.all = 0;
    control->state = NORMAL_OUTPUT;
}

/******************************************************************************
 * nixie_stream_init(*stream, *control, *segdata)
 *
 * Initialize a stdio FILE object for use as a nixie virtual display
 * output stream
 *
 * Inputs:  *stream     Pointer to a FILE object (as defined in stdio.h) that
 *                      will be used as a nixie virtual display output stream.
 *                      Following initialization, the FILE object can be passed
 *                      by any stdio fxxx(FILE, ...) or fxxx_P(FILE, ....)
 *                      function to output data to a virtual display.
 *          *control    Points to a nixie_stream_t object to be associated
 *                      with the <stream>.  The <control> object is used by
 *                      nixie_out(), and contains the control data needed to
 *                      implement the virtual display.
 *          *segdata    Points to an array of 64 (NIXIE_SEGMENTS) byte values
 *                      which represent the segment intensity levels of each
 *                      display emitter or visual element.  Data in this array
 *                      is manipulated based on the character stream received and
 *                      processed by nixie_out().  If the virtual display is
 *                      mapped to the physical display via nixie_show_stream(),
 *                      this data is used by nixie_display_refresh() to control
 *                      physical display segment intensity.
 *
 * Returns: Nothing
 *
 * Notes:   The caller must declare and allocate memory for the above objects,
 *          then pass pointers to them to this function, which will
 *          initialize them and link them together to build a FILE object
 *          that can be used by the stdio library output functions.
 ******************************************************************************/

void nixie_stream_init(FILE *stream, nixie_stream_t *control, uint8_t *segdata)
{
    stream->put = &nixie_out;
    stream->get = NULL;
    stream->flags = _FDEV_SETUP_WRITE;
    stream->udata = control;

    ((nixie_stream_t *) (stream->udata))->segdata = segdata;

    nixie_control_init(control);
}

/******************************************************************************
 * nixie_show_stream(*stream)
 *
 * Set nixie output stream to present on physical display
 *
 * Inputs:  *stream     Pointer to a FILE object that has been initialized
 *                      by nixie_stream_init().  The <stream> specified will
 *                      be shown on the physical nixie display.
 *
 * Returns: Nothing
 ******************************************************************************/

void nixie_show_stream(FILE *stream)
{
    nixie_segment_ptr = ((nixie_stream_t *) stream->udata)->segdata;
}

/******************************************************************************
 * nixie_crossfade(*to_stream)
 *
 * Cross-fade display from the present displayed state to the <to_stream> state
 *
 * Inputs:  to_stream   Pointer to a FILE object that has been initialized
 *                      by nixie_stream_init() that represents the display
 *                      state that should be crossfaded TO.
 *
 * Returns: Nothing
 *
 * Notes:   The display that is presently being shown on the physical display
 *          will be slowly changed or 'morphed' into the display state
 *          contained in the to_stream.  Segments that are on in the
 *          displayed stream will be faded down to off, while segments that
 *          are on in the to_stream will be faded up to the intensity level
 *          specified by the segment control data in the to_stream.  This
 *          creates a crossfading effect that is visually appealing and less
 *          'abrupt' than a sudden state change.
 *
 *          This routine will 'block' (not return) until the physical display
 *          has attained the state specified by the to_stream.  Depending on
 *          the state of the new (to_stream) and old displays, this can take
 *          several hundred milliseconds to complete.
 ******************************************************************************/

void nixie_crossfade(FILE *to_stream)
{
    register uint8_t *p_from;
    register uint8_t *p_to;
    register uint8_t activity;
    register uint8_t index;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        nixie_control.one_cycle_only = 1;
        nixie_control.one_cycle_done = 0;
        nixie_control.crossfade_count = 3;
    }

    do {
        // Set up for next crossfade pass
        // Reset pointers to segment data arrays

        p_from = nixie_segment_ptr;
        p_to = ((nixie_stream_t *) to_stream->udata)->segdata;

        // Wait for one display PWM cycle to complete
        //
        // With one_cycle_only in effect, refresh is suspended after one full
        // PWM cycle has completed.

        while (! nixie_control.one_cycle_done) ;
        nixie_control.one_cycle_done = 0;

        // Perform crossfade intensity adjustment only every other cycle if
        // slow crossfade mode is enabled

        if (nixie_control.crossfade_count < nixie_control.crossfade_rate) {
            nixie_control.refresh_enable = 1;
            nixie_control.crossfade_count++;
            activity = 1;
            continue;
        }

        // Fade segments that are ON in "to" display up 
        // Fade segments that are OFF in "to" display down

        activity = 0;
        for (index = 0; index < NIXIE_SEGMENTS; index++) {
            if (*p_to) {            // Is segment in new display on ?
                if (*p_from < *p_to) {
                    // Fade segment up to new intensity level
                    (*p_from)++;
                    activity++;
                }
            }
            else if (*p_from) {     // Is segment that is supposed to be off still on?
                // Fade segment down to off
                (*p_from)--;
                activity++;
            }

            // Point to next segment to work on

            p_from++;
            p_to++;
        }

        // One crossfade cycle completed, perform another PWM cycle

        nixie_control.refresh_enable = 1;
        nixie_control.crossfade_count = 0;
    } while (activity);

    // Crossfading complete, enable normal display refresh

    nixie_control.one_cycle_only = 0;
}

/******************************************************************************
 * nixie_crossfade_rate(rate)
 *
 * Set rate at which display is crossfaded when using nixie_crossfade()
 *
 * Inputs:  rate    Rate at which display is crossfaded.  0 is fastest,
 *                  the maximum value of 3 is slowest.
 *
 * Returns: Nothing
 ******************************************************************************/

void nixie_crossfade_rate(uint8_t rate)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        nixie_control.crossfade_rate =
            (rate <= MAX_NIXIE_CROSSFADE_RATE) ? rate : MAX_NIXIE_CROSSFADE_RATE;
    }
}

/******************************************************************************
 * clear_nixie_digit(*segdata, digit)
 *
 * Turn off all segments in the selected display tube (digit)
 *
 * Inputs:  *segdata    Pointer to an array containing nixie display segment
 *                      intensity data for all 64 available display segments
 *          digit       The digit (nixie tube) to clear by turning off (setting
 *                      to 0) all segments associated with it.
 *
 * Returns: Nothing
 *
 * Notes:   For 'normal' nixie digits (tubes) 0-5, the 10 segments associated
 *          with the selected display position will be turned off.  For pseudo-
 *          digits such as the neon lamp decimal points, only one segment
 *          will be turned off.
 ******************************************************************************/

static void clear_nixie_digit(uint8_t *segdata, uint8_t digit)
{
    uint8_t count;

    // If clearing a normal digit/tube, clear 10 segments.
    // For pseudo-digits such as the neon lamp "decimal points", turn off
    // only one segment.

    count = (digit < NIXIE_DISPLAY_WIDTH) ? NIXIE_SEGMENTS_PER_DIGIT : 1;

    // Get segment offset for selected digit/tube

    segdata += pgm_read_byte(&nixie_digit_offset[digit]);

    // Turn off segment(s) in selected digit

    for ( ; count; count--) {
        *segdata = 0;
        segdata++;
    }
}

/******************************************************************************
 * set_nixie_segment(*segdata, digit, segment, intensity)
 *
 * Set intensity of a single display segment
 *
 * Inputs:  *segdata    Pointer to an array containing nixie display segment
 *                      intensity data for all 64 available display segments
 *          digit       The digit, (tube number) of the segment to be set
 *          segment     The segment within the selected digit position to
 *                      be set.  Segment numbers 0..9 correspond one-to-one with
 *                      the nixie tube emitters that form the digits 0..9.
 *          intensity   The intensity level that the selected segment should be
 *                      driven at. 0=Off, 9 (MAX_NIXIE_INTENSITY) is full on.
 *
 * Returns: Nothing
 ******************************************************************************/

static void set_nixie_segment(uint8_t *segdata, uint8_t digit, uint8_t segment, uint8_t intensity)
{
    segdata += pgm_read_byte(&nixie_digit_offset[digit]) + segment;
    *segdata = intensity;
}

/******************************************************************************
 * inc_cursor(*control, char_mode)
 *
 * Move the cursor (next digit/display element to update) forward (leftward)
 *
 * Inputs:  *control    Pointer to a nixie virtual display control structure.
 *                      The <cursor> field in this structure will be
 *                      (conditionally) incremented.
 *          char_mode   If nonzero, it is assumed that the cursor increment
 *                      request follows a displayable character output
 *                      operation.  A zero value signifies that the increment
 *                      request is in response to a move-cursor-forward
 *                      control operation.
 *
 * Returns: Nothing
 *
 * Notes:   The value set in the <cursor> field is typically set to +1 of its
 *          prior value, but this behavior can be change depending on several
 *          factors, such as if the cursor is pointing to the end (right side)
 *          of the display (it will wrap to the left side, if enabled) or if
 *          cursor autoincrement is disabled.
 ******************************************************************************/

static void inc_cursor(nixie_stream_t *control, uint8_t char_mode)
{
    if (char_mode) {
        if (control->control.no_cursor_inc || control->control.single_no_inc) {
            control->control.single_no_inc = 0;
            return;
        }
    }

    control->cursor++;

    if (control->control.no_cursor_wrap) {
        if (control->cursor > NIXIE_DISPLAY_WIDTH) {
            control->cursor = NIXIE_DISPLAY_WIDTH;
        }
    }
    else if (control->cursor >= NIXIE_DISPLAY_WIDTH) {
        control->cursor = 0;
    }
}

/******************************************************************************
 * dec_cursor(*control)
 *
 * Move the cursor (next digit/display element to update) forward (leftward)
 *
 * Inputs:  *control    Pointer to a nixie virtual display control structure.
 *                      The <cursor> field in this structure will be
 *                      (conditionally) decremented.
 *
 * Returns: Nothing
 *
 * Notes:   The value set in the <cursor> field is typically set to -1 of its
 *          prior value, but this behavior can be change depending on several
 *          factors, such as if the cursor is pointing to the start (left side)
 *          of the display (it will wrap to the right side, if enabled)
 ******************************************************************************/

static void dec_cursor(nixie_stream_t *control)
{
    control->cursor--;

    if (control->cursor & 0x80) {
        if (control->control.no_cursor_wrap) {
            control->cursor = 0;
        }
        else {
            control->cursor = NIXIE_DISPLAY_WIDTH - 1;
        }
    }
}

/******************************************************************************
 * int16_t nixie_out(ch, *stream)
 *
 * Output a character to a nixie virtual display stream
 * stdio library compliant
 *
 * Inputs:  ch          Character to output to the selected <stream>.
 *                      Various control characters as well as "displayable"
 *                      characters are recognized and interpreted, as
 *                      documented in the comments at the start of this
 *                      source file.
 *          *stream     Pointer to a FILE structure that has been initialized
 *                      using nixie_stream_init().  Specifies the virtual
 *                      display to be updated/written to.
 *
 * Returns: 0 in all cases.  Return value is provided to make this function
 *          compliant with the expectations of the stream-output functions
 *          (such as fprintf()) in the stdio library.
 *
 * Notes:   The goal behind the design of this function, and the functions that
 *          support it, is to make control of the nixie display as simple and
 *          as similar to output to a terminal or similar character-mode output
 *          device.  The full features and capabilities offered by the stdio
 *          library may be used with this function serving as the basic
 *          character output "device driver".
 *
 *          The character(s) recognized as input to this function, and the
 *          actions that they perform are documented in the comments that
 *          can be found near the start of this source file.
 ******************************************************************************/

int16_t nixie_out(char ch, FILE *stream)
{
    register nixie_stream_t *p;
    uint8_t char_type;

    p = stream->udata;

    // If previous command character requires a parameter digit, interpret
    // the next character as a parameter and set value according to previous
    // character sent.

    if (p->state != NORMAL_OUTPUT) {
        ch -= '0';

        if (p->state == SET_INTENSITY) {
            if (ch <= MAX_NIXIE_INTENSITY) {
                p->intensity = ch;
            }
        }
 
        else if (p->state == SET_CURSOR_POS) {
            if (ch <= NIXIE_DISPLAY_WIDTH) {
                p->cursor = ch;
            }
        }

        p->state = NORMAL_OUTPUT;
        return 0;
    }

    // Determine if character is displayable
    // char_type = 0 : Not displayable
    // char_type = 1 : Single digit
    // char_type = 2 : Single digit plus '0' segment
    // char_type = 3 : Space

    char_type = 0;
    if ((ch >= '0') && (ch <= '9'))  {
        char_type = 1;
        ch -= '0';
    }
    else if ((ch >= 'A') && (ch <= 'I')) {
        char_type = 2;
        ch -= 'A';
    }
    else if ((ch >= 'a') && (ch <= 'i')) {
        char_type = 2;
        ch -= 'a';
    }
    else if (ch == ' ') {
        char_type = 3;
    }

    // If character is displayable, turn on appropriate segment(s)
    // on the nixie display

    if (char_type) {
        if (p->cursor < NIXIE_DISPLAY_WIDTH) {
            if (! (p->control.overlay || p->control.single_overlay)) {
                clear_nixie_digit(p->segdata, p->cursor);
            }

            if (char_type == 2) {
                set_nixie_segment(p->segdata, p->cursor, 0, p->intensity);
                ch++;
            }
            if (char_type != 3) {
                set_nixie_segment(p->segdata, p->cursor, ch, p->intensity);
            }
        }

        inc_cursor(p, 1);
        p->control.single_overlay = 0;
        return 0;
    }

    // Character is not displayable
    // Check for valid control characters

    switch(ch) {
        case '<' :                      // Turn on left neon lamp
            set_nixie_segment(p->segdata, NIXIE_LEFT_LAMP, 0, p->intensity);
            break;
 
        case '>' :                      // Turn on right neon lamp
            set_nixie_segment(p->segdata, NIXIE_RIGHT_LAMP, 0, p->intensity);
            break;

        case '(' :                      // Turn off left neon lamp
            set_nixie_segment(p->segdata, NIXIE_LEFT_LAMP, 0, 0);
            break;

        case ')' :                      // Turn off right neon lamp
            set_nixie_segment(p->segdata, NIXIE_RIGHT_LAMP, 0, 0);
            break;

        case '`' :                      // Turn off both neon lamps
            set_nixie_segment(p->segdata, NIXIE_LEFT_LAMP, 0, 0);
            set_nixie_segment(p->segdata, NIXIE_RIGHT_LAMP, 0, 0);
            break;

        case '.' :                      // Turn on lamp to left of cursor
            if ((p->cursor == 2) || (p->cursor == 3)) {
                set_nixie_segment(p->segdata, NIXIE_LEFT_LAMP, 0, p->intensity);
            }
            else if (p->cursor > 3) {
                set_nixie_segment(p->segdata, NIXIE_RIGHT_LAMP, 0, p->intensity);
            }
            break;

        case ',' :                      // Turn on lamp to right of cursor
            if ((p->cursor == 0) || (p->cursor == 1)) {
                set_nixie_segment(p->segdata, NIXIE_LEFT_LAMP, 0, p->intensity);
            }
            else if (p->cursor < 4) {
                set_nixie_segment(p->segdata, NIXIE_RIGHT_LAMP, 0, p->intensity);
            }
            break;

        case 'X' :                      // Turn on AUX output A
            set_nixie_segment(p->segdata, NIXIE_AUX_A, 0, p->intensity);
            break;

        case 'x' :                      // Turn off AUX output A
            set_nixie_segment(p->segdata, NIXIE_AUX_A, 0, 0);
            break;

        case 'Y' :                      // Turn on AUX output B
            set_nixie_segment(p->segdata, NIXIE_AUX_B, 0, p->intensity);
            break;

        case 'y' :                      // Turn off AUX output B
            set_nixie_segment(p->segdata, NIXIE_AUX_B, 0, 0);
            break;


        case '[' :                      // Decrease intensity by 1
            if (p->intensity) {
                p->intensity--;
            }
            break;

        case ']' :                      // Increase intensity by 1
            if (p->intensity < MAX_NIXIE_INTENSITY) {
                p->intensity++;
            }
            break;

        case '*' :                      // Set intensity to following digit
            p->state = SET_INTENSITY;
            break;

        case '~' :                      // Set intensity to max/nominal
            p->intensity = NOMINAL_NIXIE_INTENSITY;
            break;

        case '$' :                      // Enable cursor auto-increment
            p->control.no_cursor_inc = 0;
            break;

        case '#' :                      // Disable cursor auto-increment
            p->control.no_cursor_inc = 1;
            break;

        case '!' :                      // Next char does not auto-inc cursor
            p->control.single_no_inc = 1;
            break;

        case '&' :                      // Disable overlay mode
            p->control.overlay = 0;
            break;

        case '|' :                      // Enable overlay mode
            p->control.overlay = 1;
            break;

        case '_' :                      // Next char will overlay
            p->control.single_overlay = 1;
            break;

        case '^' :                      // Dec cursor, next char overlays
            dec_cursor(p);
            p->control.single_overlay = 1;
            break;

        case '@' :                      // Set cursor to absolute position
            p->state = SET_CURSOR_POS;
            break;

        case '{' :                      // Disable cursor auto-wraparound
            p->control.no_cursor_wrap = 1;
            break;

        case '}' :                      // Enable cursor auto-wraparound
            p->control.no_cursor_wrap = 0;
            break;

        case '\f' :                     // Clear display, cursor to left
            clear_nixie_display(p->segdata);
            p->cursor = 0;
            break;

        case '\r' :                     // Move cursor to leftmost digit
            p->cursor = 0;
            break;

        case '\n' :                     // Clear display
            clear_nixie_display(p->segdata);
            break;

        case '\b' :                     // Move cursor left 1 digit
            dec_cursor(p);
            break;

        case '\t' :                     // Move cursor right 1 digit
            inc_cursor(p, 0);
            break;

        case '\v' :                     // Partial display init
            p->intensity = MAX_NIXIE_INTENSITY;
            p->cursor = 0;
            p->control.all = 0;
            break;
    }

    return 0;
}
