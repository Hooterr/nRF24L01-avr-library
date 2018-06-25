/*
 * nrf24.h
 *
 * Created: 26/01/2018 19:54:22
 *  Author: maxus
 */ 

#ifndef NRF24_H_
#define NRF24_H_

//////////////////////////////////////////////////////////////////////////
// COMPILE-TIME SETTINGS
//////////////////////////////////////////////////////////////////////////
#define CE_PORT B
#define CE 0

#define CSN_PORT B
#define CSN 1

//pcint23
#define IRQ_PORT D
#define IRQ 7

#define TX_ADDRESS_LENGTH 5
#define RX_ADDRESS_LENGTH 5

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// define using IRQ (1 - use IRQ, 0 - don't use IRQ)															//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define USE_IRQ 1

//////////////////////////////////////////////////////////////////////////
// METHODS
//////////////////////////////////////////////////////////////////////////
void RadioInitialize(void);
void RegisterRadioCallback(void (*callback)(uint8_t*, uint8_t));
void RadioInitialize(void);
void RadioConfig(void);
void RadioReadRegister(uint8_t reg, uint8_t* buffer, uint8_t len);
uint8_t RadioReadRegisterSingle(uint8_t reg);
void RadioWriteRegister(uint8_t reg, uint8_t* value, uint8_t len);
void RadioWriteRegisterSingle(uint8_t reg, uint8_t value);
void RadioClearTX(void);
void RadioClearRX(void);
void RadioSetTransmitterAddress(const char* address);
void RadioSetReceiverAddress(uint8_t dataPipe, const char* address);
uint8_t IsReceivedDataReady(void);
uint8_t IsDataSentSuccessful(void);
void RadioEnableCRC(void);
void RadioSetCRCLength(uint8_t crcLength);
void RadioPowerUp(void);
void RadioPowerDown(void);
void RadioEnterTxMode(void);
void RadioEnterRxMode(void);
void RadioSetChannel(uint8_t channel);
void RadioEnableDataPipe(uint8_t dataPipe);
void RadioDisableDataPipe(uint8_t dataPipe);
void RadioConfigureInterrupts(void);
void RadioEnableAutoAck(uint8_t dataPipe);
void RadioDisableAck(uint8_t dataPipe);
void RadioConfigDataPipe(uint8_t dataPipe, uint8_t onOff, uint8_t AutoAckOnOff);
void RadioSetStaticPayloadWidth(uint8_t dataPipe, uint8_t width);
void RadioConfigRetransmission(uint8_t time, uint8_t ammount);
void RadioSetSpeed(uint8_t speed);
void RadioSetPower(uint8_t power);
void RadioSetDynamicPayload(uint8_t dataPipe, uint8_t onOff);
void RadioLoadPayload(uint8_t* data, uint8_t length);
void RadioSend(uint8_t* data);
void RADIO_EVENT(void);
void RadioPrintConfig(void(*printString)(char*), void(*printChar)(char), void(*printNumber)(int number, int raddix));
//////////////////////////////////////////////////////////////////////////
// Variables
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// HELPERS
//////////////////////////////////////////////////////////////////////////
#define CE_LOW PORT(CE_PORT) &= ~(1<<CE)
#define CE_HIGH PORT(CE_PORT) |= (1<<CE)

#define CSN_LOW PORT(CSN_PORT) &= ~(1<<CSN)
#define CSN_HIGH PORT(CSN_PORT) |= (1<<CSN)

#define DATA_RECEIVED_MASK (1<<RX_DR)
#define DATA_SENT_MASK (1<<TX_DS)
#define MAX_RETRANSMISSION_MASK (1<<MAX_RT)

#define MAXIMUM_RETRANSMISSIONS_REACHED(x) (x & MAX_RETRANSMISSION_MASK)
#define DATA_SEND_SUCCESS(x) (x & DATA_SENT_MASK)
#define ACK_RECEIVED(x)		 (x & ACK_RECEIVED_MASK)
#define DATA_RECEIVED(x)	 (x & DATA_RECEIVED_MASK)

#define IRQ_CLEAR_MASK ((1<<MAX_RT) | (1<<TX_DS) | (1<<RX_DR))

#define POWER_DOWN	1
#define STANDBY_1	2
#define STANDBY_2	3
#define RX_MODE		4
#define TX_MODE		5

#define ROLE_TRANSMITTER 1
#define ROLE_RECEIVER	 2

#define INTERRUPTS_MASK	0x70

//////////////////////////////////////////////////////////////////////////
// COMPILE TIME ERROR CHECKS
//////////////////////////////////////////////////////////////////////////

#if (RX_ADDRESS_LENGTH < 3 || RX_ADDRESS_LENGTH > 5)
#error "RX_ADDRESS_LENGTH must be between 3 and 5!"
#endif 

#if (TX_ADDRESS_LENGTH < 3 || TX_ADDRESS_LENGTH > 5)
#error "TX_ADDRESS_LENGTH must be between 3 and 5!"
#endif


#endif /* NRF24_H_ */