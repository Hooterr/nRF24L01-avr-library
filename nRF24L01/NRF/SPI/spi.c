/*
 * spi.c
 *
 * Created: 26/01/2018 19:54:37
 *  Author: maxus
 */ 
#include "../../Common/Common.h"
#include <avr/io.h>

#include "spi.h"

// Initializes SPI, needs to be called to enable communication
void SpiInitialize(void)
{
	// MOSI and SCK outputs
	DDR(MOSI_PORT) |= (1<<MOSI);
	DDR(SCK_PORT) |= (1<<SCK);
	
	// MISO input
	DDR(MISO_PORT) &= ~(1<<MISO);
	
	//DDRB |= (1<<PB2);

	// For hardware SPI setup the module
	#if SOFT_SPI == 0
		
    SPCR = ((1<<SPE)|               // SPI Enable
		    (0<<SPIE)|              // SPI Interrupt Enable
		    (0<<DORD)|              // Data Order (0:MSB first / 1:LSB first)
		    (1<<MSTR)|              // Master/Slave select
		    (0<<SPR1)|(1<<SPR0)|    // SPI Clock Rate --> fOSC/16
		    (0<<CPOL)|              // Clock Polarity (0:SCK low / 1:SCK hi when idle)
		    (0<<CPHA));             // Clock Phase (0:leading / 1:trailing edge sampling)

    SPSR = (1<<SPI2X);              // Double the speed
	
	#endif
}

// Basic, low-level SPI shift
uint8_t SpiShift(uint8_t data)
{
	// Hardware SPI
	#if SOFT_SPI == 0
	
	// Load the data
	SPDR = data;
	
	// Wait until it's sent
	while(!(SPSR & (1<<SPIF)));
	
	// Return what has been received
	return SPDR;
	
	// Software SPI
	#else
	
	uint8_t counter = 0x80;
	uint8_t response = 0;
	
	SCK_0;

	// Send what we got and save any response from the device
	while(counter)
	{
		if (data & counter) MOSI_1;
		else MOSI_0;

		SCK_0;
		SCK_1;

		response |= MISO_CHECK;
		
		response <<= 1;
		
		counter >>= 1;
	}

	SCK_0;

	// Flip the received data
	// 1101|0001 => 0001|1101
	uint8_t helper = response & 0x0F;
	response >>= 4;
	helper <<= 4;
	response |= helper;

	return response;
	
	#endif
}