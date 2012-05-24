//***********************************************************
//* init.c
//***********************************************************

//***********************************************************
//* Includes
//***********************************************************

#include <avr/io.h>
#include <avr/eeprom.h>
#include <stdbool.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "..\inc\eeprom.h"
#include "..\inc\io_cfg.h"
#include "..\inc\isr.h"
#include "..\inc\rc.h"
#include "..\inc\main.h"
#include "..\inc\adc.h"
#include "..\inc\pots.h"
#include "..\inc\servos.h"
#include "..\inc\gyros.h"
#include "..\inc\acc.h"
#include "..\inc\uart.h"

//************************************************************
// Prototypes
//************************************************************

void init(void);
void CenterSticks(void);
void SetFailsafe(void);

//************************************************************
// Defines
//************************************************************

#define UC_ADC_MAX 1023				// Used to invert ADC reading. Do not change.

//************************************************************
// Code
//************************************************************

CONFIG_STRUCT Config;			// eeProm data configuration

void init(void)
{
	// Setup I/O pins
	RX_ROLL_DIR		= INPUT;
	RX_PITCH_DIR	= INPUT;
#if (defined(CPPM_MODE) || defined(ICP_CPPM_MODE))
	THR_DIR			= OUTPUT;	// In CPPM mode the THR pin is an output
#else
	RX_COLL_DIR		= INPUT;	// Otherwise the THR pin is an input
#endif
	RX_YAW_DIR		= INPUT;
	FS_DIR			= INPUT;	// Failsafe set trigger

	GYRO_YAW_DIR	= INPUT;
	GYRO_PITCH_DIR	= INPUT;
	GYRO_ROLL_DIR	= INPUT;
	GAIN_YAW_DIR	= INPUT;
	GAIN_PITCH_DIR	= INPUT;
	GAIN_ROLL_DIR	= INPUT;

	M1_DIR			= OUTPUT;
	M2_DIR			= OUTPUT;
	M4_DIR			= OUTPUT;
	M5_DIR			= OUTPUT;

	LED_DIR			= OUTPUT;
	LCD_TX_DIR		= OUTPUT;
	LVA_DIR			= OUTPUT;
	ICP_DIR			= INPUT;	// Always an input for safety, even in PWM mode

	M1			= 0;			// Hold all pwm outputs low to stop glitches
	M2			= 0;
	M4			= 0;
	M5			= 0;

	LVA 		= 0; 			// LVA alarm OFF
	LED 		= 0;			// LED off

	// Set/clear pull-ups (1 = set, 0 = clear)
	RX_ROLL 	= 1;
	RX_PITCH 	= 1;
#if (defined (LEGACY_PWM_MODE) || defined (AUTOMAGIC_PWM_MODE) || defined (DOSD_PWM_MODE)) // Only enable pull-up when THR is an input
	RX_COLL 	= 1;
#endif
	RX_YAW		= 1;
	FS			= 1;

	// Timer0 (8bit) - run @ 8MHz (125ns)
	// Used to pad out loop cycle time in blocks of 1us
	TCCR0A = 0;							// Normal operation
	TCCR0B = (1 << CS00);				// Clk/0 = 8MHz = 125ns
	TIMSK0 = 0; 						// No interrupts

	// Timer1 (16bit) - run @ 1Mhz
	// Used to measure Rx Signals & control ESC/servo output rate
	TCCR1A = 0;
	TCCR1B = (1 << CS11);				// Clk/8 = 1MHz

	// Timer2 8bit - run @ 8MHz / 1024 = 7812.5kHz / 128us
	// Used to time arm/disarm intervals
	TCCR2A = 0;	
	TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);	// Clk/1024 = 7812.5kHz
	TIMSK2 = 0;
	TIFR2 = 0;
	TCNT2 = 0;							// Reset counter

// Conditional builds for interrupt and pin function setup
#ifdef ICP_CPPM_MODE 
	// Input capture interrupt
	TIMSK1  = (1 << ICIE1);					// Input capture interrupt enable
	TIFR1   = 0;							// Reset input capture interrupt flag
	TCCR1B |= (1 << ICNC1);					// Set input capture noise canceller
	//TCCR1B |= (1 << ICES1);				// Set input capture edge selection to rising (otherwise falling)
#else
	#ifdef CPPM_MODE 
		// External interrupts INT0 (CPPM input)
		EICRA = (1 << ISC01);				// Falling edge of INT0
		EIMSK = (1 << INT0);				// Enable INT0
		EIFR |= (1 << INTF0);				// Clear INT0 interrupt flags

	#else // PWM modes
		// Pin change interrupt enables PCINT0 and PCINT2 (Yaw, Roll)
		PCICR |= (1 << PCIE0);				// PCINT0  to PCINT7  (PCINT0 group)		
		PCICR |= (1 << PCIE2);				// PCINT16 to PCINT23 (PCINT2 group)
		PCMSK0 |= (1 << PCINT7);			// PB7 (Rudder/Yaw pin change mask)
		PCMSK2 |= (1 << PCINT17);			// PD1 (Aileron/Roll pin change mask)
		// External interrupts INT0 and INT1 (Pitch, Collective)
		EIMSK = (1 << INT0) | (1 << INT1);	// External Interrupt Mask Register - enable INT0 and INT1
		EIFR |= (1 << INTF0) | (1 << INTF1);// Clear both INT0 and INT1 interrupt flags

		#ifdef DOSD_PWM_MODE 
			EICRA = (1 << ISC01) | (1 << ISC11);// Falling edges of INT0, INT1 
		#else
			EICRA = (1 << ISC00) | (1 << ISC10);// Any change INT0, INT1 
		#endif

	#endif
#endif

	RC_Lock = false;					// Preset important flags
	Failsafe = false;
	GyroCalibrated = false;
	AccCalibrated = false;
	AutoLevel = false;

	Initial_EEPROM_Config_Load();		// Loads config at start-up 
	Init_ADC();
	init_uart();						// Initialise UART

	IntegralgPitch = 0;	 				// Reset I-terms
	IntegralgRoll = 0;
	IntegralaPitch = 0;	 
	IntegralaRoll = 0;
	IntegralYaw = 0;

	RxChannel1 = Config.RxChannel1ZeroOffset; // Preset servo outputs
	RxChannel2 = Config.RxChannel2ZeroOffset;
	RxChannel3 = Config.RxChannel3ZeroOffset;
	RxChannel4 = Config.RxChannel4ZeroOffset;
	RxChannel5 = Config.RxChannel5ZeroOffset;

	// Flash LED
	LED = 1;
	_delay_ms(150);
	LED = 0;

	sei();						// Enable global Interrupts 

	// Pause
	_delay_ms(1500);			// Pause for gyro stability
	CalibrateGyros();			// Calibrate gyros, hopefully after motion minimised
	ReadGainValues();			// Check pots

//************************************************************
// Config Modes (at startup)
//************************************************************

	// Stick Centering
	if (GainInADC[YAW] > 240)	// More than 95%
	{
		CenterSticks();
		while(1); 				// Loop forever
	}

	// Autotune
	else if (GainInADC[YAW] < 15)	// Less than 5%
	{
		autotune();
		init_uart();
		Save_Config_to_EEPROM();// Save to eeProm	
		LED = !LED;
		_delay_ms(500);
		LED = !LED;
		while(1); 				// Loop forever
	}

// If ACC fitted, allow the switchability of the stability mode with the THR input
#ifdef ACCELEROMETER
	RxGetChannels();
	// Manual stability mode selection
	if (RxInYaw > 200)
	{
		// flash LED 4 times
		for (int i=0;i<4;i++)
		{
			LED = 1;
			_delay_ms(150);
			LED = 0;
			_delay_ms(150);
		}

		if (Config.StabMode == 1) Config.StabMode = 0;	// Stability switchable
		else Config.StabMode = 1; 						// Stability always ON

		Save_Config_to_EEPROM();

		LED = !LED;
		_delay_ms(500);
		LED = !LED;
	}

	// Manual autolevel mode selection
	if (RxInYaw < -200)
	{
		// flash LED 5 times
		for (int i=0;i<5;i++)
		{
			LED = 1;
			_delay_ms(150);
			LED = 0;
			_delay_ms(150);
		}

		if (Config.ALMode == 1) Config.ALMode = 0;		// Autolevel switchable
		else Config.ALMode = 1; 						// Autolevel always OFF

		Save_Config_to_EEPROM();

		LED = !LED;
		_delay_ms(500);
		LED = !LED;
	}
#endif

#ifndef ACCELEROMETER			// Allow gyro reversing via pot without accelerometer
	// Gyro direction reversing
	if (GainInADC[ROLL] < 15)	// less than 5% 
	{
		// flash LED 6 times
		for (int i=0;i<6;i++)
		{
			LED = 1;
			_delay_ms(150);
			LED = 0;
			_delay_ms(150);
		}

		while(1)
		{
			RxGetChannels();

			if (RxInRoll < -200) {			// Normal(left)
				Config.RollGyro = GYRO_NORMAL;
				Save_Config_to_EEPROM();
				LED = 1;
			} if (RxInRoll > 200) {			// Reverse(right)
				Config.RollGyro = GYRO_REVERSED;
				Save_Config_to_EEPROM();
				LED = 1;
			} else if (RxInPitch < -200) {	// Normal(up)
				Config.PitchGyro = GYRO_NORMAL;
				Save_Config_to_EEPROM();
				LED = 1;
			} else if (RxInPitch > 200) {	// Reverse(down)
				Config.PitchGyro = GYRO_REVERSED;
				Save_Config_to_EEPROM();
				LED = 1;
			} else if (RxInYaw < -200) {	// Normal(left)
				Config.YawGyro = GYRO_NORMAL;
				Save_Config_to_EEPROM();
				LED = 1;
			} else if (RxInYaw > 200) {		// Reverse(right)
				Config.YawGyro = GYRO_REVERSED;
				Save_Config_to_EEPROM();
				LED = 1;
			}

			_delay_ms(50);
			LED = 0;
		}
	} //if (GainInADC[ROLL] < 15)
#endif

} // init()

//************************************************************
// Misc init subroutines
//************************************************************

// Center sticks on request from GUI or by pot selection
void CenterSticks(void)		
{
	uint8_t i;

	// Flash LED 3 times to warn of impending stick measurement
	for (i=0;i<3;i++)
	{
		LED = !LED;
		_delay_ms(150);
		LED = !LED;
		_delay_ms(150);
	}

	uint16_t RxChannel1ZeroOffset = 0;
	uint16_t RxChannel2ZeroOffset = 0;
	uint16_t RxChannel4ZeroOffset = 0;
	uint16_t RxChannel5ZeroOffset = 0;

	for (i=0;i<8;i++)
	{
		RxChannel1ZeroOffset += RxChannel1;
		RxChannel2ZeroOffset += RxChannel2;
		RxChannel4ZeroOffset += RxChannel4;
		RxChannel5ZeroOffset += RxChannel5;
		_delay_ms(100);
	}

	Config.RxChannel1ZeroOffset = RxChannel1ZeroOffset >> 3; // Divide by 8
	Config.RxChannel2ZeroOffset = RxChannel2ZeroOffset >> 3;
	Config.RxChannel3ZeroOffset = 1500;// Cheat for CH3 - we just need a guaranteed switch here						 
	Config.RxChannel4ZeroOffset = RxChannel4ZeroOffset >> 3;
	Config.RxChannel5ZeroOffset = RxChannel5ZeroOffset >> 3;

	Save_Config_to_EEPROM();

	LED = !LED;
	_delay_ms(500);
	LED = !LED;
}

// Set failsafe position
void SetFailsafe(void)		
{
	uint8_t i;
	uint16_t Failsafe_1 = 0;
	uint16_t Failsafe_2 = 0;
	uint16_t Failsafe_3 = 0;
	uint16_t Failsafe_4 = 0;
	uint16_t Failsafe_5 = 0;
	uint16_t Failsafe_6 = 0;

	// Flash LED 6 times to warn of impending stick measurement
	for (i=0;i<6;i++)
	{
		LED = !LED;
		_delay_ms(150);
		LED = !LED;
		_delay_ms(150);
	}

	for (i=0;i<8;i++)
	{
		SetServoPositions();
		Failsafe_1 += ServoOut1; // Aileron & Left flaperon
		Failsafe_2 += ServoOut2; // Elevator
		Failsafe_3 += Throttle;  // Throttle
		Failsafe_4 += ServoOut4; // Rudder
		Failsafe_5 += ServoOut5; // Right flaperon
		Failsafe_6 += ServoOut6; // Spare
		_delay_ms(100);
	}

	Config.Failsafe_1 = Failsafe_1 >> 3; // Divide by 8
	Config.Failsafe_2 = Failsafe_2 >> 3;
	Config.Failsafe_3 = Failsafe_3 >> 3;
	Config.Failsafe_4 = Failsafe_4 >> 3;
	Config.Failsafe_5 = Failsafe_5 >> 3;
	Config.Failsafe_6 = Failsafe_6 >> 3;

	Save_Config_to_EEPROM();

	LED = !LED;
	_delay_ms(500);
	LED = !LED;
}
