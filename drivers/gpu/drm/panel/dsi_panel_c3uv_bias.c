// SPDX-License-Identifier: GPL-2.0
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#include "dsi_panel_c3uv_bias.h"

static struct i2c_client *lcd_bias_i2c_client;

int lcd_bias_write_bytes(unsigned char addr, unsigned char value)
{
	int ret;
	char write_data[2] = { addr, value };

	if (!lcd_bias_i2c_client)
		return -ENODEV;

	ret = i2c_master_send(lcd_bias_i2c_client, write_data, 2);
	if (ret < 0)
		pr_err("lcd_bias i2c write failed: %d\n", ret);
	return ret;
}
EXPORT_SYMBOL(lcd_bias_write_bytes);

static int lcd_bias_probe(struct i2c_client *client)
{
	lcd_bias_i2c_client = client;
	return 0;
}

static void lcd_bias_remove(struct i2c_client *client)
{
	lcd_bias_i2c_client = NULL;
}

static const struct of_device_id lcd_bias_of_match[] = {
	{ .compatible = "bias,lcd_bias" },
	{ .compatible = "xiaomi,gale-lcd-bias" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd_bias_of_match);

static struct i2c_driver lcd_bias_driver = {
	.probe = lcd_bias_probe,
	.remove = lcd_bias_remove,
	.driver = {
		.name = "dsi_panel_c3uv_bias",
		.of_match_table = lcd_bias_of_match,
	},
};
module_i2c_driver(lcd_bias_driver);

MODULE_DESCRIPTION("Gale C3UV LCD bias");
MODULE_LICENSE("GPL");
