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

Content:    SPI (Serial Peripheral Interface) support routines - header
------------------------------------------------------------------------------*/

#ifndef SPI_H
#define SPI_H

#include <stdint.h>

//-----------------------------------------------------------------------------

void spi_init(void);

void spi_data_out(void *data, uint8_t size);

#endif
