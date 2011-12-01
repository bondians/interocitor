/*------------------------------------------------------------------------------
Name:       rotary.h
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

#ifndef ROTARY_H
#define ROTARY_H

void rotary_init(void);

uint8_t rotary_status(void);

int8_t left_rotary_relative(void);
int8_t left_rotary_absolute(void);
int8_t right_rotary_relative(void);
int8_t right_rotary_absolute(void);

#endif
