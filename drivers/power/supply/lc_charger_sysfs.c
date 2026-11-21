#include "charger_class.h"
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include "mtk_charger.h"
#include "tcpm.h"
#include "mtk_disp_notify.h"

struct lc_charger {
	struct mtk_charger *manager;
	struct power_supply *primary;
	struct power_supply *master;
	struct power_supply *battery;
	struct power_supply *usb;
	struct power_supply *bms;
	struct tcpc_device *tcpc;
	struct notifier_block psy_nb;
	struct notifier_block screen_nb;
	bool shutdown_delay;
	bool input_suspend;
	bool sw_cid_enable;
	bool screen_on;
	bool audio_on;
	bool shipmode_count_reset;
	int fake_temp;
	int mtbf_current;
	int quick_charge_type;
};

static struct lc_charger *lc;

static int lc_get_property(struct power_supply *psy,
			   enum power_supply_property property)
{
	union power_supply_propval value = {};

	if (!psy || power_supply_get_property(psy, property, &value))
		return 0;
	return value.intval;
}

static int lc_charge_type(void)
{
	int type = lc_get_property(lc->primary, POWER_SUPPLY_PROP_TYPE);
	int usb_type = lc_get_property(lc->primary, POWER_SUPPLY_PROP_USB_TYPE);

	if (charger_manager_pd_is_online())
		return 6;
	switch (type) {
	case POWER_SUPPLY_TYPE_USB:
		return usb_type == POWER_SUPPLY_USB_TYPE_DCP ? 6 : 1;
	case POWER_SUPPLY_TYPE_USB_DCP:
		return 4;
	case POWER_SUPPLY_TYPE_USB_CDP:
		return 2;
	case POWER_SUPPLY_TYPE_USB_ACA:
		return 5;
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return 0;
	default:
		return 3;
	}
}

static int lc_usb_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	int vbus = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		return 0;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = lc_get_property(lc->primary, POWER_SUPPLY_PROP_ONLINE);
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!lc->manager || !lc->manager->chg1_dev ||
		    charger_dev_get_vbus(lc->manager->chg1_dev, &vbus))
			return -ENODATA;
		val->intval = vbus;
		return 0;
	default:
		return -EINVAL;
	}
}

static int lc_bms_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	if (psp != POWER_SUPPLY_PROP_TEMP)
		return -EINVAL;
	val->intval = lc->fake_temp ? lc->fake_temp :
		lc_get_property(lc->battery, POWER_SUPPLY_PROP_TEMP);
	return 0;
}

static enum power_supply_property lc_usb_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property lc_bms_props[] = {
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc lc_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = lc_usb_props,
	.num_properties = ARRAY_SIZE(lc_usb_props),
	.get_property = lc_usb_get_property,
};

static const struct power_supply_desc lc_bms_desc = {
	.name = "bms",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = lc_bms_props,
	.num_properties = ARRAY_SIZE(lc_bms_props),
	.get_property = lc_bms_get_property,
};

#define LC_BOOL_ATTR(name) \
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, \
			   char *buf) \
{ \
	return sysfs_emit(buf, "%d\n", lc->name); \
} \
static ssize_t name##_store(struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	bool value; \
	int ret = kstrtobool(buf, &value); \
	if (ret) return ret; \
	lc->name = value; \
	return count; \
} \
static DEVICE_ATTR_RW(name)

LC_BOOL_ATTR(shipmode_count_reset);
LC_BOOL_ATTR(sw_cid_enable);

static int lc_update_typec_role(void)
{
	if (!lc->sw_cid_enable || !lc->tcpc)
		return 0;
	if (tcpm_inquire_typec_attach_state(lc->tcpc) != TYPEC_UNATTACHED)
		return 0;
	return tcpm_typec_change_role_postpone(lc->tcpc,
		lc->screen_on || lc->audio_on ? TYPEC_ROLE_TRY_SNK :
		TYPEC_ROLE_SNK, true);
}

static ssize_t screen_on_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->screen_on);
}
static ssize_t screen_on_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = kstrtobool(buf, &lc->screen_on);

	if (ret)
		return ret;
	ret = lc_update_typec_role();
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(screen_on);

static ssize_t audio_on_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->audio_on);
}
static ssize_t audio_on_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret = kstrtobool(buf, &lc->audio_on);

	if (ret)
		return ret;
	ret = lc_update_typec_role();
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(audio_on);

static ssize_t shutdown_delay_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->shutdown_delay);
}
static DEVICE_ATTR_RO(shutdown_delay);

static ssize_t input_suspend_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->input_suspend);
}

static ssize_t input_suspend_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	bool suspend;
	int ret = kstrtobool(buf, &suspend);

	if (ret)
		return ret;
	if (lc->manager && lc->manager->chg1_dev) {
		ret = charger_dev_enable(lc->manager->chg1_dev, !suspend);
		if (ret)
			return ret;
		if (suspend)
			charger_dev_set_input_current(lc->manager->chg1_dev, 100000);
	}
	lc->input_suspend = suspend;
	power_supply_changed(lc->battery);
	return count;
}
static DEVICE_ATTR_RW(input_suspend);

static ssize_t ship_mode_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->manager->ship_mode);
}
static ssize_t ship_mode_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	bool ship;
	int ret = kstrtobool(buf, &ship);

	if (ret)
		return ret;
	lc->manager->ship_mode = ship;
	return count;
}
static DEVICE_ATTR_RW(ship_mode);

static ssize_t chip_ok_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n",
		lc_get_property(lc->battery, POWER_SUPPLY_PROP_AUTHENTIC) != 0);
}
static DEVICE_ATTR_RO(chip_ok);

static ssize_t batt_id_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", 255);
}
static DEVICE_ATTR_RO(batt_id);

static ssize_t battery_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "C3U_5000mAh_18w\n");
}
static DEVICE_ATTR_RO(battery_type);

static ssize_t resistance_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Unknown\n");
}
static DEVICE_ATTR_RO(resistance_id);

static ssize_t model_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "C3U_5000mAh_18w\n");
}
static DEVICE_ATTR_RO(model_name);

static ssize_t fake_temp_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->fake_temp);
}
static ssize_t fake_temp_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &lc->fake_temp);

	if (ret)
		return ret;
	power_supply_changed(lc->bms);
	return count;
}
static DEVICE_ATTR_RW(fake_temp);

static ssize_t quick_charge_type_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->quick_charge_type);
}
static DEVICE_ATTR_RO(quick_charge_type);

static ssize_t real_type_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	static const char * const names[] = {
		"Unknown", "USB", "USB_CDP", "USB_FLOAT",
		"USB_DCP", "USB_HVDCP", "USB_PD"
	};
	int type = lc_charge_type();

	return sysfs_emit(buf, "%s\n", names[clamp(type, 0, 6)]);
}
static DEVICE_ATTR_RO(real_type);

static ssize_t typec_cc_orientation_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(typec_cc_orientation);

static ssize_t typec_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->tcpc ?
		tcpm_inquire_typec_attach_state(lc->tcpc) : 0);
}
static DEVICE_ATTR_RO(typec_mode);

static ssize_t usb_otg_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->tcpc &&
		tcpm_inquire_typec_attach_state(lc->tcpc) == TYPEC_ATTACHED_SRC);
}
static DEVICE_ATTR_RO(usb_otg);

static ssize_t apdo_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}
static DEVICE_ATTR_RO(apdo_max);

static ssize_t pd_verifed_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}
static DEVICE_ATTR_RO(pd_verifed);

static ssize_t mtbf_current_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", lc->mtbf_current);
}
static ssize_t mtbf_current_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &lc->mtbf_current);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(mtbf_current);

static struct attribute *lc_battery_attrs[] = {
	&dev_attr_shutdown_delay.attr,
	&dev_attr_input_suspend.attr,
	&dev_attr_chip_ok.attr,
	&dev_attr_batt_id.attr,
	&dev_attr_shipmode_count_reset.attr,
	&dev_attr_ship_mode.attr,
	&dev_attr_battery_type.attr,
	&dev_attr_resistance_id.attr,
	&dev_attr_model_name.attr,
	&dev_attr_fake_temp.attr,
	&dev_attr_sw_cid_enable.attr,
	&dev_attr_screen_on.attr,
	&dev_attr_audio_on.attr,
	&dev_attr_quick_charge_type.attr,
	&dev_attr_real_type.attr,
	&dev_attr_mtbf_current.attr,
	NULL,
};

static struct attribute *lc_usb_attrs[] = {
	&dev_attr_typec_cc_orientation.attr,
	&dev_attr_typec_mode.attr,
	&dev_attr_usb_otg.attr,
	&dev_attr_apdo_max.attr,
	&dev_attr_pd_verifed.attr,
	NULL,
};

static const struct attribute_group lc_battery_group = {
	.attrs = lc_battery_attrs,
};
static const struct attribute_group lc_usb_group = {
	.attrs = lc_usb_attrs,
};

static int lc_psy_notify(struct notifier_block *nb,
			 unsigned long event, void *data)
{
	int type;

	if (event != PSY_EVENT_PROP_CHANGED || !data)
		return NOTIFY_DONE;
	if (data != lc->primary && data != lc->master && data != lc->battery)
		return NOTIFY_DONE;
	type = lc_charge_type();
	lc->quick_charge_type = type == 5 || type == 6;
	if (lc->usb)
		power_supply_changed(lc->usb);
	return NOTIFY_OK;
}

static int lc_screen_notify(struct notifier_block *nb,
			    unsigned long event, void *data)
{
	if (!data)
		return NOTIFY_DONE;
	lc->screen_on = event == MTK_DISP_BLANK_UNBLANK;
	lc_update_typec_role();
	return NOTIFY_OK;
}

static int __init lc_charger_init(void)
{
	struct power_supply_config config = {};
	int ret;

	lc = kzalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc)
		return -ENOMEM;
	lc->primary = power_supply_get_by_name("primary_chg");
	lc->master = power_supply_get_by_name("mtk-master-charger");
	lc->battery = power_supply_get_by_name("battery");
	if (!lc->primary || !lc->master || !lc->battery) {
		ret = -EPROBE_DEFER;
		goto err_put;
	}
	lc->manager = power_supply_get_drvdata(lc->master);
	if (!lc->manager) {
		ret = -EINVAL;
		goto err_put;
	}
	lc->tcpc = tcpc_dev_get_by_name("type_c_port0");
	config.drv_data = lc;
	lc->usb = power_supply_register(&lc->manager->pdev->dev,
					&lc_usb_desc, &config);
	if (IS_ERR(lc->usb)) {
		ret = PTR_ERR(lc->usb);
		lc->usb = NULL;
		goto err_put;
	}
	lc->bms = power_supply_register(&lc->manager->pdev->dev,
					&lc_bms_desc, &config);
	if (IS_ERR(lc->bms)) {
		ret = PTR_ERR(lc->bms);
		lc->bms = NULL;
		goto err_usb;
	}
	ret = sysfs_create_group(&lc->battery->dev.kobj, &lc_battery_group);
	if (ret)
		goto err_bms;
	ret = sysfs_create_group(&lc->usb->dev.kobj, &lc_usb_group);
	if (ret)
		goto err_battery_group;
	lc->psy_nb.notifier_call = lc_psy_notify;
	lc->screen_nb.notifier_call = lc_screen_notify;
	ret = power_supply_reg_notifier(&lc->psy_nb);
	if (ret)
		goto err_usb_group;
	ret = mtk_disp_notifier_register("SW_CID", &lc->screen_nb);
	if (ret)
		goto err_psy_nb;
	return 0;

err_psy_nb:
	power_supply_unreg_notifier(&lc->psy_nb);
err_usb_group:
	sysfs_remove_group(&lc->usb->dev.kobj, &lc_usb_group);
err_battery_group:
	sysfs_remove_group(&lc->battery->dev.kobj, &lc_battery_group);
err_bms:
	power_supply_unregister(lc->bms);
err_usb:
	power_supply_unregister(lc->usb);
err_put:
	if (lc->battery)
		power_supply_put(lc->battery);
	if (lc->master)
		power_supply_put(lc->master);
	if (lc->primary)
		power_supply_put(lc->primary);
	kfree(lc);
	lc = NULL;
	return ret;
}

static void __exit lc_charger_exit(void)
{
	mtk_disp_notifier_unregister(&lc->screen_nb);
	power_supply_unreg_notifier(&lc->psy_nb);
	sysfs_remove_group(&lc->usb->dev.kobj, &lc_usb_group);
	sysfs_remove_group(&lc->battery->dev.kobj, &lc_battery_group);
	power_supply_unregister(lc->bms);
	power_supply_unregister(lc->usb);
	power_supply_put(lc->battery);
	power_supply_put(lc->master);
	power_supply_put(lc->primary);
	kfree(lc);
	lc = NULL;
}
module_init(lc_charger_init);
module_exit(lc_charger_exit);

MODULE_LICENSE("GPL");
