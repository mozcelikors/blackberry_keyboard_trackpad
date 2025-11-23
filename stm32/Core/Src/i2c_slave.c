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

#include "i2c_slave.h"
#include "keyboard.h"

I2C_HandleTypeDef hi2c1;

uint8_t I2C_RxData[1];

// volatile because may be accessed from ISR
volatile uint8_t I2C_Keyboard_TxData[1] = {0x00};
volatile uint8_t I2C_Trackpad_TxData[4] = {0x00, 0x00, 0x00, 0x00};
volatile uint8_t i2c_busy = 0;

void I2C_Error_Handler(void);

void MX_I2C1_Init_Slave(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable clocks */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* Configure GPIOs: PB6=SCL, PB7=SDA for I2C1 */
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Configure I2C1 as slave */
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = (KEYBOARD_I2C_ADDRESS << 1);
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        I2C_Error_Handler();
    }

    /* Configure analog filter */
    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);

    /* Enable interrupts */
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);

    HAL_I2C_EnableListen_IT(&hi2c1);

    // DEBUG:
    volatile uint32_t cr1_val = I2C1->CR1;
    volatile uint32_t cr2_val = I2C1->CR2;
    volatile uint32_t oar1_val = I2C1->OAR1;
    // cr1_val should have PE bit (bit 0) set = 0x0001 at minimum
    // cr2_val should have ITEVTEN (bit 9) and ITERREN (bit 8) set
    // oar1_val should be 0x4052 or 0xC052 (bit 14 must be 1, address in bits 1-7)
    (void)cr1_val; (void)cr2_val; (void)oar1_val; // Prevent optimization
}

void set_i2c_keyboard_txdata(char c)
{
	I2C_Keyboard_TxData[0] = c;
}

void set_i2c_trackpad_txdata(int16_t dx, int16_t dy)
{
	I2C_Trackpad_TxData[0] = (dx >> 8) & 0xFF; // dx High Byte
	I2C_Trackpad_TxData[1] = dx & 0xFF;        // dx Low Byte
	I2C_Trackpad_TxData[2] = (dy >> 8) & 0xFF; // dy High Byte
	I2C_Trackpad_TxData[3] = dy & 0xFF;        // dy Low Byte
}

void set_i2c_trackpad_mouseclick_txdata(void)
{
	I2C_Trackpad_TxData[0] = 0xFF;
	I2C_Trackpad_TxData[1] = 0xFF;
	I2C_Trackpad_TxData[2] = 0xFF;
	I2C_Trackpad_TxData[3] = 0xFF;
}

void wait_i2c_busy(void)
{
    // Only update when not in the middle of I2C transaction
    while(i2c_busy);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    // Listen completed
    i2c_busy = 0;
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c,
                          uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
    if (hi2c->Instance != I2C1)
        return;

    i2c_busy = 1;

    if (TransferDirection == I2C_DIRECTION_TRANSMIT)
    {
        // Master is writing to us
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, I2C_RxData, 1, I2C_FIRST_AND_LAST_FRAME);
    }
    else
    {
        // Master is reading from us
    	if (I2C_RxData[0] == ECHODEV_REG_ADDR_READ_KEYBOARD)
    	{
    		HAL_I2C_Slave_Seq_Transmit_IT(hi2c, (uint8_t*)I2C_Keyboard_TxData, 1, I2C_FIRST_AND_LAST_FRAME);
    	}
    	else if (I2C_RxData[0] == ECHODEV_REG_ADDR_READ_TRACKBALL)
    	{
    		HAL_I2C_Slave_Seq_Transmit_IT(hi2c, (uint8_t*)I2C_Trackpad_TxData, 4, I2C_FIRST_AND_LAST_FRAME);
    	}
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    // Process received data if needed
    i2c_busy = 0;
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    // Transmit complete
    i2c_busy = 0;
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    // Disable interrupts during recovery
    __disable_irq();

    // Clear I2C error flags
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_AF | I2C_FLAG_OVR);

    // Deinit and reinit the peripheral
    HAL_I2C_DeInit(hi2c);
    MX_I2C1_Init_Slave();

    i2c_busy = 0;

    __enable_irq();
}

void I2C_Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

void I2C1_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&hi2c1);
}
