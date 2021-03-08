/*
 * avr.c
 *
 * Created: 6/4/2019 11:34:47 AM
 *  Author: Victor
 */ 


#include "avr.h"
#define TIMER_RESOLUTION 0.00001

void
avr_init(void)
{
	WDTCR = 15;
}

void
avr_wait(unsigned short msec)
{
	TCCR0 = 3;
	while (msec--) {
		TCNT0 = (unsigned char)(256 - (XTAL_FRQ / 64) * 0.001);
		//TCNT0 = (unsigned char)(256 - (XTAL_FRQ / 64) * TIMER_RESOLUTION);
		SET_BIT(TIFR, TOV0);
		WDR();
		while (!GET_BIT(TIFR, TOV0));
	}
	TCCR0 = 0;
}
