#ifndef SERIAL_H
#define SERIAL_H
/*------------------------------------------------------------------------------
Name:       serial.h
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       16-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168/328

Content:    Interface for serial I/O library, polled or interrupt driven
------------------------------------------------------------------------------*/

#include <stdint.h>

// Uncomment the #define below to allow large buffer sizes
// Data pointers will be 16-bit values, resulting in somewhat larger code,
// slightly slower execution, and slightly higher RAM requirements.

// #define SERIAL_LARGE_BUFFERS 1

// If using <stdio.h> to implement C-standard I/O, uncomment the #define
// below to enable inclusion of I/O functions that are compatible with
// stdio calling conventions.

#define SERIAL_INCLUDE_STDIO 1

#ifdef SERIAL_INCLUDE_STDIO
#include <stdio.h>
#endif

// I/O modes for serial_init() <mode> parameter

enum serial_io_mode {
    IN_OUT_POLL     = 0,        // Use polling for both input and output
    IN_INT_OUT_POLL = 1,        // Use interrupts for input, polling for output
    IN_POLL_OUT_INT = 2,        // Use polling for input, interrupts for output
    IN_OUT_INT      = 3         // Use interrupts for both input and output
};

// Uncomment this define to allow large buffer sizes
// Data pointers will be 16-bit values, resulting in slower execution and
// somewhat higher RAM requirements.

// #define SERIAL_LARGE_BUFFERS

#ifdef SERIAL_LARGE_BUFFERS
    typedef uint16_t serial_t;
#else
    typedef uint8_t serial_t;
#endif

// Set the size of the transmit and receive buffers
// Limit: 256 bytes unless SERIAL_LARGE_BUFFERS is undefined

#define RX_BUFSIZE      16
#define TX_BUFSIZE      32

//-----------------------------------------------------------------------------

// Register name resolution

#ifdef UDR
    #define UDR0    UDR
#endif

#ifdef UCSRA
    #define UCSR0A  UCSRA
    #define RXC0    RXC
    #define TXC0    TXC
    #define UDRE0   UDRE
    #define FE0     FE
    #define DOR0    DOR
    #define PE0     PE
    #define U2X0    U2X
    #define MPCM0   MPCM
#endif

#ifdef UCSRB
    #define UCSR0B  UCSRB
    #define RXCIE0  RXCIE
    #define TXCIE0  TXCIE
    #define UDRIE0  UDRIE
    #define RXEN0   RXEN
    #define TXEN0   TXEN
    #define UCSZ02  UCSZ2
    #define RXB08   RXB8
    #define TXB08   TXB8
#endif

#ifdef UCSRC
    #define UBRRH_UCSRC_SHARED
    #define UCSR0C  UCSRC
    #define UMSEL0  UMSEL
    #define UPM01   UPM1
    #define UPM00   UPM0
    #define USBS0   USBS
    #define UCSZ01  UCSZ1
    #define UCSZ00  UCSZ0
    #define UCPOL0  UCPOL
#endif

#ifdef UBRRH
    #define UBRR0H  UBRRH
#endif

#ifdef UBRRL
    #define UBRR0L  UBRRL
#endif

//-----------------------------------------------------------------------------

// Enable or disable blocking for serial_in()

void serial_in_blocking(uint8_t mode);

// Enable or disable blocking for serial_out()

void serial_out_blocking(uint8_t mode);

// Check if serial input buffer is empty

uint8_t serial_in_empty(void);

// Check if serial input buffer is full

uint8_t serial_in_full(void);

// Calculate the number of bytes queued in the serial input buffer

serial_t serial_in_used(void);

// Calculate the number of free bytes in the serial input buffer

serial_t serial_in_free(void);

// Check if serial output buffer is empty

uint8_t serial_out_empty(void);

// Check if serial output buffer is full

uint8_t serial_out_full(void);

// Check if transmitter is idle

uint8_t serial_out_idle(void);

// Calculate the number of byts queued in the serial output buffer

serial_t serial_out_used(void);

// Calculate the number of free bytes in the serial output buffer

serial_t serial_out_free(void);

// Initialize the serial port (USART0)

uint16_t serial_init(uint16_t baud, uint8_t mode);

// Read byte from serial input buffer

int16_t serial_in(void);

// Write byte to the serial output buffer

void serial_out(uint8_t data);

// Output CR/LF sequence

void serial_crlf(void);

// Output single ASCII HEX digit

void serial_hex1(uint8_t digit);

// Output 2-digit ASCII HEX value

void serial_hex2(uint8_t value);

// Output 8-digit ASCII binary value

void serial_binary(uint8_t value);

// Output string in RAM

void serial_puts(const char *s);

// Output string in program space (FLASH)

void serial_puts_P(const char *s);

//-----------------------------------------------------------------------------

#ifdef SERIAL_INCLUDE_STDIO

// stdio-compatible file handle for serial I/O

extern FILE serial_f;

// Write byte to serial output buffer (stdio-compatible)

int serial_putc(char ch, FILE *stream);

// Get byte from serial input buffer (stdio-compatible)

int serial_getc(FILE *stream);

// Control auto-newline generation for serial_putc()

void serial_putc_auto_newline(uint8_t mode);

#endif  // SERIAL_INCLUDE_STDIO

#endif  // SERIAL_H
