/*
 * Common.h
 *
 * Created: 26/01/2018 19:48:13
 *  Author: maxus
 */ 


#ifndef COMMON_H_
#define COMMON_H_

#define F_CPU 11059200UL

#define LED_ON DDRD |= (1<<PD7); PORTD &= ~(1<<PD7)


// 1 - mega32
// 2 - mega32p
#define CPU 2

// Makes port, pin and ddr access easier
// *** PORT
#define PORT(x) SPORT(x)
#define SPORT(x) (PORT##x)
// *** PIN
#define PIN(x) SPIN(x)
#define SPIN(x) (PIN##x)
// *** DDR
#define DDR(x) SDDR(x)
#define SDDR(x) (DDR##x)

#define IS_BETWEEN(v, x, y)  (v >= x && v <= y)

#endif /* COMMON_H_ */