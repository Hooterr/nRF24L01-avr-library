/*
 * nRF24L01.c
 *
 * Created: 08/02/2018 20:06:12
 * Author : maxus
 */ 

#include "Common/Common.h"
#include <avr/io.h>
#include <string.h>
#include <avr/interrupt.h>

#include "NRF/nrf24.h"
#include "MK_USART/mkuart.h"

char bufor[100];

#define RECEIVER 1
#define TRANSMITTER 2

uint8_t role;

void RadioDataReceived(uint8_t* data, uint8_t dataLength);
void UsartDataReceived(char* data);

int main(void)
{    
	USART_Init(__UBRR);
	register_uart_str_rx_event_callback(UsartDataReceived);
	sei();
	
	RadioInitialize();
	RegisterRadioCallback(RadioDataReceived);
	
	role = RECEIVER;
	RadioEnterRxMode();
	RadioPrintConfig(uart_puts, uart_putc, uart_putint);
	uart_puts("Device is now in transmitter mode.\n\t'set tx' - transmitter mode\n\t'set rx' - receiver mode\n");

	while (1) 
    {
		RADIO_EVENT();
		UART_RX_STR_EVENT(bufor);
    }
}

void RadioDataReceived(uint8_t* data, uint8_t dataLength)
{
	uart_puts("Received ");
	uart_putint(dataLength, 10);
	uart_puts(" bytes: ");
	uart_puts((char*)data);
	uart_putc('\n');
}

void UsartDataReceived(char* data)
{
	if (strcmp(data, "config") == 0)
	{
		RadioPrintConfig(uart_puts, uart_putc, uart_putint);
	}
	else if(strcmp(data, "set rx") == 0)
	{
		role = RECEIVER;
		RadioEnterRxMode();
		uart_puts("Device is now in receiver mode.\n\t'set tx - transmitter mode\n\t'set rx' - receiver mode\n");
	}
	else if(strcmp(data, "set tx") == 0)
	{
		role = TRANSMITTER;
		RadioEnterTxMode();
		uart_puts("Device is now in transmitter mode.\n\t'set tx - transmitter mode\n\t'set rx' - receiver mode\n");
	}
	else
	{
		if(role == TRANSMITTER)
			RadioSend((uint8_t*)data);
	}
}