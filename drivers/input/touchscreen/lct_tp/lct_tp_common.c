// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>

#include "lct_tp_common.h"
#include "tp_common.h"

char tp_lockdowninfo[24];
EXPORT_SYMBOL_GPL(tp_lockdowninfo);

int tp_select_proximity = 1;
EXPORT_SYMBOL_GPL(tp_select_proximity);

touchscreen_usb_plugin_data_t g_touchscreen_usb_pulgin;
EXPORT_SYMBOL_GPL(g_touchscreen_usb_pulgin);

static struct kobject *touchpanel_kobj;
static struct kobj_attribute double_tap_attr = __ATTR(double_tap, 0664, NULL, NULL);
static DEFINE_MUTEX(touchpanel_lock);
static bool usb_notifier_registered;

bool lct_tp_panel_match(const char *name)
{
	struct device_node *chosen;
	const char *args, *value;
	bool match = false;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		return false;
	if (!of_property_read_string(chosen, "bootargs", &args)) {
		value = strstr(args, "panel_name=");
		if (value && (value == args || isspace(value[-1]))) {
			value += strlen("panel_name=");
		} else {
			value = strstr(args, "lcm_name=");
			if (value && (value == args || isspace(value[-1])))
				value += strlen("lcm_name=");
			else
				value = NULL;
		}
		if (value)
			match = strcspn(value, " \t") == strlen(name) &&
				!strncmp(value, name, strlen(name));
	}
	of_node_put(chosen);
	return match;
}
EXPORT_SYMBOL_GPL(lct_tp_panel_match);

int tp_common_set_double_tap_ops(struct tp_common_ops *ops)
{
	int ret;

	if (!ops)
		return -EINVAL;
	mutex_lock(&touchpanel_lock);
	if (touchpanel_kobj) {
		ret = -EBUSY;
		goto unlock;
	}
	touchpanel_kobj = kobject_create_and_add("touchpanel", NULL);
	if (!touchpanel_kobj) {
		ret = -ENOMEM;
		goto unlock;
	}
	double_tap_attr.show = ops->show;
	double_tap_attr.store = ops->store;
	ret = sysfs_create_file(touchpanel_kobj, &double_tap_attr.attr);
	if (ret) {
		kobject_put(touchpanel_kobj);
		touchpanel_kobj = NULL;
		goto unlock;
	}
unlock:
	mutex_unlock(&touchpanel_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tp_common_set_double_tap_ops);

void tp_common_unregister_double_tap_ops(void)
{
	mutex_lock(&touchpanel_lock);
	if (touchpanel_kobj) {
		sysfs_remove_file(touchpanel_kobj, &double_tap_attr.attr);
		kobject_put(touchpanel_kobj);
		touchpanel_kobj = NULL;
	}
	mutex_unlock(&touchpanel_lock);
}
EXPORT_SYMBOL_GPL(tp_common_unregister_double_tap_ops);

int ps_send_touch_event(int32_t data)
{
	int (*send)(int32_t value) = READ_ONCE(tp_ps_send_touch_event);

	if (!READ_ONCE(ps_send_touch_event_probe) || !send)
		return -ENODEV;
	return send(data);
}
EXPORT_SYMBOL_GPL(ps_send_touch_event);

static int lct_tp_usb_notify(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	struct power_supply *psy = data;
	union power_supply_propval val;
	void (*callback)(void);

	if (event != PSY_EVENT_PROP_CHANGED ||
	    psy->desc->type == POWER_SUPPLY_TYPE_BATTERY)
		return NOTIFY_DONE;
	if (power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val))
		return NOTIFY_DONE;
	WRITE_ONCE(g_touchscreen_usb_pulgin.usb_plugged_in, !!val.intval);
	callback = READ_ONCE(g_touchscreen_usb_pulgin.event_callback);
	if (READ_ONCE(g_touchscreen_usb_pulgin.valid) && callback)
		callback();
	return NOTIFY_OK;
}

static struct notifier_block lct_tp_usb_notifier = {
	.notifier_call = lct_tp_usb_notify,
};

int lct_tp_create_procfs(lct_tp_mifunc_t *funcs)
{
	struct device_node *chosen;
	const char *args, *value;
	int ret;

	if (!funcs)
		return -EINVAL;
	chosen = of_find_node_by_path("/chosen");
	if (chosen && !of_property_read_string(chosen, "bootargs", &args)) {
		value = strstr(args, "tp_lockdown_info=");
		if (value && (value == args || value[-1] == ' ')) {
			value += strlen("tp_lockdown_info=");
			scnprintf(tp_lockdowninfo, sizeof(tp_lockdowninfo), "%.*s\n",
				  (int)min(strcspn(value, " "), sizeof(tp_lockdowninfo) - 2),
				  value);
		}
	}
	of_node_put(chosen);
	ret = init_lct_tp_info(funcs->tp_info_buff);
	if (ret)
		return ret;
	ret = init_lct_tp_lockdown_info();
	if (ret)
		goto info;
	update_lct_tp_lockdown_info(funcs->tp_lockdown_info_buff ?
		funcs->tp_lockdown_info_buff : tp_lockdowninfo);
	set_lct_tp_info_callback(funcs->tp_info_cb);
	ret = init_lct_tp_work(funcs->tp_work_cb);
	if (ret)
		goto lockdown;
	ret = init_lct_tp_selftest(funcs->tp_selftest_cb);
	if (ret)
		goto work;
	ret = init_lct_tp_grip_area(funcs->set_screen_angle_cb,
				   funcs->get_screen_angle_cb);
	if (ret)
		goto selftest;
	ret = init_lct_tp_proximity_switch(funcs->tp_proximity_cb);
	if (ret)
		goto grip;
	ret = power_supply_reg_notifier(&lct_tp_usb_notifier);
	if (ret)
		goto proximity;
	usb_notifier_registered = true;
	return 0;

proximity:
	uninit_lct_tp_proximity_switch();
grip:
	uninit_lct_tp_grip_area();
selftest:
	uninit_lct_tp_selftest();
work:
	uninit_lct_tp_work();
lockdown:
	uninit_lct_tp_lockdown_info();
info:
	uninit_lct_tp_info();
	return ret;
}
EXPORT_SYMBOL_GPL(lct_tp_create_procfs);

void lct_remove_procfs(void)
{
	if (usb_notifier_registered) {
		power_supply_unreg_notifier(&lct_tp_usb_notifier);
		usb_notifier_registered = false;
	}
	WRITE_ONCE(g_touchscreen_usb_pulgin.valid, false);
	WRITE_ONCE(g_touchscreen_usb_pulgin.event_callback, NULL);
	tp_common_unregister_double_tap_ops();
	uninit_lct_tp_proximity_switch();
	uninit_lct_tp_grip_area();
	uninit_lct_tp_selftest();
	uninit_lct_tp_work();
	uninit_lct_tp_lockdown_info();
	uninit_lct_tp_info();
}
EXPORT_SYMBOL_GPL(lct_remove_procfs);
