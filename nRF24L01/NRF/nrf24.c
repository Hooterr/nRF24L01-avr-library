/*
 * nrf24.c
 *
 * Created: 26/01/2018 19:54:00
 *  Author: maxus
 */
#include "../Common/Common.h"


#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "SPI/spi.h"
#include "nrf24.h"
#include "NrfMemoryMap.h"

#include "../MK_USART/mkuart.h"

// Indicates if transmission is in progress
volatile uint8_t TransmissionInProgress = 0;

// Indicates if any data has been received
volatile uint8_t ReceivedDataReady = 0;

// Buffer for received data
uint8_t RXBuffer[MAXIMUM_PAYLOAD_SIZE + 1];

// Pointer to a callback function defined by the user
static void (*ReceiverCallback)(uint8_t*, uint8_t);

// Device state as a variable
volatile uint8_t State = POWER_DOWN;

volatile uint8_t Role = ROLE_TRANSMITTER;

// Registers callback function
void RegisterRadioCallback(void (*callback)(uint8_t*, uint8_t))
{
	ReceiverCallback = callback;
}

// Initializes the device and configures it ready to use
void RadioInitialize(void)
{
	// SPI is required to communicate with the device
	SpiInitialize();
	
	// CE and CSN - outputs
	DDR(CE_PORT) |= (1<<CE);
	DDR(CSN_PORT) |= (1<<CSN);
	
	// Makes the device enter Standby-I (see data sheet)
	CE_LOW;
	
	// CSN is SS pin, we're not talking to the device right now so set it high
	CSN_HIGH;
	
	// Start up delay
	_delay_ms(100);
	
	// Configure the device ready to use
	RadioConfig();
}

// Configures the device with the most common settings, and settings defined in config file
void RadioConfig(void)
{
	// Device registers can be read and set even though the device is in power down mode
	// Useful for battery powered devices
	RadioPowerDown();
	
	// Device will send data on this address
	RadioSetTransmitterAddress(PSTR("TEST1"));
	
	// Set receiver address for data pipe 0
	RadioSetReceiverAddress(DATA_PIPE_0, PSTR("TEST1"));
	
	// You can configure data pipes like this...
	//RadioEnableDataPipe(DATA_PIPE_0);
	//RadioEnableAutoAck(DATA_PIPE_0);
	
	// Or like this...
	RadioConfigDataPipe(DATA_PIPE_0, 1, 1);
	
	// Payload width can be either static or dynamic 
	RadioSetDynamicPayload(DATA_PIPE_0, 1);
	
	// Common for all the data pipes
	RadioEnableCRC();
	RadioSetCRCLength(1);
	
	// Retransmission settings
	// NOTE (copied from data sheet): If the ACK payload is more than 15 byte in 2Mbps mode the
	// ARD must be 500?S or more, if the ACK payload is more than 5byte in 1Mbps mode the ARD must be
	// 500?S or more. In 250kbps mode (even when the payload is not in ACK) the ARD must be 500?S or more.
	RadioConfigRetransmission(ARD_US_1000, ARC_3);
	
	// Configure interrupts settings
	RadioConfigureInterrupts();
	
	// Power and speed settings
	// 0DBM is more powerful than -18DBM (physics)
	RadioSetPower(POWER_DMB_MINUS_6);
	RadioSetSpeed(MBPS_1);

	// Radio channel or the frequency
	// Device is frequency is equal to: 2.4GHz + (this method's argument value)MHz
	// Here: 2.410 GHz
	RadioSetChannel(10);
	
	// Clear device's data buffers 
	RadioClearRX();
	RadioClearTX();
}

// Reads register to the buffer
void RadioReadRegister(uint8_t reg, uint8_t* buffer, uint8_t len)
{
	CSN_LOW;
	SpiShift(R_REGISTER | (REGISTER_MASK & reg));
	for(uint8_t i = 0; i < len; i++)
		buffer[i] = SpiShift(NOP);

	CSN_HIGH;
}

// Reads a single-byte register
uint8_t RadioReadRegisterSingle(uint8_t reg)
{
	CSN_LOW;
	SpiShift(R_REGISTER | (REGISTER_MASK & reg));
	uint8_t respone = SpiShift(NOP);
	CSN_HIGH;
	return respone;
}

// Writes register with the given value and length
void RadioWriteRegister(uint8_t reg, uint8_t* value, uint8_t len)
{
	CSN_LOW;
	SpiShift(W_REGISTER | (REGISTER_MASK & reg));
	for(uint8_t i = 0; i < len; i++)
		SpiShift(value[i]);

	CSN_HIGH;
}

// Writes a single-byte register
void RadioWriteRegisterSingle(uint8_t reg, uint8_t value)
{
	CSN_LOW;
	SpiShift(W_REGISTER | (REGISTER_MASK & reg));
	SpiShift(value);
	CSN_HIGH;
}

// Clears TX(transmitter) FIFO
// The device can store 3 different payloads using the FirstInFirstOut(FIFO) principle 
void RadioClearTX(void)
{
	CSN_LOW;
	SpiShift(FLUSH_TX);
	CSN_HIGH;	
}

// Clears RX(receiver) FIFO
void RadioClearRX(void)
{
	CSN_LOW;
	SpiShift(FLUSH_RX);
	CSN_HIGH;
}

// Configures interrupts
void RadioConfigureInterrupts(void)
{
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);

	// RADIO_CONFIG is defined based on whether the user wants to enable interrupts or not
	config |= RADIO_CONFIG;
	
	// Save it to the device
	RadioWriteRegisterSingle(CONFIG, config);
}

// Sets the transmitter address 
// USAGE: RadioSetTransmitterAddress(PSTR("Address"))
// NOTE: Remember about address length you either defined in compile-time or set from the code
//		 If the address you want to set exceeds this limit, LSBytes are skipped 
void RadioSetTransmitterAddress(const char* address)
{
	if (address == NULL)
		return;
	// Buffer in RAM to save data read from the flash memory
	char RAM_TxAddress[TX_ADDRESS_LENGTH];
	
	for (uint8_t i = 0; i < TX_ADDRESS_LENGTH; i++)
		RAM_TxAddress[i] = pgm_read_byte(address++);
	
	RadioWriteRegister(TX_ADDR, (uint8_t *) RAM_TxAddress, TX_ADDRESS_LENGTH);
}

// Sets the receiver address for the specified data pipe
// USAGE: RadioSetReceiverAddress(PSTR("Address"))
void RadioSetReceiverAddress(uint8_t dataPipe, const char* address)
{
	// Make sure data pipe number is legal
	if (dataPipe > 5)
		dataPipe = 5;
		
	// TODO: address width setting
	RadioWriteRegisterSingle(SETUP_AW, 0x03);
	
	// RX_ADDR_PX is the registry we need to write the address to.
	// RX_ADDR_P0 is 0x0A, RX_ADDR_P1 is 0x0B
	uint8_t registerAddress = dataPipe + 0x0A;
	
	// Buffer in RAM to read data from flash
	char RAM_RxAddress[RX_ADDRESS_LENGTH];
	
	// Data pipe 0 and 1 take 3-5 bytes long addresses
	if(dataPipe <= DATA_PIPE_1)
	{
		for(uint8_t i = 0; i < RX_ADDRESS_LENGTH; i++)
		{
			RAM_RxAddress[i] = pgm_read_byte(address++);
		}
		RadioWriteRegister(registerAddress, (uint8_t*) RAM_RxAddress, RX_ADDRESS_LENGTH);
	}
	// Pipes 2-5 take only 1 byte address because the rest is taken from pipe 1 address
	else
	{	RAM_RxAddress[0] = pgm_read_byte(address);
		RadioWriteRegister(registerAddress, (uint8_t*)RAM_RxAddress, 1);
	}
}

// Checks the RX_DR flag status(used in pooling mode)
uint8_t IsReceivedDataReady(void)
{
	uint8_t status = RadioReadRegisterSingle(STATUS);
	return (status & (1<<RX_DR));
}

// Checks the TX_DS flag status, which indicates if transmission was successful
uint8_t IsDataSentSuccessful(void)
{
	uint8_t status = RadioReadRegisterSingle(STATUS);
	return (status & (1<<TX_DS));
}

// Enables the CRC data package validation
// Useful if you want to send important data. Makes transmission slower but more reliable
void RadioEnableCRC(void)
{
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// Enable CRC
	config |= (1<<EN_CRC);
	
	// Save it to the device
	RadioWriteRegisterSingle(CONFIG, config);
}

// Sets the CRC length
// Legal values: 1, 2. Any other will be discarded.
void RadioSetCRCLength(uint8_t crcLength)
{
	if(crcLength > 2 || crcLength < 1)
		return;
		
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// For 1 byte length:  CRCO byte should be 0
	// For 2 bytes length: CRCO byte should be 1
	config |= ((crcLength - 1) << CRCO);
	
	// Save data to the device
	RadioWriteRegisterSingle(CONFIG, config);
}

// Powers up the radio
void RadioPowerUp(void)
{
	// If the device is already powered up don't do anything
	if (State != POWER_DOWN)
		return;
	
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// Set PWR_UP and CE to enter Standby-I
	config |= (1<<PWR_UP);
	CE_LOW;
	
	// Write this config to the device
	RadioWriteRegisterSingle(CONFIG, config);
	
	// Device needs 1.5ms to power up
	_delay_us(1500);
	
	// Set appropriate state
	State = STANDBY_1;
}

// Powers down the device
void RadioPowerDown(void)
{
	// If the device has already been powered down don't do anything
	if (State == POWER_DOWN)
		return;
	
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// Clearing PWR_UP bit will make the device enter PowerDown mode (see data sheet)
	config &= ~(1<<PWR_UP);
	
	// Clear CE line as a matter of principle (don't really matter)
	CE_LOW;
	
	// Save this config to the device
	RadioWriteRegisterSingle(CONFIG, config);
	
	// Set appropriate state
	State = POWER_DOWN;
	
	// Optional: clear device's data buffers 
	RadioClearRX();
	RadioClearTX();
}

// Sets the role of the module to the transmitter
void RadioSetRoleTransmitter(void)
{
	// No need to change anything
	if (Role == ROLE_TRANSMITTER)
		return;
		
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// Clear PRIM_RX to set transmitter mode
	config &= ~(1<<PRIM_RX);
	
	// Save this config to the device
	RadioWriteRegisterSingle(CONFIG, config);
		
	// Set appropriate role
	Role = ROLE_TRANSMITTER;
}

// Sets the role of the module to the receiver
void RadioSetRoleReceiver(void)
{
	// No need to change anything
	if(Role == ROLE_RECEIVER)
		return;
	
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	
	// Set PRIM_RX to set transmitter mode
	config |= (1<<PRIM_RX);
	
	// Save this config to the device
	RadioWriteRegisterSingle(CONFIG, config);
	
	// Set appropriate role
	Role = ROLE_RECEIVER;
}

// Switches into transmitter mode
// NOTE: What method really does is entering Standby-I, but for the sake of consistency
//		 it's named how it's named
void RadioEnterTxMode(void)
{
	// Get rid of any data in the device so it's got free buffer to use
	RadioClearTX();
	
	// If set high device would enter Standby-II, which is not efficient in this case
	CE_LOW;
	
	RadioSetRoleTransmitter();
	
	// If the radio needs to be powered up, do it
	if (State == POWER_DOWN)
		RadioPowerUp();
	
	// Set appropriate state
	State = STANDBY_1;
	
	TransmissionInProgress = 0;
	ReceivedDataReady = 0;
}

// Switches into receiver mode
void RadioEnterRxMode(void)
{
	// If the device already is in RX mode or there is a transmission on air, don't do anything
	if(State == RX_MODE || TransmissionInProgress == 1)
		return;
	
	RadioSetRoleReceiver();
	
	// Clear the device of any unread data
	RadioClearRX();
	
	// If the device is not up already, power it up
	if(State == POWER_DOWN)
		RadioPowerUp();
	
	// Keeps the device in RX mode. Clear to come back to Standby-I
	CE_HIGH;
	
	// Delay required by the device
	_delay_us(130);
	
	// Set appropriate state
	State = RX_MODE;
	
	// Set initial values
	TransmissionInProgress = 0;
	ReceivedDataReady = 0;
}

// Sets the radio channel (radio frequency)
void RadioSetChannel(uint8_t channel)
{
	// First bit in RF_CH must always be 0
	RadioWriteRegisterSingle(RF_CH, 0b01111111 & channel);
}

// Enables data pipe
void RadioEnableDataPipe(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
		dataPipe = 5;
	
	// Get the current value so we can modify it
	uint8_t en_rxaddr = RadioReadRegisterSingle(EN_RXADDR);
	
	// Write one to enable this data pipe
	en_rxaddr |= (1 << dataPipe);
	
	// Save the value to the device
	RadioWriteRegisterSingle(EN_RXADDR, en_rxaddr);
}

// Disables data pipe
void RadioDisableDataPipe(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
		dataPipe = 5;
	
	// Get the current value so we can modify it
	uint8_t en_rxaddr = RadioReadRegisterSingle(EN_RXADDR);
	
	// Clear the bit to enable this data pipe
	en_rxaddr &= ~(1 << dataPipe);
	
	// Save the value to the device
	RadioWriteRegisterSingle(EN_RXADDR, en_rxaddr);
}

// Enables auto ACK on the given data pipe
void RadioEnableAutoAck(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
		dataPipe = 5;
		
	// Get the current value so we can modify it
	uint8_t en_aa = RadioReadRegisterSingle(EN_AA);
	
	// Write one to enable auto ACK on this data pipe
	en_aa|= (1 << dataPipe);
	
	// Save the value to the device
	RadioWriteRegisterSingle(EN_AA, en_aa);
}

// Disables auto ACK
void RadioDisableAck(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
		dataPipe = 5;
	
	// Get the current value so we can modify it
	uint8_t en_aa = RadioReadRegisterSingle(EN_AA);
	
	// Clear the bit to disable auto ACK on this data pipe
	en_aa &= ~(1 << dataPipe);
	
	// Save the value to the device
	RadioWriteRegisterSingle(EN_AA, en_aa);
}

// Complex data pipe configuration
void RadioConfigDataPipe(uint8_t dataPipe, uint8_t onOff, uint8_t AutoAckOnOff)
{
	if(onOff)
		RadioEnableDataPipe(dataPipe);
	else
		RadioDisableDataPipe(dataPipe);
		
	if(AutoAckOnOff)
		RadioEnableAutoAck(dataPipe);
	else
		RadioDisableAck(dataPipe);
}

// Sets static payload width on the specified data pipe
void RadioSetStaticPayloadWidth(uint8_t dataPipe, uint8_t width)
{
	// Make sure we got a valid data pipe number
	if(dataPipe > 5)
		dataPipe = 5;
	// Simple trick: RX_PW_P0 address is 11, if the user enters data pipe number X, 
	//				 X+11 will make a valid registry address
	// See device data sheet, section 9.1
	dataPipe += 0x11;
	
	// Two MSB must always be 0
	RadioWriteRegisterSingle(dataPipe, 0b00111111 & width);
}

// Configures retransmission parameters
// Time is one of ARD_US_XXXX, and ammount one of ARC_XX
void RadioConfigRetransmission(uint8_t time, uint8_t ammount)
{
	RadioWriteRegisterSingle(SETUP_RETR, time | ammount);
}

// Sets the transmission speed
// MBPS_1 or MPBS_2 or KBPS_250
void RadioSetSpeed(uint8_t speed)
{
	// TODO: fix
	// Get the current setup so we can modify it
	uint8_t rfSetup = RadioReadRegisterSingle(RF_SETUP);
	
	// Use mask to write bits correctly
	rfSetup = ((rfSetup & SPEED_MASK) | speed);
	
	// Write the value to the device
	RadioWriteRegisterSingle(RF_SETUP, rfSetup);
}

// Sets the radio power
// One of POWER_DBM_XXX...
void RadioSetPower(uint8_t power)
{
	// TODO: fix
	// Get the current setup so we can modify it
	uint8_t rfSetup = RadioReadRegisterSingle(RF_SETUP);
	
	// Use mask to write bits correctly
	rfSetup = ((rfSetup & POWER_MASK) | power);
	
	// Write the value to the device
	RadioWriteRegisterSingle(RF_SETUP, rfSetup);
}

// Sets the dynamic payload on or off
void RadioSetDynamicPayload(uint8_t dataPipe, uint8_t onOff)
{
	// Data validation
	if(dataPipe > 5)
		dataPipe = 5;
	
	// Get the current config so we can modify it
	uint8_t dynpd = RadioReadRegisterSingle(DYNPD);
	
	// Write one to enable; zero to disable dynamic payload with
	if(onOff)
		dynpd |= (1 << dataPipe);
	else
		dynpd &= (1 << dataPipe);
	
	// Write value to the device
	RadioWriteRegisterSingle(DYNPD, dynpd);
	
	// To use dynamic payload length it must be enabled in feature registry
	
	// Get current FEATURE registry value so we can modify it
	uint8_t feature = RadioReadRegisterSingle(FEATURE);
	
	// If function was called to enable dynamic width, enable it in feature registry
	if (onOff)
		feature |= (1 << EN_DPL);
		
	// If all the data pipes have dynamic payload length disabled, disable it in feature registry
	else if (dynpd == 0)
		feature &= ~(1<<EN_DPL);
		
	// Write the value to the device
	RadioWriteRegisterSingle(FEATURE, feature);
}

// Loads device with data ready to transmit
void RadioLoadPayload(uint8_t* data, uint8_t length)
{	
	CSN_LOW;
	
	// To write data to TX FIFO you need to start transmission with W_TX_PAYLOAD
	SpiShift(W_TX_PAYLOAD);
	
	// Write all the data
	for(uint8_t i = 0; i < length; i++)
		SpiShift(data[i]++);
	
	CSN_HIGH;
}

// Sends data
// NOTE: Make sure the device is in TX mode before calling this method
void RadioSend(uint8_t* data)
{
	// Wait for previous transmission to end
	// Also cannot send data when in RX mode
	// NOTE: before calling make sure that RadioEnterTxMode() had been called
	if (TransmissionInProgress == 1 || State != STANDBY_1)
		return;
	
	// If transmitter mode is already set won't change anything, 
	// but if receiver mode is set this will set proper mode
	RadioSetRoleTransmitter();
	
	// Get the length of the data 
	uint8_t dataLength = strlen((char*)data);
	
	// Make sure it does not exceed the limit
	if (dataLength > MAXIMUM_PAYLOAD_SIZE) 
		dataLength = MAXIMUM_PAYLOAD_SIZE;

	// Presuming device is in Standby-I
	RadioLoadPayload(data, dataLength);
	
	// 10µs high pulse on CE starts transmission
	CE_HIGH;
	_delay_us(10);

	// TX settings delay
	// NOTE: can be omitted
	_delay_us(130);
	CE_LOW;		
	
	// Indicate operation
	TransmissionInProgress = 1;
	State = TX_MODE;
}

uint8_t RadioReadData(void)
{
	// Prepare for reading data
	uint8_t fifoStatus;
	uint8_t dataLength;
	do
	{
		CSN_LOW;
		
		// If using dynamic width
		SpiShift(R_RX_PL_WID);
		dataLength = SpiShift(NOP);
		CSN_HIGH;
		
		// If data's too big for the buffer discard it and clear the device buffer
		if( dataLength > MAXIMUM_PAYLOAD_SIZE)
		{
			RadioClearRX();
			return 0;
		}
		
		// Read payload from the device
		CSN_LOW;
		SpiShift(R_RX_PAYLOAD);
		uint8_t i;
		for(i = 0; i < dataLength; i++)
			RXBuffer[i] = SpiShift(NOP);
		CSN_HIGH;
		
		// Add the null character at the end (useful for transmitting strings)
		RXBuffer[i] = '\0';
		
		// Clear the device buffer
		// TODO: triple buffering
		RadioClearRX();
		
		// TODO: There may be data in buffer that comes from different data pipes
		fifoStatus = RadioReadRegisterSingle(FIFO_STATUS);
	}
	// Read until RX is empty
	// TODO: triple buffering? or maybe another solution
	while ((fifoStatus & (1<<RX_EMPTY)) == 0);
	
	return dataLength;
}

// Main event function
// Should be called as often as possible in program's main loop
void RADIO_EVENT(void)
{
	// Pooling mode for now
	uint8_t status = RadioReadRegisterSingle(STATUS);
	
	//uart_putint(status, 16);
	//uart_putc('\n');
	//_delay_ms(100);
	
	// Check if sending data was successful
	if (DATA_SEND_SUCCESS(status))
	{
		// TOCO: ACK with payload handling, just clear the buffer for now
		RadioClearRX();
			
		// Clear flag
		status |= (1<<TX_DS);
		RadioWriteRegisterSingle(STATUS, status);
			
		TransmissionInProgress = 0;
		State = STANDBY_1;
		uart_puts("Data sent successful\n");
	}
	
	// Sending data failed
	// TODO: handling this event
	if (MAXIMUM_RETRANSMISSIONS_REACHED(status))
	{
		// Clear IRQ flag
		status |= (1<< MAX_RT);
		RadioWriteRegisterSingle(STATUS, status);
		
		RadioClearTX();
		TransmissionInProgress = 0;
		State = STANDBY_1;
		
		uart_puts("Max retransmissions\n");
	}
	
	// Continuously check if there is any data to be read from the device
	if(DATA_RECEIVED(status))
	{
		ReceivedDataReady = 1;
	}
	
	if (ReceivedDataReady)
	{
		// Indicate we have received data
		ReceivedDataReady = 0;
		
		// Clear flag
		status |= (1<<RX_DR);
		RadioWriteRegisterSingle(STATUS, status);
		
		uint8_t dataLength = RadioReadData();		
		
		// Tell listeners that we have received the data, make sure, however, that length is not 0
		if(dataLength != 0 && ReceiverCallback) (*ReceiverCallback)(RXBuffer, dataLength);
	}	
}

//////////////////////////////////////////////////////////////////////////
// UTILITIES
//////////////////////////////////////////////////////////////////////////

// Helper to print register values
void print(uint8_t* bufor, uint8_t len, void(*printNumber)(int number, int raddix), void(*printChar)(char))
{
	for(uint8_t i = 0; i < len; i++)
		printNumber(bufor[i], 16);
	printChar('\n');
}

// Prints out the device config
// Parameters are UART methods: method to print a string
//							    method to print a single character
//								method to print a number in a given format (16 - hex, 2 - bin, etc.)
void RadioPrintConfig(void(*printString)(char*), void(*printChar)(char), void(*printNumber)(int number, int raddix))
{
	uint8_t buf[5];
	// TODO: PSTR here

	RadioReadRegister(CONFIG, buf, 1);
	printString("CONFIG: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(EN_AA, buf, 1);
	printString("EN_AA: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(EN_RXADDR, buf, 1);
	printString("EN_RXADDR: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(SETUP_AW, buf, 1);
	printString("SETUP_AW: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(SETUP_RETR, buf, 1);
	printString("SETUP_RETR: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RF_CH, buf, 1);
	printString("RF_CH: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RF_SETUP, buf, 1);
	printString("RF_SETUP: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(STATUS, buf, 1);
	printString("STATUS: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(OBSERVE_TX, buf, 1);
	printString("OBSERVE_TX: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RPD, buf, 5);
	printString("RPD: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P0, buf, 5);
	printString("RX_ADDR_P0: ");
	print(buf, 5, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P1, buf, 5);
	printString("RX_ADDR_P1: ");
	print(buf, 5, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P2, buf, 1);
	printString("RX_ADDR_P2: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P3, buf, 1);
	printString("RX_ADDR_P3: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P4, buf, 1);
	printString("RX_ADDR_P4: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_ADDR_P5, buf, 1);
	printString("RX_ADDR_P5: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(TX_ADDR, buf, 5);
	printString("TX_ADDR: ");
	print(buf, 5, printNumber, printChar);
	RadioReadRegister(RX_PW_P0, buf, 1);
	printString("RX_PW_P0: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_PW_P1, buf, 1);
	printString("RX_PW_P1: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_PW_P2, buf, 1);
	printString("RX_PW_P2: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_PW_P3, buf, 1);
	printString("RX_PW_P3: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_PW_P4, buf, 1);
	printString("RX_PW_P4: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(RX_PW_P5, buf, 1);
	printString("RX_PW_P5: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(FIFO_STATUS, buf, 1);
	printString("FIFO_STATUS: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(DYNPD, buf, 1);
	printString("DYNPD: ");
	print(buf, 1, printNumber, printChar);
	RadioReadRegister(FEATURE, buf, 1);
	printString("FEATURE: ");
	print(buf, 1, printNumber, printChar);

	// Debugging purpose 
	printString("TransmissionInProgress: ");
	printNumber(TransmissionInProgress, 10);
	printChar('\n');

	printString("ReceivedDataReady: ");
	printNumber(ReceivedDataReady, 10);
	printChar('\n');
	
	printString("State: ");
	printNumber(State, 10);
	printChar('\n');
	
	printString("Role: ");
	printNumber(Role, 10);
	printChar('\n');
}