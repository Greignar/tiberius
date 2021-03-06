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

/*
 * Build:
 * avr-gcc -Wall -g0 -Os -flto -mmcu=attiny13 -c -std=gnu99 -DATTINY=13 -I.. -o tiberius.o -c tiberius.c
 * avr-gcc -Wall -g0 -Os -flto -mmcu=attiny13 -o tiberius.elf tiberius.o
 * avr-objcopy --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O ihex tiberius.elf tiberius.hex
 *
 * Flash:
 * avrdude -c usbasp -p t13 -u -Uflash:w:tiberius.hex -Ulfuse:w:0x75:m -Uhfuse:w:0xFD:m
 */

#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

#include "./driver.h"
#include "./controller.h"

#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/delay_basic.h>

#define _noinit_ __attribute__ ((section (".noinit")))

/*     Start main features     */

#define DEF_ADC_BRIGHT            // Steps down ADC
#define DEF_ADC_LOW_BRIGHT        // Steps down ADC low
#define DEF_ADC_CRIT_BRIGHT       // Steps down ADC crit

#define DEF_BATTERY_STATE         // Battery state

#define DEF_USER_MODE             // Add Alpine or Bicycle or SOS mode (one of)
#ifdef DEF_USER_MODE
//#define DEF_ALPINE_MODE         // Alpine mode
//#define DEF_BICYCLE_MODE        // Bicycle mode
#define DEF_SOS_MODE              // SOS mode
#endif

#ifndef DEF_BICYCLE_MODE
#define DEF_BRIGHT_LIMIT          // Steps down brightness
#endif

/*     End main features      */

// Commands
#define RESET                   0
#define SET                     1

// Clicks normal & program mode
#define CLICK_NEXT_MODE         1
#define CLICK_PREV_MODE         2

// Clicks only normal mode
#define CLICK_MAX_MODE          3
#define CLICK_MIN_MODE          4
#define CLICK_BATTERY_MODE      5
#define CLICK_USER_MODE         6
#define CLICK_PROGRAM_MODE      9

// Clicks only program mode
#define CLICK_SELECT_MODE       3
#define CLICK_START_MODE        4
#define CLICK_RESET_MODE        9

// Virtual clicks
#define CLICK_REDEFINE_MODE   255

// Voltage
#define ADC_LOW               132 // 3.2V
#define ADC_CRIT              128 // 3.0V
#define ADC_OFF               120 // 2.8V

#define BLINK_BRIGHTNESS        3 // Blinking brightness

#define MODES                   5 // Number of brightness modes

// Levels                       0,   1,   2,   3,   4,   5
// Percentages                  0,   2,   5,  14,  37, 100
#define BRIGHTNESS_MODES        0,   5,  13,  35,  94, 255

// Brightness modes
#define BRIGHTNESS_MIN          1 // Minimum
#define BRIGHTNESS_MAX          5 // MODES

// Timers
#define POWER_TIMER             5 // 5 Sec to 1 Step Down
#define BRIGHT_TIMER          180 // 3 Min to 1 Step Down

//Steps Down
#define BRIGHT_LIMIT            4 // For BRIGHT_TIMER (MODES - 1)
#define ADC_LOW_BRIGHT          3 // For ADC_LOW      (MODES - 2)
#define ADC_CRIT_BRIGHT         2 // For ADC_CRIT     (MODES - 3)

typedef struct {
	uint8_t brightPosition;   // Position
	uint8_t rawGroup[MODES];  // Raw group
} eeprom_t;

typedef struct {
	uint8_t brightPosition;   // Brightness position
	uint8_t setupMode;        // Setup brightness
	uint8_t setupPosition;    // Setup position
	uint8_t shortClick;       // Short Click
	uint8_t longClick;        // Long Click
	uint8_t action;           // Action
	uint8_t lightMode;        // Light mode
	uint8_t countModes;       // Mode Counter
	uint8_t group[MODES];     // Current group
	uint8_t rawGroup[MODES];  // Raw group
} state_t;

eeprom_t eeprom _noinit_;         // EEPROM Vars
state_t  state  _noinit_;         // State Vars

PROGMEM const uint8_t brightnessModes[]  = { BRIGHTNESS_MODES };

// Delay of 10mS
void delay10ms(uint8_t n) {
    while (n-- > 0) _delay_loop_2(BOGOMIPS * 10);
}

// Delay of 1S
void delay1s() {
    delay10ms(100);
}

// Delay of 1M
void delay1m() {
	for (uint8_t i = 0; i < 60; i++) { delay1s(); }
}

// Saving the current state to the controller memory
void saveCurrentState() {
	#define EEPROM_BRIGHT (EEPSIZE - 1)
	if (eeprom.brightPosition != state.brightPosition) {
		eeprom_write_byte((uint8_t *)(EEPROM_BRIGHT), state.brightPosition);
		eeprom.brightPosition = state.brightPosition;
	}
	#define EEPROM_MODES (EEPSIZE - 2)
	uint8_t *src = eeprom.rawGroup;
	uint8_t *cpy = state.rawGroup;
	for (uint8_t i = 0; i < MODES; i++) {
		if (*src != *cpy) {
			eeprom_write_byte((uint8_t *)(EEPROM_MODES - i), *cpy);
			*src = *cpy;
		}
		src++; cpy++;
	}
}

// Reset state
void resetState() {
	state.brightPosition = 2;
	state.setupMode = RESET;
	uint8_t *dest = state.rawGroup;
	for (uint8_t i = 1; i <= MODES; i++) { *dest++ = i; }
	saveCurrentState();
}

// Loading the current state from the controller memory
void loadCurrentState() {
	eeprom.brightPosition = eeprom_read_byte((const uint8_t *)EEPROM_BRIGHT);
	state.countModes = RESET;
	uint8_t lastMode = 0;
	uint8_t *dst = state.group;
	uint8_t *src = eeprom.rawGroup;
	uint8_t *cpy = state.rawGroup;
	for(uint8_t i = 0; i < MODES; i++) {
		*src = eeprom_read_byte((uint8_t *) EEPROM_MODES - i);
		if (*src > 0 && lastMode < MODES) { lastMode = *dst++ = *src; state.countModes++; }
		*cpy++ = *src++;
	}
}

// Getting the next brightness mode
void getNextMode() {
	if (++state.brightPosition >= state.countModes) { state.brightPosition = eeprom.brightPosition; }
}

// Getting the previous brightness mode
void getPrevMode() {
	if (state.brightPosition > 0) { state.brightPosition--; }
}

// Setting the brightness of the LED
void setLedPower(uint8_t level) {
	level = pgm_read_byte(brightnessModes + ((level > MODES) ? MODES : level));
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

#ifdef DEF_ALPINE_MODE
// Alpine mode
void getUserMode(uint8_t level) {
	for (uint8_t i = 0; i < 6; i++) {
		setLedPower(level);
		delay10ms(200/10);
		setLedPower(0);
		for (uint8_t j = 0; j < 10; j++) { delay1s(); }
	}
	delay1m();
}
#endif

#ifdef DEF_BICYCLE_MODE
// Bicycle mode
void getUserMode(uint8_t level) {
	for (uint8_t i = 0; i < 3; i++) {
		setLedPower(level);
		delay10ms(100/10);
		setLedPower(0);
		delay10ms(40/10);
	}
	delay10ms(330/10);
}
#endif

#ifdef DEF_SOS_MODE
// SOS mode
void getUserMode(uint8_t level) {
	#define EMERGENCY_SPEED 20
	for (uint8_t i = 0; i < 3; i++) {
		if (i == 1) {
			doImpulses(3, level, EMERGENCY_SPEED*3, 0, EMERGENCY_SPEED);
		} else {
			doImpulses(3, level, EMERGENCY_SPEED, 0, EMERGENCY_SPEED);
		}
		delay10ms(EMERGENCY_SPEED*2);
	}
	delay1m();
}
#endif

#ifdef DEF_BATTERY_STATE
// Outputting the battery level (the more, the better)
void getBatteryState() {
	uint8_t voltage = ADCH;
	if (voltage > ADC_LOW) { doImpulses((voltage - ADC_LOW) >> 3, BLINK_BRIGHTNESS, 500/10, 0, 500/10); }
	delay1s();
}
#endif

#ifdef DEF_ADC_BRIGHT
// Checking the battery state
void checkPowerState(uint8_t *power, uint8_t *count) {
	ADCSRA |= (1 << ADSC);
	while (ADCSRA & (1 << ADSC));
	uint8_t voltage = ADCH;
	#ifdef DEF_ADC_LOW_BRIGHT
	if (voltage < ADC_LOW && *power > ADC_LOW_BRIGHT) {
		*power = ADC_LOW_BRIGHT;
	}
	#endif
	#ifdef DEF_ADC_CRIT_BRIGHT
	*count = (voltage < ADC_CRIT) ? *count + 1 : 0;
	if (*count >= POWER_TIMER) {
		if (*power > ADC_CRIT_BRIGHT) {
			*power = *power - 1;
		} else if (voltage < ADC_OFF) {
			setLedPower(0);
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			sleep_mode();
		}
		*count = 0;
	}
	#endif
}
#endif

#ifdef DEF_BRIGHT_LIMIT
// Limitation of operating time at maximum power
void checkBrightState(uint8_t *power, uint8_t *count) {
	*count = (*power > BRIGHT_LIMIT) ? *count + 1 : 0;
	if (*count >= BRIGHT_TIMER) {
		*power = *power - 1;
		*count = 0;
	}
}
#endif

// Indication of brightness
void indicateBrightMode (uint8_t mode) {
	doImpulses(1, mode+1, 100/10, mode, 2400/10);
}

// Selecting a mode (setup)
void selectMode() {
	delay1s();
	uint8_t lastMode = 0;
	state.setupMode = SET;
	for (uint8_t i = 0; i < MODES && lastMode < MODES; i++) {
		state.setupPosition = i;
		lastMode = state.rawGroup[i];
		indicateBrightMode(state.rawGroup[i]);
	}
	state.setupMode = RESET;
}

// Selecting the brightness level (setup)
void setupMode() {
	delay1s();
	uint8_t oldMode = state.rawGroup[state.setupPosition];
	state.setupMode = state.brightPosition = RESET;
	uint8_t i = 0;
	if (state.setupPosition > 0) { i = state.rawGroup[ state.setupPosition - 1 ]; }
	for (; i <= MODES; i++) {
		state.rawGroup[state.setupPosition] = i;
		saveCurrentState();
		indicateBrightMode(i);
	}
	state.rawGroup[state.setupPosition] = oldMode;
	saveCurrentState();
}

int main(void)
{
	delay10ms(50/10);

	uint8_t ledPower = 0;
	#ifdef DEF_ADC_BRIGHT
	uint8_t powerCounter = 0;
	#endif
	#ifdef DEF_BRIGHT_LIMIT
	uint8_t brightCounter = 0;
	#endif

	DDRB |= (1 << PWM_PIN);
	DIDR0 |= (1 << ADC_DIDR);
	ADMUX = (1 << V_REF) | (1 << ADLAR) | ADC_CHANNEL;
	ADCSRA = (1 << ADEN) | (1 << ADSC) | ADC_PRSCL;

	loadCurrentState();

	if (state.longClick) { state.action = state.setupMode = RESET; state.brightPosition = eeprom.brightPosition; state.lightMode = SET; }
	if (state.setupMode == SET) {  // Setup mode
		setupMode();
	} else {  // Normal mode
		if (state.longClick) {  // Long click
			state.longClick = state.shortClick = RESET;
		} else {  // Short click
			state.action = (state.action > CLICK_PROGRAM_MODE) ? RESET : ++state.shortClick;
			delay10ms(250/10);
			state.shortClick = RESET;
			switch (state.action) {
				case CLICK_NEXT_MODE:
					getNextMode();
					break;
				case CLICK_PREV_MODE:
					getPrevMode();
					break;
			}
			if (state.lightMode == SET) {  // Light mode
				switch (state.action) {
					case CLICK_MAX_MODE:
						ledPower = BRIGHTNESS_MAX;
						break;
					case CLICK_MIN_MODE:
						ledPower = BRIGHTNESS_MIN;
						break;
					#ifdef DEF_BATTERY_STATE
					case CLICK_BATTERY_MODE:
						getBatteryState();
						break;
					#endif
					case CLICK_PROGRAM_MODE:
						state.lightMode = RESET;
						doImpulses(10, BLINK_BRIGHTNESS, 300/10/10, 0, 300/10/10);
						break;
				}
			} else {  // Program mode
				switch (state.action)  {
					case CLICK_SELECT_MODE:
						selectMode();
						break;
					case CLICK_START_MODE:
						saveCurrentState();
						break;
					case CLICK_RESET_MODE:
						resetState();
						break;
				}
			}
		}
	}

	ledPower = (ledPower) ? ledPower : state.group[state.brightPosition];

	while(1) {

		#ifdef DEF_USER_MODE
		if (state.action == CLICK_USER_MODE) {
			#ifdef DEF_ALPINE_MODE
			getUserMode(ledPower);
			#endif
			#ifdef DEF_BICYCLE_MODE
			getUserMode(ledPower);
			#endif
			#ifdef DEF_SOS_MODE
			getUserMode(ledPower);
			#endif
		} else {
			if (ledPower != state.group[state.brightPosition]) { state.action = CLICK_REDEFINE_MODE; }
			setLedPower(ledPower);
			delay1s();
		}
		#else
		if (ledPower != state.group[state.brightPosition]) { state.action = CLICK_REDEFINE_MODE; }
		setLedPower(ledPower);
		delay1s();
		#endif

		#ifdef DEF_BRIGHT_LIMIT
		checkBrightState(&ledPower, &brightCounter);
		#endif

		#ifdef DEF_ADC_BRIGHT
		checkPowerState(&ledPower, &powerCounter);
		#endif

	}
}
