/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2016 Nels D. "Chip" Pearson (aka CmdrZin)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * twiSlave.c
 *
 * Created: 8/10/2015	0.01	ndp
 *  Author: Chip
 * revision: 1/29/2016	0.02	ndp	 Add TxBuf[] test to support single read example.
 *
 * Based on the Atmel App Note AVR311 and enhanced to support FIFO data buffers for 
 * input and output.
 *
 * This I2C driver is interrupt driven and uses data FIFOs for input and output buffering.
 * The ISR type for the interrupt support routine causes C to automatically setup the interrupt vector.
 *
 * twiSlaveInit( adrs )			Set up TWI hardware and set Slave I2C Address.
 * twiSlaveEnable()				Enable I2C Slave interface.
 *
 * twiTransmitByte( data )		Place data into output buffer.
 *
 * twiReceiveByte()				Read data from input buffer.
 * twiDataInReceiveBuffer()		Check for available data in input buffer. This function should return 
 *								TRUE before calling twiReceiveByte() to get data.
 * twiDataInTransmitBuffer()	Check for empty TxBuff[]. This function should return FALSE when a new
 *								Read request is received to show that all of the prior data has been sent.
 *								If the buffer is not empty, call twiClearOutput() to recover.
 * twiClearOutput()				Reset the output buffer to empty. Used recover from sync errors.
 *
 * twiStuffRxBuf( data )		Allows manual input into input buffer for testing.
 * 
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>

#include "twiSlave.h"

void flushTwiBuffers( void );

/* *** Local Defines *** */

// TWI Slave Receiver status codes
#define	TWI_SRX_ADR_ACK				0x60  // Own SLA+W has been received ACK has been returned
#define	TWI_SRX_ADR_ACK_M_ARB_LOST	0x68  // Own SLA+W has been received; ACK has been returned
#define	TWI_SRX_GEN_ACK				0x70  // General call address has been received; ACK has been returned
#define	TWI_SRX_GEN_ACK_M_ARB_LOST	0x78  // General call address has been received; ACK has been returned
#define	TWI_SRX_ADR_DATA_ACK		0x80  // Previously addressed with own SLA+W; data has been received; ACK has been returned
#define	TWI_SRX_ADR_DATA_NACK		0x88  // Previously addressed with own SLA+W; data has been received; NOT ACK has been returned
#define	TWI_SRX_GEN_DATA_ACK		0x90  // Previously addressed with general call; data has been received; ACK has been returned
#define	TWI_SRX_GEN_DATA_NACK		0x98  // Previously addressed with general call; data has been received; NOT ACK has been returned
#define	TWI_SRX_STOP_RESTART		0xA0  // A STOP condition or repeated START condition has been received while still addressed as Slave

// TWI Slave Transmitter status codes
#define	TWI_STX_ADR_ACK				0xA8  // Own SLA+R has been received; ACK has been returned
#define	TWI_STX_ADR_ACK_M_ARB_LOST	0xB0  // Own SLA+R has been received; ACK has been returned
#define	TWI_STX_DATA_ACK			0xB8  // Data byte in TWDR has been transmitted; ACK has been received
#define	TWI_STX_DATA_NACK			0xC0  // Data byte in TWDR has been transmitted; NOT ACK has been received
#define	TWI_STX_DATA_ACK_LAST_BYTE	0xC8  // Last byte in TWDR has been transmitted (TWEA = 0); ACK has been received

// TWI Miscellaneous status codes
#define	TWI_NO_STATE				0xF8  // No relevant state information available; TWINT = 0
#define	TWI_BUS_ERROR				0x00  // Bus error due to an illegal START or STOP condition

/* *** Local variables *** */
static uint8_t          rxBuf[ TWI_RX_BUFFER_SIZE ];
static volatile uint8_t rxHead;
static volatile uint8_t rxTail;

static uint8_t          txBuf[ TWI_TX_BUFFER_SIZE ];
static volatile uint8_t txHead;
static volatile uint8_t txTail;

/* *** Local Functions *** */
/*
 * Reset TWI buffers pointers so that the FIFOs will show empty.
 */
void
flushTwiBuffers( void )
{
	rxTail = 0;
	rxHead = 0;
	txTail = 0;
	txHead = 0;
}

/* *** Public Functions *** */
/*
 * Set up TWI hardware and set Slave I2C Address.
 * This is called once during the initialization process.
 * TODO: Allow GLOBAL address.
 */
void
twiSlaveInit( uint8_t adrs )
{
	TWAR = (adrs << 1)|(0);
	
	TWCR = (1<<TWEN)|(0<<TWIE)|(0<<TWINT)|(0<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|(0<<TWWC);
	return;
}

/*
 * Enable I2C Slave
 * This is called to make the Slave ready to receive commands.
 * NOTE: Could set status flags here also.
 */
void
twiSlaveEnable( void )
{
	TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|(0<<TWWC);
}
	
/*
 * Place data into the output buffer if there is available space.
 *
 * When a SDA_R is received, the data is automatically supplied
 * from the Transmit FIFO Buffer TxBuf[].
 * NOTE: Could set status flags here also or return a BUFFER_FULL on error.
 */
void
twiTransmitByte( uint8_t data )
{
	uint8_t tmphead;

	// calculate buffer index
	tmphead = ( txHead + 1 ) & TWI_TX_BUFFER_MASK;

	// check for free space in buffer
	if ( tmphead == txTail )
	{
		return;
	}

	// store data into buffer
	txBuf[ tmphead ] = data;

	// update index
	txHead = tmphead;
}

/*
 * Read data from input buffer.
 * Data is automatically sent to the RxBuf[] FIFO when received from the Master.
 * Call twiDataInReceiveBuffer() and test for TRUE before calling this read function.
 *
 * Return 0x88 if no data is available. ERROR.
 */
uint8_t
twiReceiveByte( void )
{
	// check for available data.
	if ( rxHead == rxTail )
	{
		return 0x88;
	}

	// generate index
	rxTail = ( rxTail + 1 ) & TWI_RX_BUFFER_MASK;

	return rxBuf[ rxTail ];
}

/*
 * Check for available data in input buffer.
 * This function should return TRUE before calling twiReceiveByte() to get data.
 */
bool
twiDataInReceiveBuffer( void )
{
  // return 0 (false) if the receive buffer is empty
  return rxHead != rxTail;
}

/*
 * Check that prior data was been sent.
 * This function should return FALSE when a Read request is received.
 */
bool
twiDataInTransmitBuffer( void )
{
  // return 0 (false) if the transmit buffer is empty
  return txHead != txTail;
}

/*
 * Reset the output buffer to empty. Used to recover from sync errors.
 */
void
twiClearOutput( void )
{
	txTail = 0;
	txHead = 0;
}

/*
 * Also used for manual input into input (rxBuf[]) buffer for testing.
 * NOTE: If RX buffer is full, data is lost.
 * TODO: ERROR condition on data lost.
 */
void
twiStuffRxBuf( uint8_t data )
{
	uint8_t tmphead;

	// calculate buffer index
	tmphead = ( rxHead + 1 ) & TWI_RX_BUFFER_MASK;

	// check for free space in buffer
	if ( tmphead == rxTail )
	{
		return;
	}

	// store data into buffer
	rxBuf[ tmphead ] = data;

	// update index
	rxHead = tmphead;
}

/* *** Interrupt Service Routines *** */

/*
 * TWI Interrupt Service
 * Called by TWI Event
 * This is a simple state machine that services a TWI Event. (see AVR311 for more detail)
 */
ISR( TWI_vect )
{
	switch( TWSR )
	{
		case TWI_SRX_ADR_ACK:				// 0x60 Own SLA+W has been received ACK has been returned. Expect to receive data.
//		case TWI_SRX_ADR_ACK_M_ARB_LOST:	// 0x68 Own SLA+W has been received; ACK has been returned. RESET interface.
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event. Should be DATA.
			break;

		case TWI_SRX_ADR_DATA_ACK:			// 0x80 Previously addressed with own SLA+W; Data received; ACK'd
		case TWI_SRX_GEN_DATA_ACK:			// 0x90 Previously addressed with general call; Data received; ACK'd
			// Put data into RX buffer
			twiStuffRxBuf( TWDR );
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event. Should be more DATA.
  			break;
			
		case TWI_SRX_GEN_ACK:				// 0x70 General call address has been received; ACK has been returned
//		case TWI_SRX_GEN_ACK_M_ARB_LOST:	// 0x78 General call address has been received; ACK has been returned
			// TODO: Set General Address flag
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event. Should be DATA.
			break;

		case TWI_STX_ADR_ACK:				// 0xA8 Own SLA+R has been received; ACK has been returned. Load DATA.
//		case TWI_STX_ADR_ACK_M_ARB_LOST:	// 0xB0 Own SLA+R has been received; ACK has been returned
		case TWI_STX_DATA_ACK:				// 0xB8 Data byte in TWDR has been transmitted; ACK has been received. Load DATA.
			if ( txHead != txTail )
			{
				txTail = ( txTail + 1 ) & TWI_TX_BUFFER_MASK;
				TWDR = txBuf[ txTail ];
			}
			else
			{
				// the buffer is empty. Send 0x88. Too much data was asked for.
				TWDR = 0x88;
			}
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event.
			break;

		case TWI_STX_DATA_NACK:				// 0xC0 Data byte in TWDR has been transmitted; NOT ACK has been received. End of Sending.
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event. Should be new message.
			break;

		case TWI_SRX_STOP_RESTART:			// 0xA0 A STOP condition or repeated START condition has been received while still addressed as Slave
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event.
			break;

		case TWI_SRX_ADR_DATA_NACK:			// 0x88 Previously addressed with own SLA+W; data has been received; NOT ACK has been returned
		case TWI_SRX_GEN_DATA_NACK:			// 0x98 Previously addressed with general call; data has been received; NOT ACK has been returned
		case TWI_STX_DATA_ACK_LAST_BYTE:	// 0xC8 Last byte in TWDR has been transmitted (TWEA = 0); ACK has been received
		case TWI_NO_STATE:					// 0xF8 No relevant state information available; TWINT = 0
		case TWI_BUS_ERROR:					// 0x00 Bus error due to an illegal START or STOP condition
			TWCR =   (1<<TWSTO)|(1<<TWINT);   // Recover from TWI_BUS_ERROR
			// TODO: Set an ERROR flag to tell main to restart interface.
			break;

		default:							// OOPS
			TWCR = (1<<TWEN)|(1<<TWIE)|(1<<TWINT)|(1<<TWEA);		// Prepare for next event. Should be more DATA.
			break;
	}
}
