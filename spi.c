/*------------------------------------------------------------------------------
Name:       spi.c
Project:    NixieClock
Author:     Mark Schultz <n9xmj@yahoo.com>, Daniel Henderson <tindrum@mac.com>
Date:       16-Mar-2009
Tabsize:    4
Copyright:  None
License:    None
Revision:   $Id$
Target CPU: ATmega168 or ATmega328

Content:    SPI (Serial Peripheral Interface) support routines
------------------------------------------------------------------------------*/

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "portdef.h"
#include "spi.h"

/******************************************************************************
 *
 ******************************************************************************/

void spi_init(void)
{
    // Configure SPI subsystem
    // Master mode, SPI mode 2, LSbit sent first

    SPCR = BM(SPE) | BM(MSTR) | BM(CPOL) | BM(DORD);
    SPSR = BM(WCOL);
    SPDR = 0;

    // Configure port pin direction for SPI (nixie driver) control pins

    DDR(DRIVER_DATA_PORT) |= BM(DRIVER_DATA_PIN);
    DDR(DRIVER_CLOCK_PORT) |= BM(DRIVER_CLOCK_PIN);
    DDR(DRIVER_LATCH_PORT) |= BM(DRIVER_LATCH_PIN);
    DDR(DRIVER_ENABLE_PORT) |= BM(DRIVER_ENABLE_PIN);

    // Set initial level for latch-enable and output-enable pins

    BCLR(DRIVER_LATCH);
    BSET(DRIVER_ENABLE);
}

/******************************************************************************
 *
 ******************************************************************************/

void spi_data_out(void *data, uint8_t size)
{
    // Send data block

    while (size) {
        SPDR = *((uint8_t *) data++);
        while (!(SPSR & BM(SPIF)));
        size--;
    }

    // Pulse latch-data pin

    BSET(DRIVER_LATCH);
    BCLR(DRIVER_LATCH);
}
