/**
 * Blackberry Q10 keyboard + Blackberry Trackball 303TRACKBA1 Linux driver as a native keyboard & mouse
 *
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>

#define BBQ10_DEBUG 1

#define ECHODEV_REG_ADDR_READ_KEYBOARD 0x10
#define ECHODEV_REG_ADDR_READ_TRACKBALL 0x20

struct bbq10_data {
    struct i2c_client *client;
    struct gpio_desc *irq_gpio[2]; // 1st irq for keyboard, 2nd for trackpad
    struct input_dev *kbd_input;
    struct input_dev *mouse_input;
    struct work_struct key_work;
    struct work_struct trackball_work;
    int irq[2];
    u8 key_value;
    u8 trackball_value[4];
};

static const unsigned short alphabet[] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
};

static const unsigned short numbers[] = {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
};

/* Map received characters to Linux keycodes */
static unsigned short bbq10_char_to_keycode(u8 ch, bool *needs_shift)
{
    *needs_shift = false;
    
    /* Lowercase letters */
    if (ch >= 'a' && ch <= 'z') {
        return alphabet[ch - 'a'];
    }
    
    /* Uppercase letters */
    if (ch >= 'A' && ch <= 'Z') {
        *needs_shift = true;
        return alphabet[ch - 'A'];
    }
    
    /* Numbers */
    if (ch >= '0' && ch <= '9') {
        return numbers[ch - '0'];
    }
    
    /* Special characters */
    switch (ch) {
    case ' ':
        return KEY_SPACE;
    case '\n':
        return KEY_ENTER;
    case '\r':
        return KEY_BACKSPACE;
    case '.':
        return KEY_DOT;
    case ',':
        return KEY_COMMA;
    case '/':
        return KEY_SLASH;
    case ';':
        return KEY_SEMICOLON;
    case '\'':
        return KEY_APOSTROPHE;
    case '-':
        return KEY_MINUS;
        
    /* Shifted symbols */
    case '!':
        *needs_shift = true;
        return KEY_1;
    case '@':
        *needs_shift = true;
        return KEY_2;
    case '#':
        *needs_shift = true;
        return KEY_3;
    case '$':
        *needs_shift = true;
        return KEY_4;
    case '_':
        *needs_shift = true;
        return KEY_MINUS;
    case '+':
        *needs_shift = true;
        return KEY_EQUAL;
    case ':':
        *needs_shift = true;
        return KEY_SEMICOLON;
    case '"':
        *needs_shift = true;
        return KEY_APOSTROPHE;
    case '?':
        *needs_shift = true;
        return KEY_SLASH;
    case '(':
        *needs_shift = true;
        return KEY_9;
    case ')':
        *needs_shift = true;
        return KEY_0;
    case '*':
        *needs_shift = true;
        return KEY_8;
        
    default:
        return KEY_UNKNOWN;
    }
}

/* Keyboard work handler */
static void bbq10_key_work_handler(struct work_struct *work)
{
    struct bbq10_data *data = container_of(work, struct bbq10_data, key_work);
    unsigned short keycode;
    bool needs_shift;
    u8 val = data->key_value;

#ifdef BBQ10_DEBUG
    pr_info("bbq10_driver: processing key 0x%02x ('%c')\n", 
            val, (val >= 32 && val < 127) ? val : '?');
#endif

    /* Get keycode and shift requirement */
    keycode = bbq10_char_to_keycode(val, &needs_shift);

    if (keycode == KEY_UNKNOWN) {
        pr_err("bbq10_driver: unknown character 0x%02x\n", val);
        return;
    }

#ifdef BBQ10_DEBUG
    pr_info("bbq10_driver: keycode=%d, needs_shift=%d\n", keycode, needs_shift);
#endif

    /* Press shift if needed */
    if (needs_shift) {
        input_report_key(data->kbd_input, KEY_LEFTSHIFT, 1);
        input_sync(data->kbd_input);
    }

    /* Press and release the key */
    input_report_key(data->kbd_input, keycode, 1);  /* Press */
    input_sync(data->kbd_input);
    
    usleep_range(8000, 10000);
    
    input_report_key(data->kbd_input, keycode, 0);  /* Release */
    input_sync(data->kbd_input);

    /* Release shift if it was pressed */
    if (needs_shift) {
        input_report_key(data->kbd_input, KEY_LEFTSHIFT, 0);
        input_sync(data->kbd_input);
    }
}

static irqreturn_t bbq10_keyboard_irq_handler(int irq, void *dev_id)
{
    struct bbq10_data *data = dev_id;
    int ret;

    ret = i2c_smbus_read_byte_data(data->client, ECHODEV_REG_ADDR_READ_KEYBOARD);
    if (ret < 0) {
        pr_err("bbq10_driver: i2c_smbus_read_byte_data failed, ret=%d\n", ret);
        return IRQ_HANDLED;
    }

    data->key_value = ret;

    /* Schedule work to process the key */
    schedule_work(&data->key_work);

    return IRQ_HANDLED;
}

/* Trackball work handler */
static void bbq10_trackball_work_handler(struct work_struct *work)
{
    struct bbq10_data *data =
        container_of(work, struct bbq10_data, trackball_work);

    struct input_dev *input = data->mouse_input;
    int i;

    bool is_tap =
        data->trackball_value[0] == 0xFF &&
        data->trackball_value[1] == 0xFF &&
        data->trackball_value[2] == 0xFF &&
        data->trackball_value[3] == 0xFF;

    if (is_tap)
    {
        input_report_key(input, BTN_LEFT, 1);
        input_sync(input);

        usleep_range(8000, 10000);

        input_report_key(input, BTN_LEFT, 0);
        input_sync(input);

        return;
    }

    s16 dx = (s16)((data->trackball_value[0] << 8) |
                    data->trackball_value[1]);
    s16 dy = (s16)((data->trackball_value[2] << 8) |
                    data->trackball_value[3]);

#ifdef BBQ10_DEBUG
    pr_info("bbq10_driver: bbq10_trackball_work_handler mouse values (%d, %d)\n", dx, dy);
#endif

    /* Split movement into small steps */
    s16 abs_dx = dx < 0 ? -dx : dx;
    s16 abs_dy = dy < 0 ? -dy : dy;
    s16 steps = abs_dx > abs_dy ? abs_dx : abs_dy;
    
    if (steps == 0) return;
    
    /* Send movement in small increments */
    for (i = 0; i < steps; i++) {
        s16 step_x = 0, step_y = 0;
        
        if (abs_dx > 0) {
            step_x = dx > 0 ? 1 : -1;
            abs_dx--;
        }
        if (abs_dy > 0) {
            step_y = dy > 0 ? 1 : -1;
            abs_dy--;
        }
        
        input_report_rel(input, REL_X, step_x);
        input_report_rel(input, REL_Y, step_y);
        input_sync(input);
        
        usleep_range(100, 500);  /* Very small delay between steps */
    }
}

static irqreturn_t bbq10_trackball_irq_handler(int irq, void *dev_id)
{
    struct bbq10_data *data = dev_id;
    int ret;
    u8 buf[4];

    ret = i2c_smbus_read_i2c_block_data(data->client,
                                        ECHODEV_REG_ADDR_READ_TRACKBALL,
                                        4,
                                        buf);
    if (ret < 0) {
        pr_err("bbq10_driver: i2c_smbus_read_i2c_block_data failed, ret=%d\n", ret);
        return IRQ_HANDLED;
    }

    if (ret != 4) {
        pr_err("bbq10_driver: expected 4 bytes, got %d\n", ret);
        return IRQ_HANDLED;
    }

    /* Copy the 4 bytes into your driver data */
    memcpy(data->trackball_value, buf, 4);

#ifdef BBQ10_DEBUG
    pr_info("bbq10_driver: bbq10_trackball_work_handler trackball values (%d, %d, %d, %d)\n", data->trackball_value[0],
                                                                                             data->trackball_value[1],
                                                                                             data->trackball_value[2],
                                                                                             data->trackball_value[3]);
#endif

    /* Schedule work to process the trackball data */
    schedule_work(&data->trackball_work);

    return IRQ_HANDLED;
}

static int bbq10_probe(struct i2c_client *client,
                       const struct i2c_device_id *id)
{
    struct bbq10_data *data;
    int ret;
    int i;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;

    /* Initialize work queues */
    INIT_WORK(&data->key_work, bbq10_key_work_handler);
    INIT_WORK(&data->trackball_work, bbq10_trackball_work_handler);

    /* Create native keyboard input device */
    data->kbd_input = devm_input_allocate_device(&client->dev);
    if (!data->kbd_input) {
        dev_err(&client->dev, "Failed to allocate input device\n");
        return -ENOMEM;
    }

    data->kbd_input->name = "BBQ10 Keyboard";
    data->kbd_input->phys = "i2c/bbq10";
    data->kbd_input->id.bustype = BUS_I2C;
    data->kbd_input->id.vendor = 0x0001;
    data->kbd_input->id.product = 0x0001;
    data->kbd_input->id.version = 0x0100;
    data->kbd_input->dev.parent = &client->dev;

    /* Set up supported key events */
    __set_bit(EV_KEY, data->kbd_input->evbit);
    __set_bit(EV_REP, data->kbd_input->evbit);  /* Enable key repeat */

    /* Enable all letter keys */
    for (i = 0; i < 26; i++)
        __set_bit(alphabet[i], data->kbd_input->keybit);

    /* Enable number keys */
    for (i = 0; i < 10; i++)
        __set_bit(numbers[i], data->kbd_input->keybit);

    /* Enable special keys */
    __set_bit(KEY_SPACE, data->kbd_input->keybit);
    __set_bit(KEY_ENTER, data->kbd_input->keybit);
    __set_bit(KEY_BACKSPACE, data->kbd_input->keybit);
    __set_bit(KEY_LEFTSHIFT, data->kbd_input->keybit);
    __set_bit(KEY_DOT, data->kbd_input->keybit);
    __set_bit(KEY_COMMA, data->kbd_input->keybit);
    __set_bit(KEY_SLASH, data->kbd_input->keybit);
    __set_bit(KEY_SEMICOLON, data->kbd_input->keybit);
    __set_bit(KEY_APOSTROPHE, data->kbd_input->keybit);
    __set_bit(KEY_MINUS, data->kbd_input->keybit);
    __set_bit(KEY_EQUAL, data->kbd_input->keybit);

    /* Register keyboard input device */
    ret = input_register_device(data->kbd_input);
    if (ret) {
        dev_err(&client->dev, "Failed to register input device: %d\n", ret);
        return ret;
    }

    /* Create native mouse input device */
    data->mouse_input = devm_input_allocate_device(&client->dev);
    if (!data->mouse_input)
        return -ENOMEM;

    data->mouse_input->name = "BBQ10 Trackball";
    data->mouse_input->phys = "i2c/bbq10-trackball";
    data->mouse_input->id.bustype = BUS_I2C;
    data->mouse_input->id.vendor  = 0x0001;
    data->mouse_input->id.product = 0x0002;
    data->mouse_input->id.version = 0x0100;

    /* Relative motion axes */
    __set_bit(EV_REL, data->mouse_input->evbit);
    __set_bit(REL_X, data->mouse_input->relbit);
    __set_bit(REL_Y, data->mouse_input->relbit);

    /* Mouse buttons */
    __set_bit(EV_KEY, data->mouse_input->evbit);
    __set_bit(BTN_LEFT, data->mouse_input->keybit);
    __set_bit(BTN_RIGHT, data->mouse_input->keybit);

    /* Register mouse device */
    ret = input_register_device(data->mouse_input);
    if (ret) {
        dev_err(&client->dev, "Failed to register mouse device: %d\n", ret);
        return ret;
    }

    /* Keyboard IRQ */
    data->irq_gpio[0] = devm_gpiod_get_index(&client->dev, "irq", 0, GPIOD_IN);
    if (IS_ERR(data->irq_gpio[0])) {
        dev_err(&client->dev, "Failed to get GPIO\n");
        return PTR_ERR(data->irq_gpio[0]);
    }

    data->irq[0] = gpiod_to_irq(data->irq_gpio[0]);
    if (data->irq[0] < 0) {
        dev_err(&client->dev, "Failed to get IRQ for GPIO\n");
        return data->irq[0];
    }

    ret = devm_request_threaded_irq(&client->dev, data->irq[0],
                                    NULL, bbq10_keyboard_irq_handler,
                                    IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                    "bbq10", data);
    if (ret) {
        dev_err(&client->dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    /* Trackball IRQ */
    data->irq_gpio[1] = devm_gpiod_get_index (&client->dev, "irq", 1, GPIOD_IN);
    if (IS_ERR(data->irq_gpio[1])) {
        dev_err(&client->dev, "Failed to get GPIO\n");
        return PTR_ERR(data->irq_gpio[1]);
    }

    data->irq[1] = gpiod_to_irq(data->irq_gpio[1]);
    if (data->irq[1] < 0) {
        dev_err(&client->dev, "Failed to get IRQ for GPIO\n");
        return data->irq[1];
    }

    ret = devm_request_threaded_irq(&client->dev, data->irq[1],
                                    NULL, bbq10_trackball_irq_handler,
                                    IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                    "bbq10", data);
    if (ret) {
        dev_err(&client->dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    i2c_set_clientdata(client, data);
    dev_info(&client->dev, "bbq10 keyboard and trackball driver probed successfully\n");

    return 0;
}

static void bbq10_remove(struct i2c_client *client)
{
    struct bbq10_data *data = i2c_get_clientdata(client);
    
    cancel_work_sync(&data->key_work);
    cancel_work_sync(&data->trackball_work);
    
    dev_info(&client->dev, "bbq10 driver removed\n");
}

static const struct of_device_id bbq10_of_match[] = {
    { .compatible = "mozcelikors,bbq10_driver", },
    { }
};
MODULE_DEVICE_TABLE(of, bbq10_of_match);

static const struct i2c_device_id bbq10_id[] = {
    { "bbq10_driver", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, bbq10_id);

static struct i2c_driver bbq10_driver = {
    .driver = {
        .name = "bbq10_driver",
        .of_match_table = bbq10_of_match,
    },
    .probe = bbq10_probe,
    .remove = bbq10_remove,
    .id_table = bbq10_id,
};

module_i2c_driver(bbq10_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mustafa Ozcelikors");
MODULE_DESCRIPTION("I2C input driver for STM32 BBQ10 keyboard and 303TRACKBA1 trackball found in github.com/mozcelikors/blackberry_keyboard_trackpad");