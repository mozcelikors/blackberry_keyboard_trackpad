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

#include "trackpad.h"

#define TRACKPAD_PIN_COUNT 9
#define TRACKPAD_STEP 10

typedef enum {
    TP_BLU,
    TP_RED,
    TP_GRN,
    TP_WHT,
    TP_UP,
    TP_DWN,
    TP_LFT,
    TP_RHT,
    TP_BTN
} TrackpadPinName;

GPIO_TypeDef* trackpad_ports[TRACKPAD_PIN_COUNT] = {
    GPIOA, GPIOA, GPIOA, GPIOB,  // BLU, RED, GRN, WHT
    GPIOB, GPIOA, GPIOA, GPIOA,  // UP, DWN, LFT, RHT
    GPIOA                        // BTN
};

uint16_t trackpad_pins[TRACKPAD_PIN_COUNT] = {
    GPIO_PIN_5, // BLU
    GPIO_PIN_6, // RED
    GPIO_PIN_7, // GRN
    GPIO_PIN_2, // WHT
    GPIO_PIN_14, // UP
    GPIO_PIN_11, // DWN
    GPIO_PIN_15, // LFT
    GPIO_PIN_9, // RHT
    GPIO_PIN_8  // BTN
};

GPIO_TypeDef*    trackpad_irq_port = GPIOB;
const uint16_t   trackpad_irq_pin  = GPIO_PIN_12;

volatile int16_t trackpad_x = 0;
volatile int16_t trackpad_y = 0;
volatile uint8_t trackpad_btn = 0;
uint32_t last_btn_tick = 0;
float x_accel_factor = 1.0;
float y_accel_factor = 1.0;

// Read deltas + button, resets accumulators
void trackpad_get_deltas(int16_t *dx, int16_t *dy, uint8_t *btn)
{
    __disable_irq();
    *dx = trackpad_x;
    *dy = trackpad_y;
    *btn = trackpad_btn;
    trackpad_x = 0;
    trackpad_y = 0;
    trackpad_btn = 0;
    __enable_irq();
}

void trackpad_init_exti(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

    // Configure trackball movement pins and BTN as EXTI
    TrackpadPinName exti_pins[] = { TP_UP, TP_DWN, TP_LFT, TP_RHT, TP_BTN };
    int exti_count = sizeof(exti_pins)/sizeof(exti_pins[0]);

    for(int i = 0; i < exti_count; i++)
    {
        TrackpadPinName name = exti_pins[i];
        GPIO_InitStruct.Pin = trackpad_pins[name];
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(trackpad_ports[name], &GPIO_InitStruct);

        // Determine EXTI IRQ number based on pin
        IRQn_Type irq = 0;
        switch(trackpad_pins[name])
        {
            case GPIO_PIN_0: irq = EXTI0_IRQn; break;
            case GPIO_PIN_1: irq = EXTI1_IRQn; break;
            case GPIO_PIN_2: irq = EXTI2_IRQn; break;
            case GPIO_PIN_3: irq = EXTI3_IRQn; break;
            case GPIO_PIN_4: irq = EXTI4_IRQn; break;
            case GPIO_PIN_5:
            case GPIO_PIN_6:
            case GPIO_PIN_7:
                irq = EXTI9_5_IRQn; break;
            case GPIO_PIN_8:
            case GPIO_PIN_9:
                irq = EXTI9_5_IRQn; break;
            case GPIO_PIN_10:
            case GPIO_PIN_11:
            case GPIO_PIN_12:
            case GPIO_PIN_13:
            case GPIO_PIN_14:
            case GPIO_PIN_15:
                irq = EXTI15_10_IRQn; break;
        }

        // Enable and set priority, need to be lower priority than I2C
        HAL_NVIC_SetPriority(irq, 1, 0);
        HAL_NVIC_EnableIRQ(irq);
    }
}

void trackpad_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

	for (int i = 0; i < TP_UP; i++)
	{
		GPIO_InitStruct.Pin = trackpad_pins[i];
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		HAL_GPIO_Init(trackpad_ports[i], &GPIO_InitStruct);
	}

	trackpad_init_exti();

	trackpad_set_rgb_led (ALL);

	// Configure interrupt pin as output, start low
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Pin = trackpad_irq_pin;
	HAL_GPIO_Init(trackpad_irq_port, &GPIO_InitStruct);
	HAL_GPIO_WritePin(trackpad_irq_port, trackpad_irq_pin, GPIO_PIN_RESET);
}

void trackpad_set_rgb_led (color_t color)
{
	switch(color)
	{
	case WHITE:
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_SET);
		break;
	case RED:
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_SET);
		break;
	case GREEN:
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_SET);
		break;
	case BLUE:
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_RESET);
		break;
	case NONE:
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_SET);
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_SET);
		break;
	case ALL: default:
		HAL_GPIO_WritePin(trackpad_ports[TP_BLU], trackpad_pins[TP_BLU], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_RED], trackpad_pins[TP_RED], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_GRN], trackpad_pins[TP_GRN], GPIO_PIN_RESET);
		HAL_GPIO_WritePin(trackpad_ports[TP_WHT], trackpad_pins[TP_WHT], GPIO_PIN_RESET);
		break;
	}
}

static uint8_t absvalue (int8_t val)
{
	if (val < 0) val*=-1;
	return val;
}

static float get_accel_factor(uint8_t step)
{
	float accel_factor = 1.0;

	if (absvalue(step) >= TRACKPAD_STEP*7)
		accel_factor = 7.0;
	else if (absvalue(step) >= TRACKPAD_STEP*5)
		accel_factor = 5.0;
	else if(absvalue(step) >= TRACKPAD_STEP*3)
		accel_factor = 3.0;
	else if(absvalue(step) >= TRACKPAD_STEP*2)
		accel_factor = 2.0;
	else if (absvalue(step) >= TRACKPAD_STEP)
		accel_factor = 1.3;
	else
		accel_factor = 1.0;

	return accel_factor;
}

void trackpad_update_pin(TrackpadPinName pin_name)
{
	float accel_factor_x = get_accel_factor(trackpad_x);
	float accel_factor_y = get_accel_factor(trackpad_y);

    switch(pin_name)
    {
        case TP_LFT:
            trackpad_x += (int16_t)(TRACKPAD_STEP * accel_factor_x);
            break;
        case TP_RHT:
            trackpad_x -= (int16_t)(TRACKPAD_STEP * accel_factor_x);
            break;
        case TP_UP:
            trackpad_y += (int16_t)(TRACKPAD_STEP * accel_factor_y);
            break;
        case TP_DWN:
            trackpad_y -= (int16_t)(TRACKPAD_STEP * accel_factor_y);
            break;
        case TP_BTN:
        {
            uint32_t t = HAL_GetTick();
            if((t - last_btn_tick) >= TRACKPAD_BTN_DEBOUNCE_MS)
            {
                last_btn_tick = t;
                GPIO_PinState state = HAL_GPIO_ReadPin(trackpad_ports[TP_BTN], trackpad_pins[TP_BTN]);
                trackpad_btn = (state == GPIO_PIN_RESET); // active-low
            }
            break;
        }
        default:
            // LEDs: TP_BLU, TP_RED, TP_GRN, TP_WHT – ignore here
            break;
    }
}

void trackpad_generate_irq_pulse(void)
{
	// Pulse on interrupt output pin TRACKPAD_CHANGED_IRQ
	HAL_GPIO_WritePin(trackpad_irq_port, trackpad_irq_pin, GPIO_PIN_SET);
	HAL_Delay(1);
	HAL_GPIO_WritePin(trackpad_irq_port, trackpad_irq_pin, GPIO_PIN_RESET);
}

void trackpad_exti_callback(uint16_t GPIO_Pin)
{
    for (int i = 0; i < TRACKPAD_PIN_COUNT; i++)
    {
        if (trackpad_pins[i] == GPIO_Pin)
        {
            trackpad_update_pin((TrackpadPinName)i);
            break;
        }
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    trackpad_exti_callback(GPIO_Pin);
}

void EXTI15_10_IRQHandler(void)
{
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_10) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_11) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_12) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_15) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}

void EXTI9_5_IRQHandler(void)
{
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_5) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_6) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_7) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_8) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
    if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_9) != RESET) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}
