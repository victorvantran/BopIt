/*
 * avr.h
 *
 * Created: 6/4/2019 11:35:06 AM
 *  Author: Victor
 */ 


#ifndef AVR_H_
#define AVR_H_


#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/io.h>

#define XTAL_FRQ 8000000lu

#define SET_BIT(p,i) ((p) |=  (1 << (i)))
#define CLR_BIT(p,i) ((p) &= ~(1 << (i)))
#define GET_BIT(p,i) ((p) &   (1 << (i)))

#define WDR() asm volatile("wdr"::)
#define NOP() asm volatile("nop"::)
#define RST() for(;;);

void avr_init(void);

void avr_wait(unsigned short msec);

#endif /* AVR_H_ */