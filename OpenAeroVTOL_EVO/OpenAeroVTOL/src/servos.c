//***********************************************************
//* Servos.c
//***********************************************************

//***********************************************************
//* Includes
//***********************************************************

#include "compiledefs.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include "typedefs.h"
#include "io_cfg.h"
#include "main.h"
#include "isr.h"
#include "rc.h"

//************************************************************
// Prototypes
//************************************************************

void output_servo_ppm(uint8_t ServoFlag);
void output_servo_ppm_asm(volatile uint16_t *ServoOut, uint8_t ServoFlag);

//************************************************************
// Code
//************************************************************

volatile uint16_t ServoOut[MAX_OUTPUTS];

void output_servo_ppm(uint8_t ServoFlag)
{
	uint32_t temp;
	uint8_t i = 0;

	// Re-span numbers from internal values to microseconds
	for (i = 0; i < MAX_OUTPUTS; i++)
	{
		temp = ServoOut[i];					// Promote to 32 bits

		// Check for motor marker and ignore if set
		if (Config.Channel[i].Motor_marker != MOTOR)
		{
			// Scale servo from 2500~5000 to 875~2125
			temp = ((temp - 3750) >> 1) + 1500;
		}
		else
		{
			// Scale motor from 2500~5000 to 1000~2000
			temp = ((temp << 2) + 5) / 10; 	// Round and convert	
		}
		
		ServoOut[i] = (uint16_t)temp;
	}

	// Re-sample throttle value
	MonopolarThrottle = RxChannel[THROTTLE] - Config.RxChannelZeroOffset[THROTTLE];

	// Check for motor flags if throttle is below arming minimum or disarmed
	// and set all motors to minimum throttle if so
	if 	(
			(MonopolarThrottle < THROTTLEIDLE) || 
			((General_error & (1 << DISARMED)) != 0)
		)
	{
		// For each output
		for (i = 0; i < MAX_OUTPUTS; i++)
		{
			// Check for motor marker
			if (Config.Channel[i].Motor_marker == MOTOR)
			{
				// Set output to minimum pulse width (1000us)
				ServoOut[i] = MOTORMIN;
			}
		}
	}

	// Determine output rate based on device type
	// Suppress outputs during throttle high error
	if((General_error & (1 << THROTTLE_HIGH)) == 0)
	{
		// Reset JitterFlag immediately before PWM generation
		JitterFlag = false;
	
		// We now care about interrupts
		JitterGate = true;

		// Pass address of ServoOut array
		output_servo_ppm_asm(&ServoOut[0], ServoFlag);
		
		// We no longer care about interrupts
		JitterGate = false;
	}
}
