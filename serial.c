/*------------------------------------------------------------------------------
Name:       serial.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       16-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168/328

Content:    Serial I/O library, polled or interrupt driven
------------------------------------------------------------------------------*/

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "portdef.h"
#include "serial.h"

// Constants and defines

#ifndef RX_BUFSIZE
#define RX_BUFSIZE          16          // Receive buffer size
#endif
#ifndef TX_BUFSIZE
#define TX_BUFSIZE          16          // Transmit buffer size
#endif

// Variable declarations

static volatile uint8_t  rx_buffer[RX_BUFSIZE];  // Receive buffer
static volatile uint8_t  tx_buffer[TX_BUFSIZE];  // Transmit buffer
static volatile serial_t rx_head;       // Receive buffer head index
static volatile serial_t rx_tail;       // Receive buffer tail index
/*static*/ volatile serial_t tx_head;              // Transmit buffer head index
/*static*/ volatile serial_t tx_tail;              // Transmit buffer tail index

/*static*/ volatile uint8_t rx_ctrl;        // Receiver status/control:
// 7  6  5  4  3  2  1  0
// |  |  |  |  |  |  |  |______________ Receive buffer empty
// |  |  |  |  |  |  |_________________ Receive buffer full
// |  |  |  |  |  |____________________ Unused/reserved
// |  |  |  |  |_______________________ Unused/reserved
// |  |  |  |__________________________ Use polling mode for receive/input
// |  |  |_____________________________ serial_in() blocks when buffer empty
// |  |________________________________ Unused/reserved
// |___________________________________ Unused/reserved

#define rx_empty()          (rx_ctrl & BM(0))
#define set_rx_empty()      rx_ctrl |= BM(0)
#define clear_rx_empty()    rx_ctrl &= INVBM(0)

#define rx_full()           (rx_ctrl & BM(1))
#define set_rx_full()       rx_ctrl |= BM(1)
#define clear_rx_full()     rx_ctrl &= INVBM(1)

#define rx_poll()           (rx_ctrl & BM(4))
#define set_rx_poll()       rx_ctrl |= BM(4)
#define clear_rx_poll()     rx_ctrl &= INVBM(4)

#define rx_block()          (rx_ctrl & BM(5))
#define set_rx_block()      rx_ctrl |= BM(5)
#define clear_rx_block()    rx_ctrl &= INVBM(5)

/*static*/ volatile uint8_t tx_ctrl;               // Transmitter control/status bits:
// 7  6  5  4  3  2  1  0
// |  |  |  |  |  |  |  |______________ Transmit buffer empty
// |  |  |  |  |  |  |_________________ Transmit buffer full
// |  |  |  |  |  |____________________ Unused/reserved
// |  |  |  |  |_______________________ Unused/reserved
// |  |  |  |__________________________ Use polling mode for transmit/output
// |  |  |_____________________________ serial_out() blocks when buffer full
// |  |________________________________ serial_putc() sends CR/LF when LF passed
// |___________________________________ Unused/reserved

#define tx_empty()          (tx_ctrl & BM(0))
#define set_tx_empty()      tx_ctrl |= BM(0)
#define clear_tx_empty()    tx_ctrl &= INVBM(0)

#define tx_full()           (tx_ctrl & BM(1))
#define set_tx_full()       tx_ctrl |= BM(1)
#define clear_tx_full()     tx_ctrl &= INVBM(1)

#define tx_poll()           (tx_ctrl & BM(4))
#define set_tx_poll()       tx_ctrl |= BM(4)
#define clear_tx_poll()     tx_ctrl &= INVBM(4)

#define tx_block()          (tx_ctrl & BM(5))
#define set_tx_block()      tx_ctrl |= BM(5)
#define clear_tx_block()    tx_ctrl &= INVBM(5)

#define auto_newline()       (tx_ctrl ^ BM(6))
#define set_auto_newline()   tx_ctrl |= BM(6)
#define clear_auto_newline() tx_ctrl &= INVBM(6)

// stdio-compatible file handle for serial I/O

#ifdef SERIAL_INCLUDE_STDIO
FILE serial_f = FDEV_SETUP_STREAM(serial_putc, serial_getc, _FDEV_SETUP_RW);
#endif

/*******************************************************************************
Interrupt control functions
*******************************************************************************/

static inline void rx_int_off(void)
{
    UCSR0B &= INVBM(RXCIE0);
}
        
static inline void rx_int_on(void)
{
    UCSR0B |= BM(RXCIE0);
}
        
static inline void tx_int_off(void)
{
    UCSR0B &= INVBM(UDRIE0);
}
        
static inline void tx_int_on(void)
{
    UCSR0B |= BM(UDRIE0);
}

/*******************************************************************************
void serial_in_blocking(mode)

Usage:  Control serial input blocking mode

Inputs: mode        If nonzero, enable blocking for serial_in() (no return until
                    data received).

Return: (return)    If blocking is disabled, will return -1 if no data available
*******************************************************************************/

void serial_in_blocking(uint8_t mode)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mode)
            set_rx_block();
        else
            clear_rx_block();
    }
}

/*******************************************************************************
void serial_out_blocking(mode)

Usage:  Control serial output blocking mode

Inputs: mode        If nonzero, enable blocking for serial_out() (no return
                    until data queued/sent).  When blocking is disabled,
                    serial_out() will discard the data passed to it if it cannot
                    be queued or sent.

Return: (none)
*******************************************************************************/

void serial_out_blocking(uint8_t mode)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mode)
            set_tx_block();
        else
            clear_tx_block();
    }
}

/*******************************************************************************
uint8_t serial_in_empty(void)

Usage:  Check if serial input buffer is empty (no received data available).

Inputs: (none)

Return: (return)    Nonzero if no received data available.
*******************************************************************************/

uint8_t serial_in_empty(void)
{
    uint8_t ret;

    if (rx_poll())
        ret = !(UCSR0A & BM(RXC0));
    else
        ret = rx_empty();

    return ret;
}

/*******************************************************************************
uint8_t serial_in_full(void)

Usage:  Check if serial input buffer is full

Inputs: (none)

Return: (return)    Nonzero if the receive buffer is full.
                    In polled mode, returns nonzero result if received data is
                    available.
*******************************************************************************/

uint8_t serial_in_full(void)
{
    uint8_t ret;

    if (rx_poll())
        ret = (UCSR0A & BM(RXC0));
    else
        ret = rx_full();

    return ret;
}

/*******************************************************************************
serial_t serial_in_used(void)

Usage:  Calculate serial input buffer space used

Inputs: (none)

Return: (return)    Number of bytes in the serial input queue.
                    Result is meaningless if called when receive polled mode
                    is active.
*******************************************************************************/

serial_t serial_in_used(void)
{
    serial_t used;

    if (rx_poll())
        return 0;

    // Disable receive interrupt during buffer index evaluation

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_off();
    }

    // Calculate used space in receive buffer

    if (rx_full())
        used = RX_BUFSIZE;
    else if (rx_head >= rx_tail)
        used = rx_head - rx_tail;
    else
        used = RX_BUFSIZE - rx_tail + rx_head;

    // Enable transmit interrupt & exit

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_on();
    }

    return used;
}

/*******************************************************************************
serial_t serial_in_free(void)

Usage:  Calculate serial input buffer space available

Inputs: (none)

Return: (return)    Number of unused bytes in the serial input queue.
                    Result is meaningless if called when receive polled mode
                    is active.
*******************************************************************************/

uint8_t serial_in_free(void)
{
    serial_t free;

    if (rx_poll())
        return 0;

    // Disable receive interrupt during buffer index evaluation

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_off();
    }

    // Calculate free space in receive buffer

    if (rx_empty())
        free = RX_BUFSIZE;
    else if (rx_head >= rx_tail)
        free = RX_BUFSIZE - rx_head + rx_tail;
    else
        free = rx_tail - rx_head;

    // Enable transmit interrupt & exit

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_on();
    }

    return free;
}

/*******************************************************************************
uint8_t serial_out_empty(void)

Usage:  Check if serial output buffer is empty

Inputs: (none)

Return: (return)    Nonzero if the serial output queue is empty.
                    In interrupt-driven mode, the status does not reflect any
                    data that might be pending in the USART transmit register(s).
                    In polled mode, returns nonzero result if the transmit data
                    register is empty.
*******************************************************************************/

uint8_t serial_out_empty(void)
{
    uint8_t ret;

    if (tx_poll())
        ret = (UCSR0A & BM(UDRE0));
    else
        ret = tx_empty();

    return ret;
}

/*******************************************************************************
uint8_t serial_out_full(void)

Usage:  Check if serial output buffer is full
        (e.g. send attempt will block or loose data)

Inputs: (none)

Return: (return)    Nonzero if the serial output queue is full.
                    In polled mode, returns nonzero result if the transmit data
                    register is full.
*******************************************************************************/

uint8_t serial_out_full(void)
{
    uint8_t ret;

    if (tx_poll())
        ret = !(UCSR0A & BM(UDRE0));
    else
        ret = tx_full();

    return ret;
}

/*******************************************************************************
uint8_t serial_out_idle(void)

Usage:  Check if serial transmitter is idle

Inputs: (none)

Return: (return)    Nonzero if serial output queue is empty and the USART
                    transmitter is idle (not sending any data).
*******************************************************************************/

uint8_t serial_out_idle(void)
{
    uint8_t ret;

    if (tx_poll())
        ret = ((UCSR0A & BM(TXC0)) != 0);
    else
        ret = (tx_empty() && (UCSR0A & BM(TXC0)));

    return ret;
}

/*******************************************************************************
serial_t serial_out_used(void)

Usage:  Calculate serial output buffer space used

Inputs: (none)

Return: (return)    Number of bytes in the serial output queue.
                    Result is meaningless if called when the transmit polled
                    mode is active.
*******************************************************************************/

serial_t serial_out_used(void)
{
    serial_t used;

    if (tx_poll())
        return 0;

    // Disable transmit interrupt during buffer index evaluation

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tx_int_off();
    }

    // Calculate used space in transmit buffer

    if (tx_full())
        used = TX_BUFSIZE;
    else if (tx_head >= tx_tail)
        used = tx_head - tx_tail;
    else
        used = TX_BUFSIZE - tx_tail + tx_head;

    // Enable transmit interrupt & exit

    if (! tx_empty()) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            tx_int_on();
        }
    }

    return used;
}

/*******************************************************************************
serial_t serial_out_free(void)

Usage:  Calculate serial output buffer space available

Inputs: (none)

Return: (return)    Number of unused bytes in the serial output queue.
                    Result is meaningless if called when transmit polled mode
                    is active.
*******************************************************************************/

serial_t serial_out_free(void)
{
    serial_t free;

    if (tx_poll())
        return 0;

    // Disable transmit interrupt during buffer index evaluation

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tx_int_off();
    }

    // Calculate free space in transmit buffer

    if (tx_empty())
        free = TX_BUFSIZE;
    else if (tx_head >= tx_tail)
        free = TX_BUFSIZE - tx_head + tx_tail;
    else
        free = tx_tail - tx_head;

    // Enable transmit interrupt & exit

    if (! tx_empty()) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            tx_int_on();
        }
    }

    return free;
}

/*******************************************************************************
uint16_t serial_init(baud, mode)

Usage:  Initialize USART in asynchronous I/O mode
        Configures USART for 8 bit word, no parity, 1 stop bit

Inputs: baud        Baud (bit) rate requested
        mode        Interrupt/polling mode to use (see serial.h)

Return: (return)    Actual baud rate selected, adjusted for F_CPU and divisor
                    granularity.
*******************************************************************************/

uint16_t serial_init(uint16_t baud, uint8_t mode)
{
    uint16_t actual_baud;
    uint16_t divisor;

    // Adjust baud rate to conform to system limits (if needed)

    if (! baud)
        baud = 19200;                   // Default rate
    if (baud < 300)
        baud = 300;                     // Minimum rate

    // Calculate baud rate divisor and actual baud rate

    divisor = (uint16_t) ((F_CPU >> 4) / (uint32_t) baud);
    actual_baud = (uint16_t) ((F_CPU >> 4) / (uint32_t) divisor);

    // Disable transmit & receive during configuration

    UCSR0B = 0x00;
    UCSR0C = 0x00;

    // Reset buffer pointers and status

    rx_head = 0;
    rx_tail = 0;
    rx_ctrl = 0;

    tx_head = 0;
    tx_tail = 0;
    tx_ctrl = 0;

    set_rx_empty();
    set_tx_empty();
    set_tx_block();

    // Set baud rate divisor

    divisor--;                          // UBBR requires divisor - 1
    UBRR0H = (uint8_t) (divisor >> 8);
    UBRR0L = (uint8_t) divisor;

    // Set up serial port pins
    //
    // Not really needed, as UART will take over these pins, but
    // ensures that these pins idle at the right state if the UART
    // is disabled.

    TXD_PORT |= BM(TXD_PIN);           // TxD idles high
    RXD_PORT |= BM(RXD_PIN);           // Enable pullup on RxD pin
    DDR(TXD_PORT) |= BM(TXD_PIN);
    DDR(RXD_PORT) &= INVBM(RXD_PIN);

    // Configure UART operating mode
    // Async, 8 bit word, no parity, 1 stop bit
    // Enable transmit & receive

#ifdef UBRRH_UCSRC_SHARED
    UCSR0C = BM(URSEL) | BM(UCSZ01) | BM(UCSZ00);
#else
    UCSR0C = BM(UCSZ01) | BM(UCSZ00);
#endif
    UCSR0B = BM(RXEN0) | BM(TXEN0);

    // Configure interrupts

    if ((mode == IN_OUT_POLL) || (mode == IN_POLL_OUT_INT))
        set_rx_poll();
    else
        rx_int_on();

    if ((mode == IN_OUT_POLL) || (mode == IN_INT_OUT_POLL))
        set_tx_poll();

    return actual_baud;
}

/*******************************************************************************
int16_t serial_in(void)

Usage:  Get data from serial input buffer (basic input function)

Inputs: (none)

Return: (return)    Data from input queue (if interrupt mode), or data from
                    receive data register (polling mode).  A negative result
                    is returned if no data is available and input blocking
                    is disabled.

Notes:  If input blocking is enabled, will not return until data is available.
*******************************************************************************/

int16_t serial_in(void)
{
    uint8_t data;

    // Polled mode reception

    if (rx_poll()) {
        if (rx_block()) {
            // Blocking mode - wait for reception
            while (! (UCSR0A & BM(RXC0))) ;
            return UDR0;
        }
        else {
            // Non-blocking mode - Return no-data status if nothing received
            if (UCSR0A & BM(RXC0))
                return UDR0;
            return -1;
        }
    }

    // Buffered (interrupt-driven) reception

    if (rx_block()) {
        // Blocking mode - wait for reception
        while (rx_empty()) ;
    }
    else {
        // Non-blocking mode: Return empty status if nothing in receive buffer
        if (rx_empty())
            return -1;
    }

    // Disable receive interrupt during buffer/status update

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_off();
    }

    // Get data from receive buffer

    data = rx_buffer[rx_tail++];

    // Check for tail index wraparound

    if (rx_tail >= RX_BUFSIZE)
        rx_tail = 0;

    // Set receive buffer empty flag if buffer empty

    clear_rx_full();
    if (rx_head == rx_tail)
        set_rx_empty();
    
    // Enable receive interrupt & exit

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        rx_int_on();
    }

    return data;
}

/*******************************************************************************
void serial_out(data)

Usage:  Place data in serial output buffer (basic output function)

Inputs: data        Data to place in buffer (interrupt mode) or into transmit
                    data register (polled mode).

Return: (none)

Note:   If output blocking is enabled, will wait for transmit buffer space to
        become available.  In polled mode, waits for empty transmit data
        register.
        When blocking is disabled, data will be discared if it cannot be queued
        or sent.
*******************************************************************************/

void serial_out(uint8_t data)
{
    // Polled mode transmission

    if (tx_poll()) {
        if (tx_block()) {
            // Blocking mode - wait for data reg to become empty
            while (! (UCSR0A & BM(UDRE0))) ;
        }
        else {
            // Non-blocking mode - discard data if data reg not empty
            if (! (UCSR0A & BM(UDRE0)))
                return;
        }                    
        UDR0 = data;
        return;
    }

    // Buffered (interrupt-driven) transmission

    if (tx_block()) {
        // Blocking mode - wait for transmit buffer space
        while (tx_full()) ;
    }
    else {
        // Non-blocking mode - exit if no transmit buffer space
        if (tx_full())
            return;
    }

    // Disable transmit interrupt during buffer/status update

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tx_int_off();
    }

    // Put data in transmit buffer
    
    tx_buffer[tx_head++] = data;

    // Check for head index wraparound

    if (tx_head >= TX_BUFSIZE)
        tx_head = 0;

    // Set transmit buffer full flag if buffer full

    clear_tx_empty();
    if (tx_head == tx_tail)
        set_tx_full();

    // Enable transmit interrupt & exit

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tx_int_on();
    }
}

#ifdef SERIAL_INCLUDE_STDIO

/*******************************************************************************
int serial_putc(ch)

Usage:  stdio-compatible serial output

Inputs: ch          Character to transmit
        *stream     File buffer

Return: (return)    0 if character was successfully transmitted, or error code

Notes:  If the tx_auto_newline flag is set, a CR/LF sequence will be sent when
        <ch> = '\n' (0x0A).
        Per stdio convention, serial_putc() ALWAYS blocks, even if the
        tx_blocking flag is not set.
*******************************************************************************/

int serial_putc(char ch, FILE *stream)
{
    // Send CR/LF if auto-newline enabled

    if ((ch == '\n') && auto_newline())
        serial_putc('\r', stream);

    // Polled mode transmission

    if (tx_poll()) {
        while (! (UCSR0A & BM(UDRE0))) ;
        UDR0 = ch;
        return 0;
    }

    // Interrupt-driven transmission
    // Check for free buffer space (ignore blocking flag)

    while (tx_full()) ;

    serial_out(ch);

    return 0;
}

/*******************************************************************************
int serial_getc(FILE *stream)

Usage:  Get character from serial input

Inputs: *stream     File buffer

Return: (return)    Character from serial input, or 0 if no data available
*******************************************************************************/

int serial_getc(FILE *stream)
{
    // Polled mode reception

    if (rx_poll()) {
        if (UCSR0A & BM(RXC0))
            return UDR0;
        else
            return 0;
    }

    // Buffered (interrupt-driven) reception

    if (rx_empty())
        return 0;
    else
        return serial_in();
}

/*******************************************************************************
void serial_putc_auto_newline(mode)

Usage:  Control auto-newline generation in serial_putc()

Inputs: mode        If nonzero, enable generation of a CR/LF sequence when
                    serial_putc() is passed  the newline ('\n', 0x0A) character.

Return: (none)

Note:   This setting only has meaning for serial_putc().  The normal serial
        output routine (serial_out()) ignores this setting.
*******************************************************************************/

void serial_putc_auto_newline(uint8_t mode)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mode)
            set_auto_newline();
        else
            clear_auto_newline();
    }
}

#endif  // SERIAL_INCLUDE_STDIO

/*******************************************************************************
void serial_crlf(void)

Usage:  Send CR/LF to serial port

Inputs: (none)

Return: (none)
*******************************************************************************/

void serial_crlf(void)
{
    serial_out('\r');
    serial_out('\n');
}

/*******************************************************************************
void serial_hex1(digit)

Usage:  Send 1-digit ASCII HEX value to serial port

Input:  digit       4-bit value to convert to uppercase ASCII hex and send
                    Upper 4 bits of value are masked/ignored.

Return: (none)
*******************************************************************************/

void serial_hex1(uint8_t digit)
{
    digit &= 0x0F;
    if (digit < 10)
        digit += '0';
    else
        digit += 'A' - 10;
    serial_out(digit);
}

/*******************************************************************************
void serial_hex2(value)

Usage:  Send 2-digit ASCII HEX value to serial port

Input:  value       8-bit value which will be converted to two uppercase ASCII
                    hex digits and sent.

Return: (none)
*******************************************************************************/

void serial_hex2(uint8_t value)
{
    serial_hex1(value >> 4);
    serial_hex1(value);
}

/*******************************************************************************
void serial_binary(value)

Usage:  Send 8-digit ASCII binary value to serial port

Input:  value       8-bit value which will be converted to 8 ASCII binary
                    digits and sent.

Return: (none)
*******************************************************************************/

void serial_binary(uint8_t value)
{
    uint8_t digit;

    for (digit = 8; digit; digit--) {
        serial_out((value & 0x80) ? '1' : '0');
        value <<= 1;
    }
}

/*******************************************************************************
void serial_puts(s)

Usage:  Write string in RAM to serial port

Inputs: s           Null-terminated string in RAM to send

Return: (none)
*******************************************************************************/

void serial_puts(const char *s)
{
    char ch;

    do {
        ch = *s++;
        if (! ch) break;
        serial_out(ch);
    } while (1);
}

/*******************************************************************************
void serial_puts_P(s)

Usage:  Write string in program space (FLASH) to serial port

Inputs: s           Null-terminated string in program space to send

Return: (none)
*******************************************************************************/

void serial_puts_P(const char *s)
{
    char ch;

    do {
        ch = pgm_read_byte(s++);
        if (! ch) break;
        serial_out(ch);
    } while (1);
}

/*******************************************************************************
USART received data interrupt
Places received data into the input buffer
*******************************************************************************/

// See note below (USART_UDRE ISR declaration) for reasons why the ISR_BLOCK
// attribute is used in this ISR declaration.  RXCIE0 must be turned off before
// this interrupt can safely unblock and allow other interrupts to take
// priority.

#ifdef USART_RX_vect
ISR(USART_RX_vect, ISR_BLOCK)
#else
ISR(USART_RXC_vect, ISR_BLOCK)
#endif
{
    static uint8_t data;

    // Get interrupts enabled ASAP

    rx_int_off();
    sei();
    data = UDR0;

    // Do not queue data if buffer full

    if (! rx_full()) {

        // Place received data in buffer

        rx_buffer[rx_head++] = data;

        // Check for head index wraparound

        if (rx_head >= RX_BUFSIZE)
            rx_head = 0;

        // Set receive buffer full flag if buffer full
        // Do not enable receive interrupt if buffer is full

        clear_rx_empty();
        if (rx_head == rx_tail)
            set_rx_full();
        else {
            cli();
            rx_int_on();
        }
    }
}
    
/*******************************************************************************
USART transmit data register empty interrupt
Transmits data from the output buffer
*******************************************************************************/

// Must use ISR_BLOCK attribute in declaration even though it is desired for
// this ISR to _NOT_ block (i.e. let other interrupts take priority) but
// the USART UDRE interrupt will re-enter itself repeatedly (eventually over-
// flowing the stack) until the UDRE flag is cleared or the associated interrupt
// enable (UIDRE0) is turned off.  Thus, the interrupt is declared in BLOCKing
// mode and the entry code masks the TDRE interrupt and performs a SEI as soon
// as it can do so safely.

ISR(USART_UDRE_vect, ISR_BLOCK)
{
    // Get interrupts enabled ASAP

    tx_int_off();
    sei();

    // Do not transmit new data if buffer is empty

    if (! tx_empty()) {

        // Transmit next byte in buffer

        UDR0 = tx_buffer[tx_tail++];
        
        // Check for tail index wraparound

        if (tx_tail >= TX_BUFSIZE)
            tx_tail = 0;

        // Set transmit buffer empty flag if buffer empty
        // Do not re-enable transmit interrupt if the buffer is empty

        clear_tx_full();
        if (tx_head == tx_tail)
            set_tx_empty();
        else {
            cli();
            tx_int_on();
        }
    }            
}
