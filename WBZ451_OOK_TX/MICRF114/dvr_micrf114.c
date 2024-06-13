// <editor-fold defaultstate="collapsed" desc="File Header">
/***********************************************************************************************************************
 *
 * Filename:   dvr_micrf112.c
 *
 * Global Designator: MICRF_
 *
 * Contents:  Interfaces to the MICRF112 module.  The ISR in this module sends the data out to the MICRF112.
 * 
 ***********************************************************************************************************************
 * � 2023 Microchip Technology Inc. and its subsidiaries.  You may use this software and any derivatives exclusively
 * with Microchip products. 
 * 
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS
 * SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION. 
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE,
 * COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF
 * THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON
 * ALL CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY, THAT YOU HAVE PAID
 * DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 * 
 * MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE TERMS. 
 ***********************************************************************************************************************
 * MICRF112 Pin out:
 *   J1-1: Vdd (3V)
 *   J1-2: GND
 *   J1-3: Data Input (Data out from Micro)
 * 
 *   J2-1: Enable
 *   J2-2: GND
 *   J2-3: EXT REFOSC - Not connected
 **********************************************************************************************************************/
// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="Include Files">
/* ****************************************************************************************************************** */
/* INCLUDE FILES */

#include "dvr_micrf114.h"
#include <string.h>
#include "definitions.h"

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="Macro Definitions">
/* ****************************************************************************************************************** */
/* MACRO DEFINITIONS */

#define MICRF_TX_LEN_MIN        ((uint8_t)1)                    /* Minimum number of bytes in the data payload. */

#define MICRF_SCL_ENABLE()          MICRF_SCL_Set(); MICRF_SCL_OutputEnable() /* Enable the Transmitter */
#define MICRF_SCL_DISABLE()         MICRF_SCL_Clear();  MICRF_SCL_OutputEnable() /* Disable the Transmitter */

#define MICRF_SDA_PIN_HIGH()     MICRF_SDA_Set()              /* Sets the TX pin High */
#define MICRF_SDA_PIN_LOW()      MICRF_SDA_Clear()               /* Sets the TX pin Low */
#define MICRF_SDA_PIN_CFG()      MICRF_SDA_OutputEnable(); MICRF_SDA_PIN_HIGH()    /* Configure the TX pin */

#define BITS_IN_BYTE            ((uint8_t)8)

#define TIMER_INIT()            TC0_TimerInitialize()       /* Initialize the timer used for transmitting */
#define TIMER_ENABLE()          TC0_TimerStart()              /* Starts the transmitter */
#define TIMER_DISABLE()         TC0_TimerStop()               /* Stops the transmitter */
//#define TIMER_CALLBACK(x)       TC0_TimerCallbackRegister(x)     /* Sets the interrupt handler or call-back */


// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="Type Definitions">
/* ****************************************************************************************************************** */
/* TYPE DEFINITIONS */

typedef enum
{
    eTRAINING,                              // Transmission State - Sending Training Bytes
    ePREAMBLE,                              // Transmission State - Sending the Preamble
    eDATA                                   // Transmission State - Sending the data bytes
}eState_t;                                  // State machine for the transmission interrupt routine

typedef struct
{
    uint8_t *pData;                         // Pointer to data buffer
    uint8_t cnt;                            // Number of bytes in the data field
}appData_t;                                 // Packet to transmit

typedef struct
{
    uint8_t         *pData;                 // Pointer to data being transmitted
    eState_t        eState;                 // State of the transmitter
    uint16_t        manchesterData;         // Manchester data to send
    uint8_t         cnt;                    // Number of bytes left to send
    uint8_t         byteToSend;             // Current byte being sent
    uint8_t         bitCnt;                 // Bit number being sent
    bool            bNextBit;               // Contains the next bit to send
    bool            bStopTx;                // Stops the transmitting of data
    bool            bComplete;              // Data complete.
}transmit_t;

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="Constant Definitions">
/* ****************************************************************************************************************** */
/* CONSTANTS */

static const uint8_t training_[] = {0xAA, 0xAA, 0xAA, 0xAA};    /* Training bytes */
static const uint8_t preamble_[] = {0x3A};                      /* Preamble */

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="File Variables - Static">
/* ****************************************************************************************************************** */
/* FILE VARIABLE DEFINITIONS */

static volatile transmit_t   txInfo_;   // Transmitter status information
static          appData_t    appData_;  // Data to transmit

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="Local Function Prototypes">
/* ****************************************************************************************************************** */
/* FUNCTION PROTOTYPES */

static void transmitData(void);
static uint16_t manchesterEncode(uint8_t data);

/* Not really a local function, because the ISR calls it. */
void MICRF_isr(TC_TIMER_STATUS status, uintptr_t context);

// </editor-fold>

/* ****************************************************************************************************************** */
/* FUNCTION DEFINITIONS */

// <editor-fold defaultstate="collapsed" desc="void MICRF_init(void)">
/***********************************************************************************************************************
 *
 * Function Name: MICRF_init
 *
 * Purpose: Initializes the transmitter module
 *
 * Arguments: None
 *
 * Returns: N/A
 *
 * Side Effects: N/A
 *
 * Reentrant Code: No
 *
 **********************************************************************************************************************/
void MICRF_init(void)
{
    MICRF_SCL_ENABLE();
    MICRF_SDA_PIN_CFG();
    TIMER_DISABLE();
    txInfo_.bComplete = true;
}
/* ****************************************************************************************************************** */
// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="void MICRF_transmit(void *pData, uint8_t cnt)">
/***********************************************************************************************************************
 *
 * Function Name: MICRF_transmit
 *
 * Purpose: Transmits a packet of data.  The count is checked to make sure it is within range.
 *
 * Arguments: void *pData, uint8_t cnt
 *
 * Returns: eMICRF_response_t
 *
 * Side Effects: Transmitter is enabled
 *
 * Reentrant Code: No
 *
 **********************************************************************************************************************/
eMICRF_response_t MICRF_transmit(volatile void *pData, uint8_t cnt)
{
    eMICRF_response_t retVal = eMICRF_dataLength; // Assume the data length will be incorrect.
    
    if (MICRF_TX_LEN_MIN <= cnt)    // Validate the data length
    {
        appData_.pData = (uint8_t *)pData;  // Store the pointer info
        appData_.cnt = cnt;                 // Store the counter info
        transmitData();                     // Start transmitting the data
        retVal = eMICRF_success;            // Return success!
    }
    return(retVal);
}
/* ****************************************************************************************************************** */
// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="bit MICRF_isTxIdle( void )">
/***********************************************************************************************************************
 *
 * Function Name: MICRF_isTxIdle
 *
 * Purpose: Returns true if the TX is complete or false if not complete
 *
 * Arguments: void
 *
 * Returns: bool - false = Busy, true = Idle
 *
 * Side Effects: N/A
 *
 * Reentrant Code: No
 *
 **********************************************************************************************************************/
bool MICRF_isTxIdle( void )
{
    return(txInfo_.bComplete);
}
/* ****************************************************************************************************************** */
// </editor-fold>

/* ****************************************************************************************************************** */
/* Local Functions */

// <editor-fold defaultstate="collapsed" desc="static void transmitData(void)">
/***********************************************************************************************************************
 *
 * Function Name: transmitData
 *
 * Purpose: transmits the byte(s) of data
 *
 * Arguments: None
 *
 * Returns: None
 *
 * Side Effects: This is blocking code
 *
 * Reentrant Code: No
 *
 **********************************************************************************************************************/
static void transmitData(void)
{
    MICRF_SDA_PIN_LOW();                   // Make sure the TX pin is configured
    MICRF_SCL_DISABLE();                   // Enable the transmitter
    vTaskDelay((80/1000) / portTICK_PERIOD_MS);
  
    MICRF_SDA_PIN_HIGH();   //SET CALIBRATION
    MICRF_SCL_ENABLE();
    vTaskDelay((80/1000) / portTICK_PERIOD_MS);
    MICRF_SCL_DISABLE();
    vTaskDelay((80/1000) / portTICK_PERIOD_MS);
    
    //NO CFG
    MICRF_SDA_PIN_LOW();
    MICRF_SCL_ENABLE();
    vTaskDelay((80/1000) / portTICK_PERIOD_MS);
    MICRF_SCL_DISABLE();
    vTaskDelay((80/1000) / portTICK_PERIOD_MS);
    
    
    
    txInfo_.pData = (uint8_t *)&training_[0];// The 1st set of data to transmit is actually training
    txInfo_.cnt = sizeof(training_) - 1;    // Set the count
    txInfo_.byteToSend = *txInfo_.pData++;  // Set the next byte up to send.  Using local variable for speed!
    txInfo_.bitCnt = 0;                     // init the bit counter
    txInfo_.bStopTx = false;                // don't stop now!
    txInfo_.bComplete = false;              // init the bComplete flag
    txInfo_.bNextBit = false;               // The next bit is assumed false
    if (txInfo_.byteToSend & 0x80)          // is the next bit going to be a 1 (true)
    {   
        txInfo_.bNextBit = true;            // Set the next bit to true
    }
    txInfo_.byteToSend <<= 1;               // Left-shift the data to get the next byte to send in the msb
    txInfo_.eState = eTRAINING;             // Start by sending training info.
    TIMER_INIT();                           // Initialize the timer
    TC0_TimerCallbackRegister(MICRF_isr, (uintptr_t)NULL);              // Set the call-back function pointer
    TIMER_ENABLE();                         // Start the timer which starts transmitting
    
#if 1    
    /* Note:  This could be removed and the application could monitor when the data is complete.  The function 
       MICRF_isTxIdle() can be used to monitor. */
    while(!txInfo_.bComplete)               // While not complete, wait here.
    {}
#endif
}
/* ****************************************************************************************************************** */
// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="static uint16_t manchesterEncode(uint8_t data)">
/***********************************************************************************************************************
 *
 * Function Name: manchesterEncode
 *
 * Purpose: Encode a byte of data
 *
 * Arguments: uint8_t data
 *
 * Returns: uint16_t - encoded data
 *
 * Side Effects: None
 *
 * Reentrant Code: Yes
 *
 **********************************************************************************************************************/
static uint16_t manchesterEncode(uint8_t data)
{
    uint8_t     i;
    uint16_t    encodedBytes;

    for (i = 0, encodedBytes = 0; i < 8; i++, data <<= 1)
    { 
        encodedBytes <<= 2;
        if (data & 0x80)
        {
            encodedBytes |= 1;  // 1: low going to a high
        }
        else
        {
            encodedBytes |= 2;  // 0: high going to a low
        }
    }
    return(encodedBytes);
}
/* ****************************************************************************************************************** */
// </editor-fold>

/* ****************************************************************************************************************** */
/* Event Handlers */

// <editor-fold defaultstate="collapsed" desc="void MICRF_isr(void)">
/***********************************************************************************************************************
 *
 * Function Name: MICRF_isr
 *
 * Purpose: ISR - Called by a timer.
 *
 * Arguments: None
 *
 * Returns: None
 *
 * Side Effects: The transmitter will be disabled once transmission is complete.
 *
 * Reentrant Code: No
 *
 **********************************************************************************************************************/
void MICRF_isr(TC_TIMER_STATUS status, uintptr_t context)
{
    if (!txInfo_.bStopTx)   // The last bit was already sent, then it is time to stop and disable the transmitter.
    {   // More bytes to send!
        /* Must set the next bit before doing anything else.  Can't afford any "gitter" on the TX line.  */
        if (txInfo_.bNextBit)       // Is the next bit high or low (its already been calculated and store in bNextBit)
        {
            MICRF_SDA_PIN_HIGH();    // Set the output pin High
        }
        else
        {
            MICRF_SDA_PIN_LOW();     // Set the output pin Low
        }
        txInfo_.bitCnt++;                   // Increase the bit counter, we need to send the next bit
        if (BITS_IN_BYTE <= txInfo_.bitCnt) // Have all 8 bits been sent?   
        {   // Yes, now get the next byte of data to be sent.
            txInfo_.bitCnt = 0;             // Reset the bit counter
            if (0 != txInfo_.cnt)           // Are there bytes left to send?
            {   // Yes, collect the next byte to send
                txInfo_.byteToSend = *txInfo_.pData++;  // Store the next byte and increment the pointer
                txInfo_.cnt--;                          // Decrement the byte counter
            }
            else    // The data
            {
                switch(txInfo_.eState)  
                {
                    case eTRAINING:
                    {
                        txInfo_.pData = (uint8_t *)&preamble_[0];       // Set of data to transmit the preamble
                        txInfo_.cnt = sizeof(preamble_) - 1;            // Set the count
                        txInfo_.byteToSend = *txInfo_.pData++;          // Set next byte up to send.
                        txInfo_.eState = ePREAMBLE;
                        break;
                    }
                    case ePREAMBLE:
                    {
                        txInfo_.manchesterData = manchesterEncode(*appData_.pData++);
                        txInfo_.cnt = sizeof(txInfo_.manchesterData) - 1;   // Set the count
                        txInfo_.pData = (uint8_t *)&txInfo_.manchesterData; // The 1st set of data to transmit
                        txInfo_.byteToSend = *txInfo_.pData++;              // Set next byte up to send.
                        appData_.cnt--;
                        txInfo_.eState = eDATA;
                        break;
                    }
                    case eDATA:
                    {
                        if (0 != appData_.cnt)
                        {
                            txInfo_.manchesterData = manchesterEncode(*appData_.pData++);
                            txInfo_.cnt = sizeof(txInfo_.manchesterData) - 1;   // Set the count
                            txInfo_.pData = (uint8_t *)&txInfo_.manchesterData; // The 1st set of data to transmit
                            txInfo_.byteToSend = *txInfo_.pData++;              // Set next byte up to send.
                            appData_.cnt--;
                        }
                        else
                        {
                            txInfo_.bStopTx = true;
                        }
                        break;
                    }
                    default:    // Error!
                    {
                        txInfo_.bComplete = true;
                        MICRF_SDA_PIN_LOW();
                        MICRF_SCL_ENABLE();
                        MICRF_SDA_PIN_HIGH();
                        TIMER_DISABLE();
                        break;
                    }
                }
            }
        }
        if (!txInfo_.bStopTx)
        {
            txInfo_.bNextBit = false;
            if (txInfo_.byteToSend & 0x80)
            {
                txInfo_.bNextBit = true;
            }
            txInfo_.byteToSend <<= 1;
        }
    }
    else    // Time to stop transmitting and shut it down!
    {
        txInfo_.bComplete = true;
        MICRF_SDA_PIN_LOW();
        MICRF_SCL_ENABLE();
        MICRF_SDA_PIN_HIGH();
        TIMER_DISABLE();
    }
}
/* ****************************************************************************************************************** */
// </editor-fold>

/* ****************************************************************************************************************** */
/* Unit Test Code */
