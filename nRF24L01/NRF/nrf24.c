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

#include "../SPI/spi.h"
#include "nrf24.h"
#include "NrfMemoryMap.h"


#include "../MK_USART/mkuart.h"


volatile uint8_t TransmissionInProgress = 0;
volatile uint8_t RX_flag = 0;

// Receiver buffer
uint8_t RXBuffer[MAXIMUM_PAYLOAD_SIZE + 1];

// Callback pointer
static void (*ReceiverCallback)(uint8_t*, uint8_t);

// Registers callback function
void RegisterRadioCallback(void (*callback)(uint8_t*, uint8_t))
{
	ReceiverCallback = callback;
}

void RadioInitialize(void)
{
	SpiInitialize();
	
	// CE and CSN - outputs
	DDR(CE_PORT) |= (1<<CE);
	DDR(CSN_PORT) |= (1<<CSN);
	
	CE_LOW;
	CSN_HIGH;
	
	RadioConfig();
}

// Configures the device with the most common settings
void RadioConfig(void)
{
	RadioPowerUp();
	RadioSetTransmitterAddress(PSTR("TEST1"));
	RadioSetReceiverAddress(RX_ADDR_P0, PSTR("TEST1"));
	RadioWriteRegisterSingle(CONFIG, RADIO_CONFIG);
	RadioConfigDataPipe(0, 1, 1);
	RadioConfigDataPipe(1, 1, 1);
	RadioEnableCRC();
	RadioSetCRCLength(1);
	RadioConfigRetransmission(ARD_US_500, ARC_1);
	
	RadioSetPower(POWER_DBM_0);
	RadioSetSpeed(MBPS_1);
	
	RadioSetDynamicPayload(0, 1);	
	RadioSetDynamicPayload(1, 1);
	RadioSetChannel(10);
	
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

void RadioWriteRegisterSingle(uint8_t reg, uint8_t value)
{
	CSN_LOW;
	SpiShift(W_REGISTER | (REGISTER_MASK & reg));
	SpiShift(value);
	CSN_HIGH;
}

// Clears TX fifo
void RadioClearTX(void)
{
	CSN_LOW;
	SpiShift(FLUSH_TX);
	CSN_HIGH;	
}

// Clears RX fifo
void RadioClearRX(void)
{
	CSN_LOW;
	SpiShift(FLUSH_RX);
	CSN_HIGH;
}

// Sets the transmitter address 
// USAGE: RadioSetTransmitterAddress(PSTR("MyAddress"))
void RadioSetTransmitterAddress(const char* address)
{
	char RAM_TxAddress[TX_ADDRESS_LENGTH];
	for (uint8_t i = 0; i < TX_ADDRESS_LENGTH; i++)
		RAM_TxAddress[i] = pgm_read_byte(address++);
		
	RadioWriteRegister(TX_ADDR, (uint8_t *) RAM_TxAddress, TX_ADDRESS_LENGTH);
}

// Sets the receiver address for the specified data pipe
// USAGE: RadioSetReceiverAddress(PSTR("MyAddress"))
void RadioSetReceiverAddress(uint8_t dataPipe, const char* address)
{
	dataPipe = ValidateDataPipeAddress(dataPipe);
	
	char RAM_RxAddress[RX_ADDRESS_LENGTH];
	
	if(dataPipe <= RX_ADDR_P1)
	{
		// Pipe 0 and pipe 1 take 3-5 bytes long addresses	
		for(uint8_t i = 0; i < RX_ADDRESS_LENGTH; i++)
		{
			RAM_RxAddress[i] = pgm_read_byte(address++);
		}
		RadioWriteRegister(dataPipe, (uint8_t*) RAM_RxAddress, RX_ADDRESS_LENGTH);
	}
	else
	{
		// Pipes 2-5 take only 1 byte address because the rest is taken from pipe 1 address
		RAM_RxAddress[0] = pgm_read_byte(address);
		RadioWriteRegister(dataPipe, (uint8_t*)RAM_RxAddress, 1);
	}
}

// Checks the RX_DR flag status(used in pooling mode)
uint8_t IsReceivedDataReady(void)
{
	uint8_t status = RadioReadRegisterSingle(STATUS);
	return (status & (1<<RX_DR));
}

// Checks the TX_DS flag, which indicates success of the transmission, in status registry
uint8_t IsDataSentSuccessful(void)
{
	uint8_t status = RadioReadRegisterSingle(STATUS);
	return (status & (1<<TX_DS));
}

// Enables the CRC data package validation
void RadioEnableCRC(void)
{
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	config |= (1<<EN_CRC);
	RadioWriteRegisterSingle(CONFIG, config);
}

// Sets the CRC length
// Legal values: 1, 2. Any other will be discarded 
void RadioSetCRCLength(uint8_t crcLength)
{
	if(crcLength > 2 || crcLength < 1)
		return;
		
	// Get the current config so we can modify it
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	// For 1 byte length:  CRCO byte should be 0
	// For 2 bytes length: CRCO byte should be 1
	config |= ((crcLength - 1) << CRCO);
}

// Powers up the radio
void RadioPowerUp(void)
{
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	config |= (1<<PWR_UP);
	RadioWriteRegisterSingle(CONFIG, config);
	
	// Device needs 1.5ms to power up
	_delay_us(1500);
}

//Powers down the radio
void RadioPowerDown(void)
{
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	config &= ~(1<<PWR_UP);
	RadioWriteRegisterSingle(CONFIG, config);
	
	// Optional: clear device's data buffers 
	RadioClearRX();
	RadioClearTX();
}

// Sets the role of the module to the transmitter
void RadioSetRoleTransmitter(void)
{
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	config &= ~(1<<PRIM_RX);
	RadioWriteRegisterSingle(CONFIG, config);
	
	// High state on CE line prevents the device from entering Standby-I
	CE_HIGH;
	_delay_us(130);
}

// Sets the role of the module to the receiver
void RadioSetRoleReceiver(void)
{
	uint8_t config = RadioReadRegisterSingle(CONFIG);
	config |= (1<<PRIM_RX);
	RadioWriteRegisterSingle(CONFIG, config);
	CE_HIGH;
	_delay_us(130);
}

// Switches into transmitter mode
void RadioEnterTxMode(void)
{
	// TODO: method name possibly misleading. Check the result of loading FIFO with dummy bytes enforce proper mode
	RadioSetRoleTransmitter();
	RadioPowerUp();
		
	TransmissionInProgress = 0;
	RX_flag = 0;
	// NOTE: code above will make the device enter Standby-II
	// OPTIONAL: Load FIFO with some dummy bytes to make the device enter TXMode (130us delay required)
}

// Switches into receiver mode
void RadioEnterRxMode(void)
{
	RadioPowerUp();
	RadioSetRoleReceiver();
	TransmissionInProgress = 0;
	RX_flag = 0;
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
	
	uint8_t en_rxaddr = RadioReadRegisterSingle(EN_RXADDR);
	en_rxaddr |= (1 << dataPipe);
	RadioWriteRegisterSingle(EN_RXADDR, en_rxaddr);
}

// Disables data pipe
void RadioDisableDataPipe(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
	dataPipe = 5;
	
	uint8_t en_rxaddr = RadioReadRegisterSingle(EN_RXADDR);
	en_rxaddr &= ~(1 << dataPipe);
	RadioWriteRegisterSingle(EN_RXADDR, en_rxaddr);
}

// Enables auto ACK on the given data pipe
void RadioEnableAck(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
	dataPipe = 5;
		
	uint8_t en_aa = RadioReadRegisterSingle(EN_AA);
	en_aa |= (1 << dataPipe);
	RadioWriteRegisterSingle(EN_AA, en_aa);
}

// Disables auto ACK
void RadioDisableAck(uint8_t dataPipe)
{
	// Make sure we got a valid data pipe number
	if (dataPipe > 5)
	dataPipe = 5;
	
	uint8_t en_aa = RadioReadRegisterSingle(EN_AA);
	en_aa &= ~(1 << dataPipe);
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
		RadioEnableAck(dataPipe);
	else
		RadioDisableAck(dataPipe);
}

// Sets the payload width on the specified data pipe
void RadioSetPayloadWidth(uint8_t dataPipe, uint8_t width)
{
	if (dataPipe > 5)
		dataPipe = 5;
	// Simple trick: RX_PW_P0 address is 11, if the user enter data pipe number X, X+11 will make a valid registry address
	// See device data sheet, section 9.1
	dataPipe += 11;
	
	RadioWriteRegisterSingle(dataPipe, 0x1F & width);
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
	uint8_t rfSetup = RadioReadRegisterSingle(RF_SETUP);
	rfSetup = ((rfSetup & SPEED_MASK) | speed);
	RadioWriteRegisterSingle(RF_SETUP, rfSetup);
}

// Sets the radio power
// One of POWER_DBM_XXX...
void RadioSetPower(uint8_t power)
{
	// TODO: fix
	uint8_t rfSetup = RadioReadRegisterSingle(RF_SETUP);
	rfSetup = ((rfSetup & POWER_MASK) | power);
	RadioWriteRegisterSingle(RF_SETUP, rfSetup);
}

// Sets the dynamic payload on or off
void RadioSetDynamicPayload(uint8_t dataPipe, uint8_t onOff)
{
	// Data validation
	if(dataPipe > 5)
		dataPipe = 5;
	if(onOff > 1)
		onOff = 1;

	uint8_t dynpd = RadioReadRegisterSingle(DYNPD);
	dynpd |= (onOff << dataPipe);
	RadioWriteRegisterSingle(DYNPD, dynpd);
	
	// To use dynamic payload length it must be enabled in feature registry
	
	uint8_t feature = RadioReadRegisterSingle(FEATURE);
	if (onOff)
		feature |= (1 << EN_DPL);
		
	// If all the data pipes have dynamic payload length disabled, disable it in feature registry
	else if (dynpd == 0)
		feature &= ~(1<<EN_DPL);
	RadioWriteRegisterSingle(FEATURE, feature);
}

// Loads device with data ready to transmit
void RadioLoadPayload(uint8_t* data, uint8_t length)
{	
	CSN_LOW;
	SpiShift(W_TX_PAYLOAD);
	for(uint8_t i = 0; i < length; i++)
		SpiShift(data[i]++);
	CSN_HIGH;
}

// Sends data
void RadioSend(uint8_t* data)
{
	// Wait for transmission to end
	if (TransmissionInProgress == 1)
		return;
		
	// Now we can send data
	TransmissionInProgress = 1;
	
	uint8_t dataLength = strlen((char*)data);
	if (dataLength > MAXIMUM_PAYLOAD_SIZE) dataLength = MAXIMUM_PAYLOAD_SIZE;

	// Presuming device is in Standby-II
	// TODO: 
	RadioLoadPayload(data, dataLength);

	// Keeping CE high makes the device stay in TX mode
	// TODO: power saving mode
	CE_HIGH;
}

// Main event function
// Should be called as often as possible in program's main loop
void RADIO_EVENT(void)
{
	// Pooling mode for now
	uint8_t status = RadioReadRegisterSingle(STATUS);
	
	// Sending data failed
	// TODO: handling this event
	if (MAXIMUM_RETRANSMISSIONS_REACHED(status))
	{
		RadioClearTX();
		
		// Clear IRQ flags
		status |= IRQ_CLEAR_MASK;
		RadioWriteRegisterSingle(STATUS, status);
	}
	
	// Check if sending data was successful
	if (DATA_SEND_SUCCESS(status))
	{
		TransmissionInProgress = 0;
		// TOCO: ACK with payload handling, just clear the buffer from now
		RadioClearRX();
		
		// Clear flags
		// TODO: make a separate function for this
		status |= IRQ_CLEAR_MASK;
		RadioWriteRegisterSingle(STATUS, status);
	}
	
	// Continuously check if there is any data to be read from the device
	if(DATA_RECEIVED(status))
	{
		RX_flag = 1;
	}
	if (RX_flag)
	{
		RX_flag = 0;
		// Prepare for reading data
		uint8_t fifoStatus;
		uint8_t dataLength;
		do 
		{
			// Clears flag
			RadioWriteRegisterSingle(STATUS, (1<<RX_DR));
			
			CSN_LOW;
			
			// If using dynamic width
			SpiShift(R_RX_PL_WID);
			dataLength = SpiShift(NOP);
			CSN_HIGH;
			
			// If data's too big for the buffer discard it
			if( dataLength > MAXIMUM_PAYLOAD_SIZE) break;
			
			// Read payload from the device
			CSN_LOW;
			SpiShift(R_RX_PAYLOAD);
			uint8_t i;
			for(i = 0; i < dataLength; i++)
			{
				RXBuffer[i] = SpiShift(NOP);
			}
			RXBuffer[i] = '\0';
			
			CSN_HIGH;
			// Clear the device buffer
			// TODO: triple buffering
			RadioClearRX();
			
			fifoStatus = RadioReadRegisterSingle(FIFO_STATUS);
		} 
			// Read until RX is empty
			// TODO: triple buffering? or maybe another solution
			while ((fifoStatus & (1<<RX_EMPTY)) == 0);

		// Tell listeners that we have received the data
		// TODO: 0 length data handling
		if(dataLength != 0 && ReceiverCallback) (*ReceiverCallback)(RXBuffer, dataLength);
	}	
}

//////////////////////////////////////////////////////////////////////////

// Validates data pipe number
uint8_t ValidateDataPipeAddress(uint8_t dataPipe)
{
	// Make sure the dataPipe is valid (it should be between P0 and P5 address)
	if(dataPipe > RX_ADDR_P5) 
		return RX_ADDR_P5;
	else if(dataPipe < RX_ADDR_P0) 
		return RX_ADDR_P0;
	else
		return dataPipe;
}