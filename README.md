# Blackberry Q10 Keyboard and 303TRACKBA1 Trackball Linux Native Input Device

A fun weekend project to turn an **STM32F411CEU6 ("Black Pill")** into an **I²C slave device** that drives a **Blackberry Q10 keyboard** (BBQ10) and a **303TRACKBA1 Trackball**.  

> Developed by **@mozcelikors** in **November 2025**.
> Special thanks to **@arturo182** and many people prior for reversing this keyboard. Details: [arturo182/BBQ10KBD](https://github.com/arturo182/BBQ10KBD)

<p align="center">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/5.jpeg" alt="Demo" width="800" height="600">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/4.jpeg" alt="Demo" width="800" height="450">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/3.png" alt="Linux Driver Output" width="800" height="160">
</p>

---

## Overview

- Several drivers are presented:
  - **A Keyboard STM32 driver (stm32/Core/Src/keyboard.c):** Scans the keyboard matrix, prepares I2C data if a key is changed, and produces interrupt on KEYBOARD_IRQ line so that Linux can read the prepared value using I2C SMBus protocol.
  - **A Trackball STM32 driver (stm32/Core/Src/trackpad.c):** Read each directional encoder pulses coming from trackball using EXTI interrupts, calculate acceleration factor based on the encoder input frequency change, prepare REL_X and REL_Y values for mouse input, and generate interrupt on TRACKPAD_IRQ line so that Linux can read the prepared value using I2C SMBus protocol.
  - **A Linux kernel driver (linux/driver/bbq10_driver.c):** Upon receiving KEYBOARD_IRQ or TRACKPAD_IRQ interrupts, a seperate work handler is run in order to report received value to Linux input subsystem. In the case of trackball, REL_X and REL_Y values are reported gradually so that user experiences a smoother mouse move effect. In the case of keyboard, key press and key release events are sent in short time. In order to emulate special characters or upper case characters, a SHIFT key press/release can be also emulated.

## Notes
- **Sym** key is configured to act as **Caps Lock**.
- Keys like **Alt**, **RShift**, and **LShift** act as **mode keys** — they must be pressed *before* the actual key.
- Because the keyboard has **no diodes**, **ghosting is common**. Multi-key input was tested but disabled, similar to Blackberry’s original behavior.
---

## Hardware Connections for Demo

<p align="center">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/6.png" alt="Demo" width="800" height="400">
</p>

### BBQ10 Keyboard → STM32F411CEU6 (Black Pill)

| Keyboard Pin | STM32 Pin | Function |
|---------------|------------|-----------|
| Col1 | A0 | Column 1 |
| Col2 | A1 | Column 2 |
| Col3 | A2 | Column 3 |
| Col4 | A3 | Column 4 |
| Col5 | A4 | Column 5 |
| Row1 | B0 | Row 1 |
| Row2 | B1 | Row 2 |
| Row3 | A12 | Row 3 |
| Row4 | B3 | Row 4 |
| Row5 | C15 | Row 5 |
| Row6 | B5 | Row 6 |
| Row7 | B15 | Row 7 |
| VDD  | 3V3 | Power |

### 303TRACKBA1 Trackball → STM32F411CEU6 (Black Pill)

| Trackball Pin | STM32 Pin | Function |
|---------------|------------|-----------|
| BLU | A5 | Blue LED |
| RED | A6 | Red LED |
| GRN | A7 | Green LED |
| WHT | B2 | White LED |
| FOR | B14 | Forward Encoder |
| BAK | A11 | Back Encoder |
| LFT | A15 | Left Encoder |
| RHT | A9 | Right Encoder |
| BTN | B8 | Button |
| VDD  | 3V3 | Power |

### STM32F411CEU6 (Black Pill) → BeagleyAI Board

| BeagleyAI GPIO | STM32F411CEU6 (Black Pill) | Connection |
|----------------|------|-------------|
| GPIO 4 | B13 | IRQ_KEYBOARD |
| GPIO 5 | B12 | IRQ_TRACKPAD |
| GPIO 2 | B7 | I²C1_SDA |
| GPIO 3 | B6 | I²C1_SCL |

---

## I2C SMBus Protocol

STM32F411CEU6 firmware acts as an I2C slave with address 0x52.
Two registers are present to communicate with the underlying hardware.
Linux reads following addresses based on the interrupt received in order to access the registers.
I2C SMBus protocol starts by writing 1 byte register data to the device address, followed by read of the data.

A keypress input causes following example data to appear on I2C bus:
<p align="center">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/7.png" alt="Demo" width="800" height="450">
</p>

A trackball move input causes following example data to appear on I2C bus:
<p align="center">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/refs/heads/main/docs/8.png" alt="Demo" width="800" height="450">
</p>

### I2C SMBus Protocol Register Map



The following table identifies the simple register map for the I2C SMBus protocol that was implemented.

| Address | Name        | Description                     | R/W | Default |
|--------:|-------------|---------------------------------|:---:|:-------:|
| 0x10    | KEYBOARD_VALUE      | 1-byte value representing character to input          | R | 0x00    |
| 0x20    | TRACKBALL_VALUE      | 4-byte movement report                    | R   | 0x00000000    |

#### KEYBOARD_VALUE Register (0x10)

| Byte | Name       | Description                   | Default |
|----:|------------|-------------------------------|:-------:|
| 0   | KEYBOARD_VALUE     | 1-byte value representing character to input                 | 0       |

#### TRACKBALL_VALUE Register (0x20)

| Byte | Name       | Description                   | Default |
|----:|------------|-------------------------------|:-------:|
| 3   | DX_H     | dx High byte                 | 0x00       |
| 2   | DX_L     | dx Low byte                 | 0x00       |
| 1   | DY_H     | dy High byte                 | 0x00       |
| 0   | DY_L     | dy Low byte                 | 0x00       |

If all bytes are 0xFF, this condition represents a button press.

## Keyboard Matrix

### Normal Layout

| Row / Col | 1 | 2 | 3 | 4 | 5 |
|------------|---|---|---|---|---|
| **Row 1** | Q | E | R | U | O |
| **Row 2** | W | S | G | H | L |
| **Row 3** | sym | D | T | Y | I |
| **Row 4** | A | P | R⇧ | ↵ | ⌫ |
| **Row 5** | alt | X | V | B | $ |
| **Row 6** | space | Z | C | N | M |
| **Row 7** | 🎤 | L⇧ | F | J | K |

### Alternate Layout (when **Alt** is active)

| Row / Col | 1 | 2 | 3 | 4 | 5 |
|------------|---|---|---|---|---|
| **Row 1** | # | 2 | 3 | _ | + |
| **Row 2** | 1 | 4 | / | : | " |
| **Row 3** |   | 5 | ( | ) | - |
| **Row 4** | * | @ |   |   |   |
| **Row 5** |   | 8 | ? | ! | 🔊 |
| **Row 6** |   | 7 | 9 | , | . |
| **Row 7** | 0 |   | 6 | ; | ' |

---

## Testing with Linux

For testing, I integrated everything to Beagley-AI board that has TI J722S (Jacinto 7) SoC.

### Device tree Modifications

```
  // I2C1 in expansion header
  &mcu_i2c0 {
    bbq10_driver: bbq10_driver {
		compatible = "mozcelikors,bbq10_driver";
		reg = <0x52>;
		irq-gpios = <&main_gpio0 38 GPIO_ACTIVE_HIGH>,
					<&main_gpio1 15 GPIO_ACTIVE_HIGH>;
					//IRQ_KEYBOARD:  GPIO4, GPIO0_38, W26
					//IRQ_TRACKPAD:  GPIO5, GPIO1_15, B20
		status = "okay";
	};
  };
```
## Demo Video

Click the thumbnail to access video

<a href="https://www.youtube.com/watch?v=PuqCsDF2vTg">
  <img src="https://raw.githubusercontent.com/mozcelikors/blackberry_keyboard_trackpad/main/docs/5.jpeg" style="width:100%; max-width:300px;">
</a>