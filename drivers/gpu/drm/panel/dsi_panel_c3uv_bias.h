/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DSI_PANEL_C3UV_BIAS_H
#define _DSI_PANEL_C3UV_BIAS_H

#include <linux/types.h>

struct device;
struct regmap;

int lcd_bias_write_bytes(unsigned char addr, unsigned char value);

#endif
