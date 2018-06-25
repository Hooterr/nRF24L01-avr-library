/*
 * mkuart.c
 *
 *  Created on: 2010-09-04
 *       Autor: Miros³aw Kardaœ
 */
#include "../Common/Common.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <util/delay.h>
#include <util/atomic.h>

#include "mkuart.h"

#if CPU == 2

volatile uint8_t ascii_line;


// definiujemy w koñcu nasz bufor UART_RxBuf
volatile char UART_RxBuf[UART_RX_BUF_SIZE];
// definiujemy indeksy okreœlaj¹ce iloœæ danych w buforze
volatile uint8_t UART_RxHead; // indeks oznaczaj¹cy „g³owê wê¿a”
volatile uint8_t UART_RxTail; // indeks oznaczaj¹cy „ogon wê¿a”



// definiujemy w koñcu nasz bufor UART_RxBuf
volatile char UART_TxBuf[UART_TX_BUF_SIZE];
// definiujemy indeksy okreœlaj¹ce iloœæ danych w buforze
volatile uint8_t UART_TxHead; // indeks oznaczaj¹cy „g³owê wê¿a”
volatile uint8_t UART_TxTail; // indeks oznaczaj¹cy „ogon wê¿a”


// wskaŸnik do funkcji callback dla zdarzenia UART_RX_STR_EVENT()
static void (*uart_rx_str_event_callback)(char * pBuf);


// funkcja do rejestracji funkcji zwrotnej w zdarzeniu UART_RX_STR_EVENT()
void register_uart_str_rx_event_callback(void (*callback)(char * pBuf)) {
	uart_rx_str_event_callback = callback;
}


// Zdarzenie do odbioru danych ³añcucha tekstowego z bufora cyklicznego
void UART_RX_STR_EVENT(char * rbuf) {

	if( ascii_line ) {
		if( uart_rx_str_event_callback ) {
			uart_get_str( rbuf );
			(*uart_rx_str_event_callback)( rbuf );
		} else {
			UART_RxHead = UART_RxTail;
		}
	}
}



void USART_Init( uint16_t baud ) {
	/* Ustawienie prêdkoœci */
	UBRR0H = (uint8_t)(baud>>8);
	UBRR0L = (uint8_t)baud;
	/* Za³¹czenie nadajnika I odbiornika */
	UCSR0B = (1<<RXEN0)|(1<<TXEN0);
	/* Ustawienie format ramki: 8bitów danych, 1 bit stopu */
	UCSR0C = (1<<USBS0)|(1<<UCSZ00)|(1<<UCSZ01);

	// jeœli korzystamy z interefejsu RS485
	#ifdef UART_DE_PORT
		// inicjalizujemy liniê steruj¹c¹ nadajnikiem
		UART_DE_DIR |= UART_DE_BIT;
		UART_DE_ODBIERANIE;
	#endif

	// jeœli korzystamy z interefejsu RS485
	#ifdef UART_DE_PORT
		// jeœli korzystamy z interefejsu RS485 za³¹czamy dodatkowe przerwanie TXCIE
		UCSR0B |= (1<<RXEN0)|(1<<TXEN0)|(1<<RXCIE0)|(1<<TXCIE0);
	#else
		// jeœli nie  korzystamy z interefejsu RS485
		UCSR0B |= (1<<RXEN0)|(1<<TXEN0)|(1<<RXCIE0);
	#endif
}

// procedura obs³ugi przerwania Tx Complete, gdy zostanie opó¿niony UDR
// kompilacja gdy u¿ywamy RS485
#ifdef UART_DE_PORT
ISR( USART_TXC_vect ) {
	UART_DE_ODBIERANIE;	// zablokuj nadajnik RS485
}
#endif


// definiujemy funkcjê dodaj¹c¹ jeden bajtdoz bufora cyklicznego
void uart_putc( char data ) {
	uint8_t tmp_head;
	ATOMIC_BLOCK( ATOMIC_RESTORESTATE ) {
		tmp_head  = (UART_TxHead + 1) & UART_TX_BUF_MASK;
	}
          // pêtla oczekuje je¿eli brak miejsca w buforze cyklicznym na kolejne znaki
    while ( tmp_head == UART_TxTail ){}

    UART_TxBuf[tmp_head] = data;
    UART_TxHead = tmp_head;

    // inicjalizujemy przerwanie wystêpuj¹ce, gdy bufor jest pusty, dziêki
    // czemu w dalszej czêœci wysy³aniem danych zajmie siê ju¿ procedura
    // obs³ugi przerwania
    UCSR0B |= (1<<UDRIE0);
}


void uart_puts(char *s)		// wysy³a ³añcuch z pamiêci RAM na UART
{
  register char c;
  while ((c = *s++)) uart_putc(c);			// dopóki nie napotkasz 0 wysy³aj znak
}

void uart_putint(int value, int radix)	// wysy³a na port szeregowy tekst
{
	char string[17];			// bufor na wynik funkcji itoa
	itoa(value, string, radix);		// konwersja value na ASCII
	uart_puts(string);			// wyœlij string na port szeregowy
}


// definiujemy procedurê obs³ugi przerwania nadawczego, pobieraj¹c¹ dane z bufora cyklicznego
ISR( USART_UDRE_vect)  {
    // sprawdzamy czy indeksy s¹ ró¿ne
    if ( UART_TxHead != UART_TxTail ) {
    	// obliczamy i zapamiêtujemy nowy indeks ogona wê¿a (mo¿e siê zrównaæ z g³ow¹)
    	UART_TxTail = (UART_TxTail + 1) & UART_TX_BUF_MASK;
    	// zwracamy bajt pobrany z bufora  jako rezultat funkcji
#ifdef UART_DE_PORT
    	UART_DE_NADAWANIE;
#endif
    	UDR0 = UART_TxBuf[UART_TxTail];
    } else {
	// zerujemy flagê przerwania wystêpuj¹cego gdy bufor pusty
	UCSR0B &= ~(1<<UDRIE0);
    }
}


// definiujemy funkcjê pobieraj¹c¹ jeden bajt z bufora cyklicznego
int uart_getc(void) {
	int data = -1;
    // sprawdzamy czy indeksy s¹ równe
    if ( UART_RxHead == UART_RxTail ) return data;
    ATOMIC_BLOCK( ATOMIC_RESTORESTATE ) {
        // obliczamy i zapamiêtujemy nowy indeks „ogona wê¿a” (mo¿e siê zrównaæ z g³ow¹)
        UART_RxTail = (UART_RxTail + 1) & UART_RX_BUF_MASK;
        // zwracamy bajt pobrany z bufora  jako rezultat funkcji
        data = UART_RxBuf[UART_RxTail];
    }
    return data;
}

char * uart_get_str(char * buf) {
	int c;
	char * wsk = buf;
	if( ascii_line ) {
		while( (c = uart_getc()) ) {
			if( 13 == c || c < 0) break;
			*buf++ = c;
		}
		*buf=0;
		ascii_line--;
	}
	return wsk;
}

// definiujemy procedurê obs³ugi przerwania odbiorczego, zapisuj¹c¹ dane do bufora cyklicznego
ISR( USART_RX_vect ) {

    register uint8_t tmp_head;
    register char data;

    data = UDR0; //pobieramy natychmiast bajt danych z bufora sprzêtowego

    // obliczamy nowy indeks „g³owy wê¿a”
    tmp_head = ( UART_RxHead + 1) & UART_RX_BUF_MASK;

    // sprawdzamy, czy w¹¿ nie zacznie zjadaæ w³asnego ogona
    if ( tmp_head == UART_RxTail ) {
    	// tutaj mo¿emy w jakiœ wygodny dla nas sposób obs³u¿yæ  b³¹d spowodowany
    	// prób¹ nadpisania danych w buforze, mog³oby dojœæ do sytuacji gdzie
    	// nasz w¹¿ zacz¹³by zjadaæ w³asny ogon
    	// jednym z najbardziej oczywistych rozwi¹zañ jest np natychmiastowe
    	// wyzerowanie zmiennej ascii_line lub sterowanie sprzêtow¹ lini¹
    	// zajêtoœci bufora
    	UART_RxHead = UART_RxTail;
    } else {
    	switch( data ) {
    		case 0:					// ignorujemy bajt = 0
    		case 10: break;			// ignorujemy znak LF
    		case 13: ascii_line++;	// sygnalizujemy obecnoœæ kolejnej linii w buforze
    		default : UART_RxHead = tmp_head; UART_RxBuf[tmp_head] = data;
    	}

    }
}
#endif

#if CPU == 1

volatile uint8_t ascii_line;


// definiujemy w koñcu nasz bufor UART_RxBuf
volatile char UART_RxBuf[UART_RX_BUF_SIZE];
// definiujemy indeksy okreœlaj¹ce iloœæ danych w buforze
volatile uint8_t UART_RxHead; // indeks oznaczaj¹cy „g³owê wê¿a”
volatile uint8_t UART_RxTail; // indeks oznaczaj¹cy „ogon wê¿a”



// definiujemy w koñcu nasz bufor UART_RxBuf
volatile char UART_TxBuf[UART_TX_BUF_SIZE];
// definiujemy indeksy okreœlaj¹ce iloœæ danych w buforze
volatile uint8_t UART_TxHead; // indeks oznaczaj¹cy „g³owê wê¿a”
volatile uint8_t UART_TxTail; // indeks oznaczaj¹cy „ogon wê¿a”


// wskaŸnik do funkcji callback dla zdarzenia UART_RX_STR_EVENT()
static void (*uart_rx_str_event_callback)(char * pBuf);


// funkcja do rejestracji funkcji zwrotnej w zdarzeniu UART_RX_STR_EVENT()
void register_uart_str_rx_event_callback(void (*callback)(char * pBuf)) {
	uart_rx_str_event_callback = callback;
}


// Zdarzenie do odbioru danych ³añcucha tekstowego z bufora cyklicznego
void UART_RX_STR_EVENT(char * rbuf) {

	if( ascii_line ) {
		if( uart_rx_str_event_callback ) {
			uart_get_str( rbuf );
			(*uart_rx_str_event_callback)( rbuf );
			} else {
			UART_RxHead = UART_RxTail;
		}
	}
}



void USART_Init( uint16_t baud ) {
	/* Ustawienie prêdkoœci */
	UBRRH = (uint8_t)(baud>>8);
	UBRRL = (uint8_t)baud;
	/* Za³¹czenie nadajnika I odbiornika */
	UCSRB = (1<<RXEN)|(1<<TXEN);
	/* Ustawienie format ramki: 8bitów danych, 1 bit stopu */
	UCSRC = (1<<URSEL)|(3<<UCSZ0);

	// jeœli korzystamy z interefejsu RS485
	#ifdef UART_DE_PORT
	// inicjalizujemy liniê steruj¹c¹ nadajnikiem
	UART_DE_DIR |= UART_DE_BIT;
	UART_DE_ODBIERANIE;
	#endif

	// jeœli korzystamy z interefejsu RS485
	#ifdef UART_DE_PORT
	// jeœli korzystamy z interefejsu RS485 za³¹czamy dodatkowe przerwanie TXCIE
	UCSRB |= (1<<RXEN)|(1<<TXEN)|(1<<RXCIE)|(1<<TXCIE);
	#else
	// jeœli nie  korzystamy z interefejsu RS485
	UCSRB |= (1<<RXEN)|(1<<TXEN)|(1<<RXCIE);
	#endif
}

// procedura obs³ugi przerwania Tx Complete, gdy zostanie opó¿niony UDR
// kompilacja gdy u¿ywamy RS485
#ifdef UART_DE_PORT
ISR( USART_TXC_vect ) {
	UART_DE_ODBIERANIE;	// zablokuj nadajnik RS485
}
#endif


// definiujemy funkcjê dodaj¹c¹ jeden bajtdoz bufora cyklicznego
void uart_putc( char data ) {
	uint8_t tmp_head;
	ATOMIC_BLOCK( ATOMIC_RESTORESTATE ) {
		tmp_head  = (UART_TxHead + 1) & UART_TX_BUF_MASK;
	}
	// pêtla oczekuje je¿eli brak miejsca w buforze cyklicznym na kolejne znaki
	while ( tmp_head == UART_TxTail ){}

	UART_TxBuf[tmp_head] = data;
	UART_TxHead = tmp_head;

	// inicjalizujemy przerwanie wystêpuj¹ce, gdy bufor jest pusty, dziêki
	// czemu w dalszej czêœci wysy³aniem danych zajmie siê ju¿ procedura
	// obs³ugi przerwania
	UCSRB |= (1<<UDRIE);
}


void uart_puts(char *s)		// wysy³a ³añcuch z pamiêci RAM na UART
{
	register char c;
	while ((c = *s++)) uart_putc(c);			// dopóki nie napotkasz 0 wysy³aj znak
}

void uart_putint(int value, int radix)	// wysy³a na port szeregowy tekst
{
	char string[17];			// bufor na wynik funkcji itoa
	itoa(value, string, radix);		// konwersja value na ASCII
	uart_puts(string);			// wyœlij string na port szeregowy
}


// definiujemy procedurê obs³ugi przerwania nadawczego, pobieraj¹c¹ dane z bufora cyklicznego
ISR( USART_UDRE_vect)  {
	// sprawdzamy czy indeksy s¹ ró¿ne
	if ( UART_TxHead != UART_TxTail ) {
		// obliczamy i zapamiêtujemy nowy indeks ogona wê¿a (mo¿e siê zrównaæ z g³ow¹)
		UART_TxTail = (UART_TxTail + 1) & UART_TX_BUF_MASK;
		// zwracamy bajt pobrany z bufora  jako rezultat funkcji
		#ifdef UART_DE_PORT
		UART_DE_NADAWANIE;
		#endif
		UDR = UART_TxBuf[UART_TxTail];
		} else {
		// zerujemy flagê przerwania wystêpuj¹cego gdy bufor pusty
		UCSRB &= ~(1<<UDRIE);
	}
}


// definiujemy funkcjê pobieraj¹c¹ jeden bajt z bufora cyklicznego
int uart_getc(void) {
	int data = -1;
	// sprawdzamy czy indeksy s¹ równe
	if ( UART_RxHead == UART_RxTail ) return data;
	ATOMIC_BLOCK( ATOMIC_RESTORESTATE ) {
		// obliczamy i zapamiêtujemy nowy indeks „ogona wê¿a” (mo¿e siê zrównaæ z g³ow¹)
		UART_RxTail = (UART_RxTail + 1) & UART_RX_BUF_MASK;
		// zwracamy bajt pobrany z bufora  jako rezultat funkcji
		data = UART_RxBuf[UART_RxTail];
	}
	return data;
}

char * uart_get_str(char * buf) {
	int c;
	char * wsk = buf;
	if( ascii_line ) {
		while( (c = uart_getc()) ) {
			if( 13 == c || c < 0) break;
			*buf++ = c;
		}
		*buf=0;
		ascii_line--;
	}
	return wsk;
}

// definiujemy procedurê obs³ugi przerwania odbiorczego, zapisuj¹c¹ dane do bufora cyklicznego
ISR( USART_RXC_vect ) {

	register uint8_t tmp_head;
	register char data;

	data = UDR; //pobieramy natychmiast bajt danych z bufora sprzêtowego

	// obliczamy nowy indeks „g³owy wê¿a”
	tmp_head = ( UART_RxHead + 1) & UART_RX_BUF_MASK;

	// sprawdzamy, czy w¹¿ nie zacznie zjadaæ w³asnego ogona
	if ( tmp_head == UART_RxTail ) {
		// tutaj mo¿emy w jakiœ wygodny dla nas sposób obs³u¿yæ  b³¹d spowodowany
		// prób¹ nadpisania danych w buforze, mog³oby dojœæ do sytuacji gdzie
		// nasz w¹¿ zacz¹³by zjadaæ w³asny ogon
		// jednym z najbardziej oczywistych rozwi¹zañ jest np natychmiastowe
		// wyzerowanie zmiennej ascii_line lub sterowanie sprzêtow¹ lini¹
		// zajêtoœci bufora
		UART_RxHead = UART_RxTail;
		} else {
		switch( data ) {
			case 0:					// ignorujemy bajt = 0
			case 10: break;			// ignorujemy znak LF
			case 13: ascii_line++;	// sygnalizujemy obecnoœæ kolejnej linii w buforze
			default : UART_RxHead = tmp_head; UART_RxBuf[tmp_head] = data;
		}

	}
}


#endif
