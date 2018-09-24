/*
 * This file is part of the Tiberius firmware distribution.
 * Copyright (c) 2018 Arcady N. Shpak (aka Greignar)
 * 
 * https://github.com/Greignar/tiberius.git
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

#include "./driver.h"
#include "./controller.h"

#include <avr/pgmspace.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/delay_basic.h>
#include <avr/power.h>
#include <string.h>


#define _noinit_ __attribute__ ((section (".noinit")))

// Commands
#define INIT					0
#define SETUP_BRIGHT_MODE		1

// Clicks normal mode
#define CLICK_NEXT_MODE			1
#define CLICK_PREV_MODE			2
#define CLICK_MAX_MODE			3
#define CLICK_MIN_MODE			4
#define CLICK_SOS_MODE			5
#define CLICK_BATTERY_MODE		6
#define CLICK_PROGRAM_MODE		9

// Clicks program mode
#define CLICK_SETUP_MODE		3
#define CLICK_START_MODE		4
#define CLICK_RESET_MODE		9

// Voltage, is determined by the formula: Y = 50.6 * (X - 0,4)
#define ADC_LOW					131	// 3.0V
#define ADC_CRIT				121	// 2.8V
#define ADC_OFF					111	// 2.6V

// SOS pulse rate
#define EMERGENCY_SPEED			(200/10)

// Levels of brightness
#define BLINK_BRIGHTNESS		5

// Number of brightness modes
#define MODES 5

// Levels of brightness
#define BRIGHTNESS_FETCH_SIZE  9	// Count BRIGHTNESS_FETCH - 1
#define BRIGHTNESS_FETCH 0, 1, 2, 4, 8, 16, 32, 64, 128, 255

// Minimum and maximum brightness modes
#define BRIGHTNESS_MIN  		1
#define BRIGHTNESS_MAX			BRIGHTNESS_FETCH_SIZE

// Timers
#define LOW_POWER_TIMER			5	// 5 Sec
#define MAX_BRIGHT_TIMER		300	// 5 Min

typedef struct {
	uint8_t brightMode;				// Brightness
	uint8_t rawGroup[MODES];		// Raw group
} eeprom_t;

typedef struct {
	uint8_t brightMode;				// Brightness
	uint8_t commandMode;			// Command
	uint8_t commandVar;				// Command Variable
	uint8_t shortClick;				// Short Click
	uint8_t longClick;				// Long Click
	uint8_t action;					// Action
	uint8_t program;				// Program
	uint8_t countModes;				// Mode Counter
	uint8_t group[MODES];			// Current group
	uint8_t rawGroup[MODES];		// Raw group
} state_t;

eeprom_t	eeprom	_noinit_;		// EEPROM Vars
state_t		state	_noinit_;		// State Vars

PROGMEM const uint8_t brightnessFetch[]  = { BRIGHTNESS_FETCH };

// Delay of 10mS
void delay10ms(uint8_t n) {
    while ( n-- > 0 ) _delay_loop_2( BOGOMIPS * 10 );
}

// Delay of 1S
void delay1s() {
    delay10ms(100);
}

// Delay of 1M
void delay1m() {
	for ( uint8_t i = 0; i < 60; i++ ) { delay10ms(100); }
}

// Saving the current mode to the controller memory (with wear leveling)
void saveCurrentBright() {
	#define EEPROM_BRIGHT (EEPSIZE - 1)
	if ( eeprom.brightMode != state.brightMode ) {
		eeprom_write_byte((uint8_t *)(EEPROM_BRIGHT), state.brightMode);
		eeprom.brightMode = state.brightMode;
	}
}

// Saving the current state to the controller memory
void saveCurrentState() {
	#define EEPROM_MODES ( EEPSIZE - 2 )
	uint8_t *src = eeprom.rawGroup;
	uint8_t *cpy = state.rawGroup;
	for ( uint8_t i = 0; i < MODES; i++ ) {
		if ( *src != *cpy ) {
			eeprom_write_byte((uint8_t *)(EEPROM_MODES - i), *cpy);
			*src = *cpy;
		}
		src++; cpy++;
	}
}

// Reset state
void resetState() {
	state.brightMode = 2;
	state.commandMode = INIT;
	uint8_t *dest = state.rawGroup;
	for ( uint8_t i = 1; i <= MODES; i++ ) { *dest++ = i * ( BRIGHTNESS_FETCH_SIZE + 1 ) / MODES - 1; }
	saveCurrentState();
	saveCurrentBright();
}

// Loading the current state from the controller memory
inline void loadCurrentState() {
	eeprom.brightMode = eeprom_read_byte((const uint8_t *)EEPROM_BRIGHT);
	if ( eeprom.brightMode >= MODES ) { resetState(); }
	state.countModes = INIT;
	uint8_t lastMode = 0;
	uint8_t *dst = state.group;
	uint8_t *src = eeprom.rawGroup;
	uint8_t *cpy = state.rawGroup;
	for(uint8_t i = 0; i < MODES; i++) {
		*src = eeprom_read_byte((uint8_t *) EEPROM_MODES-i);
		if ( *src > 0 && lastMode < BRIGHTNESS_FETCH_SIZE ) { lastMode = *dst++ = *src; state.countModes++; }
		*cpy++ = *src++;
	}
}

// Getting the next brightness mode
inline void getNextMode() {
	state.brightMode += 1;
	if (state.brightMode >= state.countModes) {
		state.brightMode = eeprom.brightMode;
	}
}

// Getting the previous brightness mode
inline void getPrevMode() {
	if (state.brightMode > 0) {
		state.brightMode -= 1;
	}
}

// Setting the brightness of the LED
void setLedPower(uint8_t level) {
	if ( level > BRIGHTNESS_FETCH_SIZE ) { level = BRIGHTNESS_FETCH_SIZE; }
	if ( level ) { level = pgm_read_byte( brightnessFetch + level ); }
	TCCR0A = PHASE;
	TCCR0B = 0x02;
	PWM_LVL = level;
}

// Output of the pulse of arbitrary amplitude and duty cycle
void doImpulses(uint8_t count, uint8_t brightOn, uint8_t timeOn, uint8_t brightOff, uint8_t timeOff) {
	while (count--) {
		setLedPower(brightOn);
		delay10ms(timeOn);
		setLedPower(brightOff);
		delay10ms(timeOff);
	}
}

// SOS mode
inline void getSosMode(uint8_t *power) {
	doImpulses( 3, *power, EMERGENCY_SPEED, 0, EMERGENCY_SPEED );
	delay10ms(EMERGENCY_SPEED*2);
	doImpulses( 3, *power, EMERGENCY_SPEED*3, 0, EMERGENCY_SPEED );
	delay10ms(EMERGENCY_SPEED*2);
	doImpulses( 3, *power, EMERGENCY_SPEED, 0, EMERGENCY_SPEED );
	delay1m();
}

// Outputting the battery level (the more, the better)
inline void getBatteryMode() {
	uint8_t voltage = ADCH;
	delay1s();
	if ( voltage > ADC_LOW ) { doImpulses( ( ADCH - ADC_LOW ) >> 3, BLINK_BRIGHTNESS, 500/10, 0, 500/10 ); }
	delay1s();
}

// Checking the battery state
inline void checkPowerState(uint8_t *power, uint8_t *count) {
	if ( ADCSRA & ( 1 << ADIF ) ) {
		uint8_t voltage = ADCH;
		if ( voltage < ADC_LOW && *power > BRIGHTNESS_FETCH_SIZE - 2 ) {
			*power = BRIGHTNESS_FETCH_SIZE - 2;
		}
		if ( voltage < ADC_CRIT ) {
			*count += 1;
		} else {
			*count = 0;
		}
		if ( *count >= LOW_POWER_TIMER ) {
			if ( *power > 2 ) {
				*power = *power - 1;
			} else if ( voltage < ADC_OFF ) {
				setLedPower(0);
				set_sleep_mode(SLEEP_MODE_PWR_DOWN);
				sleep_mode();
			}
			*count = 0;
		}
		ADCSRA |= (1 << ADSC);
	}
}

// Limitation of operating time at maximum power
inline void checkBrightState(uint8_t *power, uint8_t *count) {
	if ( *power == BRIGHTNESS_FETCH_SIZE ) {
		*count += 1;
	} else {
		*count = 0;
	}
	if ( *count >= MAX_BRIGHT_TIMER ) {
		*power = *power - 1;
		*count = 0;
	}
}

// Indication of brightness
void indicateBrightMode (uint8_t mode) {
	doImpulses(1, mode+1, 100/10, mode, 1900/10);
}

// Selecting a mode (setup)
inline void setupMode() {
	delay1s();
	uint8_t lastMode = 0;
	state.commandMode = SETUP_BRIGHT_MODE;
	for ( uint8_t i = 0; i < MODES && lastMode < BRIGHTNESS_FETCH_SIZE; i++ ) {
		state.commandVar = i;
		lastMode = state.rawGroup[i];
		indicateBrightMode( state.rawGroup[i] );
	}
	state.commandMode = INIT;
}

// Selecting the brightness level (setup)
inline void setupBrightMode() {
	delay1s();
	uint8_t oldMode = state.rawGroup[state.commandVar];
	state.commandMode = state.brightMode = INIT;
	saveCurrentBright();
	uint8_t i = 0;
	if ( state.commandVar > 0 ) { i = 1 + state.rawGroup[ state.commandVar - 1 ]; }
	for ( ; i <= BRIGHTNESS_FETCH_SIZE; i++ ) {
		state.rawGroup[state.commandVar] = i;
		saveCurrentState();
		indicateBrightMode(i);
	}
	state.rawGroup[state.commandVar] = oldMode;
	saveCurrentState();
}

int main(void)
{
	uint8_t ledPower = 0;
	uint8_t ledChangePower = 0;
	uint8_t powerCounter = 0;
	uint8_t brightCounter = 0;

	DDRB |= ( 1 << PWM_PIN );
	DIDR0 |= ( 1 << ADC_DIDR );
	ADMUX  = ( 1 << V_REF ) | (1 << ADLAR) | ADC_CHANNEL;
	ADCSRA = ( 1 << ADEN ) | (1 << ADSC ) | ADC_PRSCL;

	loadCurrentState();

	if ( state.longClick ) { state.action = state.commandMode = INIT; state.brightMode = eeprom.brightMode; }
	if (! state.commandMode ) {
		if (! state.longClick ) {
			state.shortClick = ( state.shortClick + 1 ) & 0x0f;
			delay10ms(250/10);
			if ( state.action == CLICK_MAX_MODE || state.action == CLICK_MIN_MODE || state.action == CLICK_SOS_MODE ) { state.shortClick = INIT; }
			state.action = state.shortClick;
			state.shortClick = INIT;
			if ( state.action == CLICK_NEXT_MODE ) { getNextMode(); }
			else if ( state.action == CLICK_PREV_MODE ) { getPrevMode(); }
			if ( state.program ) {
				if ( state.action == CLICK_MAX_MODE ) { ledChangePower = BRIGHTNESS_MAX; }
				else if ( state.action == CLICK_MIN_MODE ) { ledChangePower = BRIGHTNESS_MIN; }
				else if ( state.action == CLICK_BATTERY_MODE ) { getBatteryMode(); }
			} else {
				if ( state.action == CLICK_SETUP_MODE ) { setupMode(); }
				else if ( state.action == CLICK_START_MODE ) { saveCurrentBright(); }
				else if ( state.action == CLICK_RESET_MODE ) { resetState(); }
			}
			if ( state.action == CLICK_PROGRAM_MODE ) { state.program = 0; }
		} else {
			state.longClick = state.shortClick = INIT;
		}
	} else {
		switch (state.commandMode) {
			case SETUP_BRIGHT_MODE:
				setupBrightMode();
				break;
		}
	}

	ledPower = state.group[state.brightMode];
	if ( ledChangePower ) { ledPower = ledChangePower; }

	while(1) {
		if ( state.action == CLICK_SOS_MODE ) {
			getSosMode(&ledPower);
		} else {
			setLedPower(ledPower);
			checkBrightState( &ledPower, &brightCounter );
		}
		checkPowerState( &ledPower, &powerCounter );
		delay1s();
	}
}
