/**
* \file  main.c
* m16946
* \brief LORAWAN Demo Application main file
*
*
* Copyright (c) 2018 Microchip Technology Inc. and its subsidiaries.
*
* \asf_license_start
*
* \page License
*
* Subject to your compliance with these terms, you may use Microchip
* software and any derivatives exclusively with Microchip products.
* It is your responsibility to comply with third party license terms applicable
* to your use of third party software (including open source software) that
* may accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
* INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
* AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
* LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
* LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
* SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
* POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
* ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
* RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*
* \asf_license_stop
*
*/
/*
* Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
*/

/****************************** INCLUDES **************************************/
/*** INCLUDE FILES ************************************************************/
#include <asf.h>
#include "system_low_power.h"
#include "radio_driver_hal.h"
#include "lorawan.h"
#include "sys.h"
#include "system_init.h"
#include "system_assert.h"
#include "aes_engine.h"
#include "sio2host.h"
#include "sw_timer.h"
#include "edbg_eui.h"
#include "resources.h"
#include "LED.h"
#ifdef CONF_PMM_ENABLE
  // if Power Mode Module is enabled
  #include "pmm.h"
  #include  "conf_pmm.h"
  #include "sleep_timer.h"
  #include "sleep.h"
#endif
#include "conf_sio2host.h"
#if (ENABLE_PDS == 1)
  // if Persistant Data Server is enabled
  #include "pds_interface.h"
#endif

/*** SYMBOLIC CONSTANTS ********************************************************/
#define DEMO_CONF_DEFAULT_APP_SLEEP_TIME_MS     1000	// Sleep duration in ms
#define APP_TIMEOUT								60000	// App timeout value in ms

/*** GLOBAL VARIABLES & TYPE DEFINITIONS ***************************************/
// LEDs states
static uint8_t on = LON ;
static uint8_t off = LOFF ;

// IO1 Xpro Light sensor
#define IO1_LIGHT_SENSOR_PIN	PIN_PA08
#define IO1_LIGHT_SENSOR_AIN	ADC_POSITIVE_INPUT_PIN16
struct adc_module adc_instance ;

// Application Timer ID
uint8_t AppTimerID = 0xFF ;

// LoRaWAN
bool joined = false ;
LorawanSendReq_t lorawanSendReq ;
#define APP_BUF_SIZE	5
//								IO1-L  IO1-Temperature sensor
uint8_t app_buf[APP_BUF_SIZE] = {0x00, 0x32, 0x35, 0x2E, 0x30} ;

// OTAA join parameters
uint8_t demoDevEui[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} ;	// Read from EDBG chip
uint8_t demoAppEui[8] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x01, 0x63, 0xE5 } ;// Put your own AppEui
uint8_t demoAppKey[16] = { 0x4C, 0xC8, 0x90, 0x59, 0x02, 0x08, 0x69, 0x82, 0xD3, 0x4C, 0xB4, 0x13, 0x96, 0xD4, 0xE0, 0x3E } ; // Put your own AppKey

#ifdef CONF_PMM_ENABLE
bool deviceResetsForWakeup = false ;
#endif

/*** LOCAL FUNCTION PROTOTYPES *************************************************/
static void app_init(void) ;
static void configure_adc(void) ;
static uint16_t read_io1_light_sensor(void) ;
static void print_array (uint8_t *array, uint8_t length) ;
SYSTEM_TaskStatus_t APP_TaskHandler(void) ;
static float convert_celsius_to_fahrenheit(float celsius_val) ;
static void send_uplink(TransmissionType_t type, uint8_t fport, void* data, uint8_t len) ;
void appTimer_callback(void) ;
void appdata_callback(void *appHandle, appCbParams_t *appdata) ;
void joindata_callback(bool status) ;

/*** LOCAL FUNCTION DEFINITIONS ************************************************/

/*** app_init *******************************************************************
 \brief      Function to initialize all the hardware and software modules
********************************************************************************/
static void app_init(void)
{
	system_init() ;
	delay_init() ;
	board_init() ;
	INTERRUPT_GlobalInterruptEnable() ;
	sio2host_init() ;
	// LoRaWAN Stack driver init
	HAL_RadioInit() ;
	AESInit() ;
	SystemTimerInit() ;
#ifdef CONF_PMM_ENABLE
	SleepTimerInit() ;
#endif 
#if (ENABLE_PDS == 1)
	PDS_Init() ;
#endif
	Stack_Init() ;
}

static void configure_adc(void)
{
	/* Configure analog pins */
	struct system_pinmux_config config;
	system_pinmux_get_config_defaults(&config);
	/* Analog functions are all on MUX setting B */
	config.input_pull   = SYSTEM_PINMUX_PIN_PULL_NONE;
	config.mux_position = 1;
	system_pinmux_pin_set_config(IO1_LIGHT_SENSOR_PIN, &config) ;
	
	struct adc_config config_adc ;
	adc_get_config_defaults(&config_adc) ;
	config_adc.clock_source = GCLK_GENERATOR_2 ;	
	config_adc.clock_prescaler = ADC_CLOCK_PRESCALER_DIV2 ;
	config_adc.reference       = ADC_REFERENCE_INTVCC2 ;
	config_adc.negative_input  = ADC_NEGATIVE_INPUT_GND ;
	config_adc.resolution	   = ADC_RESOLUTION_8BIT ;
	config_adc.divide_result   = ADC_DIVIDE_RESULT_16 ;
	config_adc.accumulate_samples = ADC_ACCUMULATE_SAMPLES_16 ;
	config_adc.freerunning = false ;
	adc_init(&adc_instance, ADC, &config_adc) ;
	adc_enable(&adc_instance) ;
}

static uint16_t read_io1_light_sensor(void)
{
	uint16_t result ;
	adc_set_positive_input(&adc_instance, IO1_LIGHT_SENSOR_AIN) ;
	adc_start_conversion(&adc_instance) ;
	do
	{
	} while (adc_read(&adc_instance, &result) == STATUS_BUSY) ;
	return(result) ;
}

/*** dev_eui_read ***************************************************************
 \brief      Reads the DEV EUI if it is flashed in EDBG MCU
********************************************************************************/
static void dev_eui_read(void)
{
#if (EDBG_EUI_READ == 1)
	uint8_t invalidEDBGDevEui[8] ;
	uint8_t EDBGDevEUI[8] ;
	edbg_eui_read_eui64((uint8_t *)&EDBGDevEUI) ;
	memset(&invalidEDBGDevEui, 0xFF, sizeof(invalidEDBGDevEui)) ;
	/* If EDBG does not have DEV EUI, the read value will be of all 0xFF, 
	   Set devEUI in conf_app.h in that case */
	if(0 != memcmp(&EDBGDevEUI, &invalidEDBGDevEui, sizeof(demoDevEui)))
	{
		memcpy(demoDevEui, EDBGDevEUI, sizeof(demoDevEui));
	}
#endif
}

/*** appWakeup ******************************************************************
 \brief      Application Wake Up
 \param[in]  sleptDuration - slept duration in ms
********************************************************************************/
#ifdef CONF_PMM_ENABLE
static void appWakeup(uint32_t sleptDuration)
{
	HAL_Radio_resources_init() ;
	sio2host_init() ;
	printf("\r\nsleep_ok %ld ms\r\n", sleptDuration) ;
}
#endif

/*** app_resources_uninit *******************************************************
 \brief      Uninit. the application resources
********************************************************************************/
#ifdef CONF_PMM_ENABLE
static void app_resources_uninit(void)
{
	// Disable USART TX and RX Pins
	struct port_config pin_conf ;
	port_get_config_defaults(&pin_conf) ;
	pin_conf.powersave  = true ;
#ifdef HOST_SERCOM_PAD0_PIN
	port_pin_set_config(HOST_SERCOM_PAD0_PIN, &pin_conf) ;
#endif
#ifdef HOST_SERCOM_PAD1_PIN
	port_pin_set_config(HOST_SERCOM_PAD1_PIN, &pin_conf) ;
#endif
	// Disable UART module
	sio2host_deinit() ;
	// Disable Transceiver SPI Module
	HAL_RadioDeInit() ;
}
#endif

/*** print_array ****************************************************************
 \brief      Function to Print array of characters
 \param[in]  *array  - Pointer of the array to be printed
 \param[in]   length - Length of the array
********************************************************************************/
static void print_array (uint8_t *array, uint8_t length)
{
    //printf("0x") ;
    for (uint8_t i =0; i < length; i++)
    {
        printf("%02x", *array) ;
        array++ ;
    }
    printf("\n\r") ;
}

/*** APP_TaskHandler ************************************************************
 \brief      Application task handler
********************************************************************************/
SYSTEM_TaskStatus_t APP_TaskHandler(void)
{
	return SYSTEM_TASK_SUCCESS ;
}

/*** convert_celsius_to_fahrenheit **********************************************
 \brief      Function to convert Celsius value to Fahrenheit
 \param[in]  cel_val    - temperature value in Celsius
 \param[out] fahren_val - temperature value in Fahrenheit
********************************************************************************/
static float convert_celsius_to_fahrenheit(float celsius_val)
{
    float fahren_val ;
    /* T(°F) = T(°C) × 9/5 + 32 */
    fahren_val = (((celsius_val * 9)/5) + 32) ;
    return fahren_val ;
}

/*** send_uplink ****************************************************************
 \brief      Function to transmit an uplink message
 \param[in]  type	- transmission type (LORAWAN_UNCNF or LORAWAN_CNF)
 \param[in]  fport	- 1-255
 \param[in]  data	- buffer to transmit
 \param[in]  len	- buffer length
 send_uplink(LORAWAN_UNCNF, 2, &temp_sen_str, strlen(temp_sen_str) - 1) ;
********************************************************************************/
static void send_uplink(TransmissionType_t type, uint8_t fport, void* data, uint8_t len)
{
	StackRetStatus_t status ;
	
	lorawanSendReq.buffer = data ;
	lorawanSendReq.bufferLength = len ;
	lorawanSendReq.confirmed = type ;	// LORAWAN_UNCNF or LORAWAN_CNF
	lorawanSendReq.port = fport ;		// fport [1-255]
	status = LORAWAN_Send(&lorawanSendReq) ;
	if (LORAWAN_SUCCESS == status)
	{
		printf("\nUplink message sent\r\n") ;
		set_LED_data(LED_GREEN, &on) ;
		set_LED_data(LED_AMBER, &off) ;
	}
	else
	{
		set_LED_data(LED_GREEN, &off) ;
		set_LED_data(LED_AMBER, &on) ;
	}
}

/*** joindata_callback **********************************************************
 \brief      Callback function for the ending of Activation procedure
 \param[in]  status - join status
********************************************************************************/
void joindata_callback(bool status)
{
	StackRetStatus_t stackRetStatus = LORAWAN_INVALID_REQUEST ;
	
	// This is called every time the join process is finished
	if (true == status)
	{
		joined = true ;
		set_LED_data(LED_GREEN, &off) ;
		set_LED_data(LED_AMBER, &off) ;
		printf("\nJoin Successful!\r\n") ;
		// Start the SW Timer to send data periodically
		stackRetStatus = SwTimerStart(AppTimerID, MS_TO_US(APP_TIMEOUT), SW_TIMEOUT_RELATIVE, (void*)appTimer_callback, NULL) ;	
		if (LORAWAN_SUCCESS != stackRetStatus)
		{
			printf("Unable to start the application timer\r\n") ;
		}
	}
	else
	{
		joined = false ;
		set_LED_data(LED_GREEN, &off) ;
		set_LED_data(LED_AMBER, &on) ;
		printf("\nJoin Denied!\r\n") ;
		printf("\nTry to join again ...\r\n") ;
		stackRetStatus = LORAWAN_Join(LORAWAN_OTAA) ;
		if (LORAWAN_SUCCESS == stackRetStatus)
		{
			set_LED_data(LED_GREEN, &on) ;
			printf("\nJoin Request sent to the network server\r\n") ;
		}
	}
}

/*** appdata_callback ***********************************************************
 \brief      Callback function for the ending of Bidirectional communication of
			 Application data
 \param[in]  *appHandle - callback handle
 \param[in]  *appData - callback parameters
********************************************************************************/
void appdata_callback(void *appHandle, appCbParams_t *appdata)
{
	StackRetStatus_t status = LORAWAN_INVALID_REQUEST ;

	if (LORAWAN_EVT_RX_DATA_AVAILABLE == appdata->evt)
	{
		// LORAWAN RX Data Available Event
		status = appdata->param.rxData.status ;
		if (LORAWAN_SUCCESS == status)
		{
			uint8_t *pData = appdata->param.rxData.pData ;
			uint8_t dataLength = appdata->param.rxData.dataLength ;
			if((dataLength > 0U) && (NULL != pData))
			{
				printf("*** Received DL Data ***\n\r") ;
				printf("\nFrame Received at port %d\n\r", pData[0]) ;
				printf("\nFrame Length - %d\n\r", dataLength) ;
				printf ("\nPayload: ") ;
				for (uint8_t i = 0; i < dataLength - 1; i++)
				{
					printf("%0x", pData[i+1]) ;
				}
				printf("\r\n*************************\r\n") ;
			}
			else
			{
				printf("Received ACK for Confirmed data\r\n") ;
			}
		}
	}
	else if(LORAWAN_EVT_TRANSACTION_COMPLETE == appdata->evt)
	{
		// LORAWAN Transaction Complete Event
		status = appdata->param.transCmpl.status ;
		printf("Transaction Complete - Status: %d\r\n", status) ;
		switch (status)
		{
			case LORAWAN_SUCCESS:
			{
				printf("Transmission Success\r\n") ;
				set_LED_data(LED_GREEN, &off) ;
				set_LED_data(LED_AMBER, &off) ;
			}
			break ;
			case LORAWAN_NO_CHANNELS_FOUND:
			{
				printf("\n\rNO_CHANNELS_FOUND\n\r") ;
				set_LED_data(LED_AMBER, &on) ;
			}
			break ;
			case LORAWAN_BUSY:
			{
				printf("\n\rBUSY\n\r") ;
				set_LED_data(LED_AMBER, &on) ;
			}
			break ;
			default:
			printf("\n\rIssue\r\n") ;
			set_LED_data(LED_AMBER, &on) ;
			break ;
		}
	}
}

/*** appTimer_callback **********************************************************
 \brief      Callback function for the Application Timer called every APP_TIMEOUT
********************************************************************************/
void appTimer_callback(void)
{
	StackRetStatus_t status ;

	printf("App timer expired \r\n") ;
	// Read IO1 Xpro Light sensor value
	uint16_t light_val = read_io1_light_sensor() ;
	printf("\nIO1 Xpro Light sensor value: %d", light_val) ;

	// Read IO1 Xpro Temperature sensor value
	float c_val = at30tse_read_temperature() ;
	float f_val = convert_celsius_to_fahrenheit(c_val) ;
	printf("\nIO1 Xpro Temperature: ") ;
	printf("%.1f\xf8 C/%.1f\xf8 F\n\r", c_val, f_val) ;
	
	// Prepare the payload to be sent
	// IO1 Light sensor
	app_buf[0] = light_val ;
	// IO1 Temperature sensor
	char TxTempBuffer[4] ;
	sprintf(TxTempBuffer, "%.1f", c_val) ;
	uint8_t j = 1 ;
	for (uint8_t i = 0; i <= 3; i++)
	{
		app_buf[j++] = TxTempBuffer[i] ;
	}
	
	printf("\nLen: %d", sizeof(app_buf)) ;
	printf("\nPayload: ") ;
	print_array(app_buf, sizeof(app_buf)) ;

	if (false == joined)
	{
		// Not join - Send Join request
		status = LORAWAN_Join(LORAWAN_OTAA) ;
		if (LORAWAN_SUCCESS == status)
		{
			printf("Join Request Sent to the Network Server\r\n") ;
		}
	}
	else
	{
		send_uplink(LORAWAN_UNCNF, 2, app_buf, sizeof(app_buf)) ;
	}
	
	SwTimerStart(AppTimerID, MS_TO_US(APP_TIMEOUT), SW_TIMEOUT_RELATIVE, (void*)appTimer_callback, NULL) ;
}

/*** main ***********************************************************************
 \brief      Main function
********************************************************************************/
int main(void)
{
	StackRetStatus_t status = LORAWAN_INVALID_REQUEST ;
	// --------------------------------------------------------------------------
	// Init. section
	// --------------------------------------------------------------------------
	app_init() ;
	// IO1 Xpro Light sensor
	configure_adc() ;
	// IO1 Xpro Temperature sensor
	at30tse_init() ;	
	set_LED_data(LED_GREEN, &off) ;
	set_LED_data(LED_AMBER, &off) ;
	printf("\r\n-- ATSAMR34 LoRaWAN Application --\r\n") ;
	
	// --------------------------------------------------------------------------
	// OTA Activation keys section
	// --------------------------------------------------------------------------
	// Device EUI is the unique identifier for this device on the network
	// Read DevEUI from EDBG
	dev_eui_read() ;
	printf("\nDevEUI : ") ;
	print_array(demoDevEui, sizeof(demoDevEui)) ;
	// App EUI identifies the application server
	//  AppEUI is defined in the global variable demoAppEUI
	printf("\nAppEUI : ") ;
	print_array(demoAppEui, sizeof(demoAppEui)) ;
	// AppKey is defined in the global variable demoAppKey
	printf("\nAppKey : ") ;
	print_array(demoAppKey, sizeof(demoAppKey)) ;	
	
	LORAWAN_Init(appdata_callback, NULL) ;	
	LORAWAN_Reset(ISM_EU868) ;
	EdClass_t classType = CLASS_A ;
	LORAWAN_SetAttr(EDCLASS, &classType) ;
	uint8_t datarate = DR5 ;
	LORAWAN_SetAttr(CURRENT_DATARATE, &datarate) ;
	LORAWAN_SetAttr(DEV_EUI, demoDevEui) ;
	LORAWAN_SetAttr(APP_EUI, demoAppEui) ;
	LORAWAN_SetAttr(APP_KEY, demoAppKey) ;
	bool adr = false ;
	LORAWAN_SetAttr(ADR, &adr) ;

	// Create the SW timer (swtimerCreate function itself will assign the different Id for each timer)
	SwTimerCreate(&AppTimerID) ;
	
	status = LORAWAN_Join(LORAWAN_OTAA) ;
	if (LORAWAN_SUCCESS == status)
	{
		set_LED_data(LED_GREEN, &on) ;
		printf("\nJoin Request sent to the network server\r\n") ;
	}

	while(1)
	{	
		// Run the scheduler tasks
		SYSTEM_RunTasks() ;
#ifdef CONF_PMM_ENABLE
		PMM_SleepReq_t sleepReq ;
		// Put the application to sleep
		sleepReq.sleepTimeMs = DEMO_CONF_DEFAULT_APP_SLEEP_TIME_MS ;
		sleepReq.pmmWakeupCallback = appWakeup ;
		sleepReq.sleep_mode = CONF_PMM_SLEEPMODE_WHEN_IDLE ;
		if (CONF_PMM_SLEEPMODE_WHEN_IDLE == SLEEP_MODE_STANDBY)
		{
			deviceResetsForWakeup = false;
		}
		if (true == LORAWAN_ReadyToSleep(deviceResetsForWakeup))
		{
			app_resources_uninit() ;
			if (PMM_SLEEP_REQ_DENIED == PMM_Sleep(&sleepReq))
			{
				HAL_Radio_resources_init();
				sio2host_init();
				//printf("\r\nsleep_not_ok\r\n");
			}
		}
#endif
	}
	return(0) ;
}
