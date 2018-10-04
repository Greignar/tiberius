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

#include "../driver.h"
#include "../controller.h"

#include <avr/sleep.h>
#include <util/delay_basic.h>

// Delay of 10mS
void delay10ms(uint8_t n) {
    while (n-- > 0) _delay_loop_2(BOGOMIPS * 10);
}

// Delay of 1S
void delay1s(uint8_t n) {
    while (n-- > 0) delay10ms(100);
}

void setLedPower(uint8_t level) {
	TCCR0A = PHASE;
	TCCR0B = 0x02;
	PWM_LVL = level;
}

// Output of the pulse of arbitrary amplitude and duty cycle
void doImpulses(uint8_t count, uint8_t level) {
	while (count--) {
		setLedPower(level);
		delay10ms(50);
		setLedPower(0);
		delay10ms(50);
	}
}

int main(void)
{
	DDRB |= (1 << PWM_PIN);
	DIDR0 |= (1 << ADC_DIDR);
	ADMUX  = (1 << V_REF) | (1 << ADLAR) | ADC_CHANNEL;
	ADCSRA = (1 << ADEN) | (1 << ADSC) | ADC_PRSCL;

	while(1) {
		for(uint8_t i = 0; i < 3; i++) {
			setLedPower(1 + i * 127);
			delay1s(3);
			if (ADCSRA & (1 << ADIF)) {
				uint8_t voltage = ADCH;
				ADCSRA |= (1 << ADSC);
				uint8_t n100, n10, n1;
				n100 = voltage / 100;
				n10 = (voltage % 100) / 10;
				n1 = (voltage % 100) % 10;
				setLedPower(0);
				delay1s(3);
				doImpulses(n100, 1 + i * 127);
				delay1s(1);
				doImpulses(n10, 1 + i * 127);
				delay1s(1);
				doImpulses(n1, 1 + i * 127);
				delay1s(5);
			}
		}
		delay1s(5);
	}
}
