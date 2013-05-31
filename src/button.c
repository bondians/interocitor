/*------------------------------------------------------------------------------
Name:       button.c
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

#include <inttypes.h>
#include <avr/io.h>
#include <util/atomic.h>

#include "portdef.h"
#include "timer.h"
#include "button.h"

//------------------------------------------------------------------------------

// Enable flag for button scanning routine button_scan()

static uint8_t button_scan_enable;

// Undebounced button status
static volatile button_t button_state;

// Debounced button status
static volatile button_t button_debounced;

// Stable button pattern held for > BUTTON_CHORD_DELAY
static volatile button_t button_chord;

// Latched buttons pressed, set only once per press
static volatile button_t button_pressed;

// Latched buttons released, set only once per release
static volatile button_t button_released;

// Latched buttons pressed > BUTTON_SHORT_DELAY, set when button(s) released
static volatile button_t button_short;

// Latched buttons pressed > BUTTON_LONG_DELAY, set after button held long enough
static volatile button_t button_long;

// Un-debounced button state from last button_scan()
static volatile button_t button_previous;

// Timer used to determine when a button chord has been held long enough
// Reset whenever button press pattern changes
static volatile uint16_t button_stable;

// Button debounce timers, one per button
static volatile uint16_t button_timer[NUM_BUTTONS];

/******************************************************************************
 * reset_buttons()
 *
 * Reset all button registers & timers
 *
 * Inputs:  None
 *
 * Returns: Nothing
 ******************************************************************************/

void reset_buttons(void)
{
    uint8_t index;

    // Disable button scanning while resetting

    button_scan_enable = 0;

    // Reset all button-down timers

    for (index = 0; index < NUM_BUTTONS; index++) {
        button_timer[index] = 0;
    }

    // Clear all button status registers

    button_state.all = 0;
    button_debounced.all = 0;
    button_chord.all = 0;
    button_pressed.all = 0;
    button_released.all = 0;
    button_short.all = 0;
    button_long.all = 0;
    button_previous.all = 0;

    // Enable button scanning

    button_scan_enable = 1;
}

/******************************************************************************
 * button_enable(enable)
 *
 * Enable or disable button scanning
 *
 * Inputs:  None
 *
 * Returns: Nothing
 ******************************************************************************/

void button_enable(uint8_t enable)
{
    button_scan_enable = enable;
}

/******************************************************************************
 * button_t read_button_state()
 *
 * Read instantaneous non-debounced button states
 *
 * Inputs:  None
 *
 * Returns: Bitmap indicating which buttons are presently pressed
 ******************************************************************************/

button_t read_button_state(void)
{
    return button_state;
}

/******************************************************************************
 * button_t read_button_debounced()
 *
 * Read debounced button states
 *
 * Inputs:  None
 *
 * Returns: Bitmap indicating button(s) that have been pressed for longer than
 *          BUTTON_SHORT_DELAY
 *
 * Note:    The return value of this function is NOT latched; it reflects the
 *          actual state of the buttons as read_button_state() does, except
 *          that the button presses reflected in the return value have been
 *          debounced.
 ******************************************************************************/

button_t read_button_debounced(void)
{
    return button_debounced;
}

/******************************************************************************
 * button_t read_button_chord()
 *
 * Read value reflecting button pattern that has been held unchanged for longer
 * than BUTTON_CHORD_DELAY
 *
 * Inputs:  None
 *
 * Returns: Bitmap indicating button pattern that has been held unchanged for
 *          longer than BUTTON_CHORD_DELAY.
 *
 * Note:    The button chord status is latched, but can be overwritten if a
 *          new chord is registered before a previously-qualified chord is
 *          read.
 ******************************************************************************/

button_t read_button_chord(void)
{
    return button_chord;
}

/******************************************************************************
 * button_t reset_button_chord()
 *
 * Read value reflecting button pattern that has been held unchanged for longer
 * than BUTTON_CHORD_DELAY.  Resets the chord status after it has been read.
 *
 * Inputs:  None
 *
 * Returns: Bitmap indicating button pattern that has been held unchanged for
 *          longer than BUTTON_CHORD_DELAY.
 *
 * Note:    The button chord status is latched, but can be overwritten if a
 *          new chord is registered before a previously-qualified chord is
 *          read.  This function will reset the button chord latch after
 *          reading it.
 ******************************************************************************/

button_t reset_button_chord(void)
{
    button_t temp;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        temp = button_chord;
        button_chord.all = 0;
    }

    return temp;
}

/******************************************************************************
 * button_t read_buttons_pressed()
 *
 * Read the latched state of the buttons that have/had been down for longer
 * than BUTTON_SHORT_DELAY.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons pressed for longer than BUTTON_SHORT_DELAY
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been
 *          pressed will not be reset after the button(s) are released.  Use
 *          the function reset_buttons_pressed() to clear the button pressed
 *          latch.
 ******************************************************************************/

button_t read_buttons_pressed(void)
{
    return button_pressed;
}

/******************************************************************************
 * button_t reset_buttons_pressed()
 *
 * Read the latched state of the buttons that have/had been down for longer
 * than BUTTON_SHORT_DELAY.  Resets the latch after it has been read.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons pressed for longer than BUTTON_SHORT_DELAY
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been
 *          pressed will not be reset after the button(s) are released.
 *          This function resets the button pressed latch after reading it.
 ******************************************************************************/

button_t reset_buttons_pressed(void)
{
    button_t temp;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        temp = button_pressed;
        button_pressed.all = 0;
    }

    return temp;
}

/******************************************************************************
 * button_t read_buttons_released()
 *
 * Read the latched state of the buttons that have been down for longer than
 * BUTTON_SHORT_DELAY but have subseqently been released.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons that have been released after having been
 *          pressed for longer than BUTTON_SHORT_DELAY
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been
 *          released will not be reset, even if the same button(s) are pressed
 *          again.  Use the function reset_buttons_released() to clear the
 *          button-released latch.
 ******************************************************************************/

button_t read_buttons_released(void)
{
    return button_released;
}

/******************************************************************************
 * button_t reset_buttons_released()
 *
 * Read the latched state of the buttons that have been down for longer than
 * BUTTON_SHORT_DELAY but have subseqently been released.  Resets the latch
 * after it has been read.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons released
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been
 *          released will not be reset, even if the same buttons are pressed
 *          again.  This function resets the buttons-released latch after
 *          reading it.
 ******************************************************************************/

button_t reset_buttons_released(void)
{
    button_t temp;
     
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        temp = button_released;
        button_released.all = 0;
    }

    return temp;
}

/******************************************************************************
 * button_t read_short_buttons()
 *
 * Read the latched value of the button(s) that have been held down for longer
 * than BUTTON_SHORT_DELAY, but have been released before being held down for
 * longer than BUTTON_LONG_DELAY.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons held down longer than SHORT_BUTTON_DELAY
 *          but released before being held down for LONG_BUTTON_DELAY.
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have
 *          qualified as a "short" press-and-release will not be reset, even
 *          if a subsequent button press-and-release occurs again.  Use the
 *          function reset_short_buttons() to clear the short button press-
 *          and-release latch.
 ******************************************************************************/

button_t read_short_buttons(void)
{
    return button_short;
}

/******************************************************************************
 * button_t reset_short_buttons()
 *
 * Read the latched value of the button(s) that have been held down for longer
 * than BUTTON_SHORT_DELAY, but have been released before being held down for
 * longer than BUTTON_LONG_DELAY.  Clears the latch after reading it.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons held down longer than SHORT_BUTTON_DELAY
 *          but released before being held down for LONG_BUTTON_DELAY.
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have
 *          qualified as a "short" press-and-release will not be reset, even
 *          if a subsequent button press-and-release occurs again.
 *          This function clears the short button press-and-release latch
 *          after reading it.
 ******************************************************************************/

button_t reset_short_buttons(void)
{
    button_t temp;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        temp = button_short;
        button_short.all = 0;
    }

    return temp;
}

/******************************************************************************
 * button_t read_long_buttons()
 *
 * Read the latched value of the button(s) that have been held down for longer
 * than BUTTON_LONG_DELAY.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons that have been held down for longer than
 *          BUTTON_LONG_DELAY.
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been held
 *          down longer than BUTTON_LONG_DELAY will not be reset, even if the
 *          button(s) are subsequently released.  Use the function
 *          reset_long_buttons() to clear the long-button-recognized latch.
 ******************************************************************************/

button_t read_long_buttons(void)
{
    return button_long;
}

/******************************************************************************
 * button_t reset_long_buttons()
 *
 * Read the latched value of the button(s) that have been held down for longer
 * than BUTTON_LONG_DELAY.  Clears the latch after reading it.
 *
 * Inputs:  None
 *
 * Returns: Latched state of buttons that have been held down for longer than
 *          BUTTON_LONG_DELAY.
 *
 * Notes:   The button states reflected in the return value are "latched",
 *          meaning that the bit(s) indicating that buttons that have been held
 *          down longer than BUTTON_LONG_DELAY will not be reset, even if the
 *          button(s) are subsequently released.  This function clears the
 *          long-button-recognized latch after reading it.
 ******************************************************************************/

button_t reset_long_buttons(void)
{
    button_t temp;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        temp = button_long;
        button_long.all = 0;
    }

    return temp;
}

/******************************************************************************
 * button_scan()
 *
 * Poll button states and modify the various button-state registers.
 * Intended to be called periodically from a interrupt.
 *
 * Inputs:  None
 *
 * Returns: Nothing
 *
 * Notes:   This function updates/modifies the following global status
 *          variables:
 *          button_state        sets/clears bits
 *          button_debounced    sets/clears bits
 *          button_pressed      sets bits only
 *          button_released     sets bits only
 *          button_short        sets bits only
 *          button_long         sets bits only
 *          button_chord        sets/clears bits
 *          button_timer[]      all elements affected
 *
 *          This function should be called at a constant periodic rate,
 *          typically from a timer interrupt.  This function has been
 *          optimized to run quickly.
 ******************************************************************************/

void button_scan(void)
{
    register button_t button;           // Un-debounced state of buttons
    register uint8_t  index;            // Button scan iterator
    register uint8_t  mask;             // Bitmap iterator, 2^(0..7)
    volatile register uint16_t *down_time_p; // Ptr to button-down timers
    register uint16_t down_time;        // Current button down timer value

    // Exit if scanning disabled

    if (!button_scan_enable) {
        return;
    }

    // Read current button status and store as present button state
    //
    // Note: Using the construct { if (PINREAD(x)) {y = 1} } is more efficient
    // than constructs like:
    //   y = PINREAD(x) != 0;
    //   or
    //   y = PINREAD(x) ? 1 : 0;
    // The method used below generates the tightest code when avr-gcc is used.

    button.all = 0xFF;

    if (PINREAD(BUTTON0)) {
        button.button0 = 0;
    }
    if (PINREAD(BUTTON1)) {
        button.button1 = 0;
    }
    if (PINREAD(BUTTON2)) {
        button.button2 = 0;
    }
    if (PINREAD(BUTTON3)) {
        button.button3 = 0;
    }
    if (PINREAD(BUTTON4)) {
        button.button4 = 0;
    }
    if (PINREAD(BUTTON5)) {
        button.button5 = 0;
    }
    if (PINREAD(LEFT_BUTTON)) {
        button.left_button = 0;
    }
    if (PINREAD(RIGHT_BUTTON)) {
        button.right_button = 0;
    }

    button_state = button;

    // Check for change in button state
    //
    // A button "chord" is valid if the button state has not changed
    // for a sufficiently long period of time (BUTTON_CHORD_DELAY).

    if (button.all == button_previous.all) {
        if (~button_stable) {   // Counter overflow check
            button_stable++;
        }
        if (button_stable == BUTTON_CHORD_DELAY) {
            button_chord = button;
        }
    }
    else {
        button_stable = 0;
    }

    // Scan button status for changes, debounce buttons and register
    // button events

    // Put ptr to debounce counts in reg var for fast access
    down_time_p = button_timer;
    mask = 0x01;

    for (index = 0; index < NUM_BUTTONS; index++) {
        // Get debounce count for current button
        down_time = *down_time_p;

        if (button.all & mask) {

            // Button is pressed (on)
            // Do not increment button-down timer if it is at max already

            if (~down_time) {   // Counter overflow check
                down_time++;

                if (down_time == BUTTON_SHORT_DELAY) {

                    // Button down long enough to be considered debounced

                    button_pressed.all |= mask;
                    button_debounced.all |= mask;
                }

                else if (down_time == BUTTON_LONG_DELAY) {

                    // Button down long enough to qualifiy as a long press

                    button_long.all |= mask;
                }
            }
        }

        else {

            // Button is up (off)

            if (down_time >= BUTTON_SHORT_DELAY) {

                // Button was down long enough to be debounced, so generate
                // a release event

                button_released.all |= mask;
                button_debounced.all &= (uint8_t) ~mask;

                if (down_time < BUTTON_LONG_DELAY) {

                    // Button was down longer than minimum short time,
                    // but not long enough for the long time, so it qualifies
                    // as a short press

                    button_short.all |= mask;
                }
            }

            down_time = 0;
        }

        *down_time_p = down_time;
        down_time_p++;

        mask <<= 1;
    }

    button_previous = button;
}
