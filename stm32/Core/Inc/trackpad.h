/**
 * Blackberry Q10 keyboard + Blackberry Trackball 303TRACKBA1 STM32 driver
 * Copyright (C) 2025 Mustafa Ozcelikors
 *
 * See GPLv3 LICENSE file in repository for licensing details.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef INC_TRACKPAD_H_
#define INC_TRACKPAD_H_

#include "stm32f4xx_hal.h"

#define TRACKPAD_BTN_DEBOUNCE_MS 20

typedef enum {
	RED,
	GREEN,
	BLUE,
	WHITE,
	ALL,
	NONE
} color_t;

/* Functions */
void trackpad_init(void);
void trackpad_exti_callback(uint16_t GPIO_Pin);
void trackpad_get_deltas(int16_t *dx, int16_t *dy, uint8_t *btn);
void trackpad_generate_irq_pulse(void);
void trackpad_set_rgb_led (color_t color);

#endif /* INC_TRACKPAD_H_ */
