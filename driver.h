/* 
 * This file is part of the fw02 distribution.
 * Copyright (c) 2018 Arcady N. Shpak.
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

#define PWM_PIN		PB1
#define ADC_CHANNEL	0x01	// MUX 01 corresponds with PB2
#define ADC_DIDR	ADC1D	// Digital input disable bit corresponding with PB2
#define ADC_PRSCL	0x06	// clk/64
#define PWM_LVL		OCR0B	// OCR0B is the output compare register for PB1
#define FAST		0x23	// fast PWM channel 1 only
#define PHASE 		0x21	// phase-correct PWM channel 1 only



