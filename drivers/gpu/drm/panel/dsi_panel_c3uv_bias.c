// SPDX-License-Identifier: GPL-2.0
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "dsi_panel_c3uv_bias.h"

bool lcd_reset_keep_high;
EXPORT_SYMBOL_GPL(lcd_reset_keep_high);

bool lcd_gesture_mode;
EXPORT_SYMBOL_GPL(lcd_gesture_mode);

void set_lcd_reset_gpio_keep_high(bool enable)
{
	WRITE_ONCE(lcd_reset_keep_high, enable);
}
EXPORT_SYMBOL_GPL(set_lcd_reset_gpio_keep_high);

void set_lcd_gesture_mode(bool enable)
{
	WRITE_ONCE(lcd_gesture_mode, enable);
}
EXPORT_SYMBOL_GPL(set_lcd_gesture_mode);

static void lcd_bias_put_device(void *data)
{
	put_device(data);
}

struct regmap *lcd_bias_get_regmap(struct device *dev)
{
	struct device_node *node;
	struct i2c_client *client;
	struct regmap *map;
	int ret;

	node = of_parse_phandle(dev->of_node, "bias", 0);
	if (!node)
		node = of_find_compatible_node(NULL, NULL, "bias,lcd_bias");
	if (!node)
		return ERR_PTR(-ENODEV);
	client = of_find_i2c_device_by_node(node);
	of_node_put(node);
	if (!client)
		return ERR_PTR(-EPROBE_DEFER);
	ret = devm_add_action_or_reset(dev, lcd_bias_put_device, &client->dev);
	if (ret)
		return ERR_PTR(ret);
	map = dev_get_regmap(&client->dev, NULL);
	if (!map)
		return ERR_PTR(-EPROBE_DEFER);
	if (!device_link_add(dev, &client->dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		return ERR_PTR(-EINVAL);
	return map;
}
EXPORT_SYMBOL_GPL(lcd_bias_get_regmap);

static const struct regmap_config lcd_bias_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x04,
};

static int lcd_bias_probe(struct i2c_client *client)
{
	struct regmap *map = devm_regmap_init_i2c(client, &lcd_bias_regmap);

	return PTR_ERR_OR_ZERO(map);
}

static const struct of_device_id lcd_bias_of_match[] = {
	{ .compatible = "bias,lcd_bias" },
	{ .compatible = "xiaomi,gale-lcd-bias" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd_bias_of_match);

static struct i2c_driver lcd_bias_driver = {
	.probe = lcd_bias_probe,
	.driver = {
		.name = "dsi_panel_c3uv_bias",
		.of_match_table = lcd_bias_of_match,
	},
};
module_i2c_driver(lcd_bias_driver);

MODULE_DESCRIPTION("Gale C3UV LCD bias");
MODULE_LICENSE("GPL");
