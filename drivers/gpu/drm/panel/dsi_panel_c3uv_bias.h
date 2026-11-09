/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DSI_PANEL_C3UV_BIAS_H
#define _DSI_PANEL_C3UV_BIAS_H

#include <linux/types.h>

struct device;
struct regmap;

extern bool lcd_reset_keep_high;
extern bool lcd_gesture_mode;

void set_lcd_reset_gpio_keep_high(bool enable);
void set_lcd_reset_gpio_keep_high_1st(bool enable);
void set_lcd_reset_gpio_keep_high_3rd(bool enable);
void set_lcd_reset_gpio_keep_high_4th(bool enable);
void set_lcd_gesture_mode(bool enable);
struct regmap *lcd_bias_get_regmap(struct device *dev);

#endif
