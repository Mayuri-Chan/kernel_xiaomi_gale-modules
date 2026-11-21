// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd. */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/regmap.h>
#include <linux/version.h>

#include "charger_class.h"
#include "mtk_charger.h"
#include "../../misc/mediatek/usb/usb20/mtk_musb.h"

static bool sc8989x_pd_is_online(void)
{
	struct power_supply *psy;
	struct mtk_charger *info;
	bool online = false;

	psy = power_supply_get_by_name("mtk-master-charger");
	if (!psy)
		return false;
	info = power_supply_get_drvdata(psy);
	if (info)
		online = info->ta_status[PD] == TA_ATTACH;
	power_supply_put(psy);
	return online;
}

enum sc8989x_part_id {
	BQ25890 = 0x03,
	SC8989X = 0x04,
};

enum sc8989x_charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,
	CHARGING_HOST,
	NONSTANDARD_CHARGER,
	STANDARD_CHARGER,
	APPLE_2_1A_CHARGER,
	APPLE_1_0A_CHARGER,
	APPLE_0_5A_CHARGER,
	WIRELESS_CHARGER,
	HVDCP_CHARGER,
	CHECK_HV,
};

#define SC8989X_DRV_VERSION "1.0.0_G"
#define DELAY_CHARGE_UI_MS 1000

enum sc8960x_part_no {
	SC89890H_PN_NUM = 0x04,
	SC89895_PN_NUM = 0x04,
	SC8950_PN_NUM = 0x02,
	SC89890W_PN_NUM = 0x07,
};

enum vindpm_track {
	SC8989X_TRACK_DIS,
	SC8989X_TRACK_200,
	SC8989X_TRACK_250,
	SC8989X_TRACK_300,
};

#define SC8989X_VINDPM_MAX 15300

#define SC8989X_REG7D 0x7D
#define SC8989X_KEY1 0x48
#define SC8989X_KEY2 0x54
#define SC8989X_KEY3 0x53
#define SC8989X_KEY4 0x38

#define SC8989X_ADC_EN 0x87
#define SC8989X_DPDM3 0x88
#define SC8989X_PRIVATE 0xF9

#define SC8989X_REG_14 0x14
#define SC8989X_RESET_MASK 0x80
#define SC8989X_RESET_SHIFT 7
#define SC8989X_RESET 1
#define SC8989X_ICO_OPTIMIZED_MASK 0x40
#define SC8989X_ICO_OPTIMIZED_SHIFT 6
#define SC8989X_PN_MASK 0x38
#define SC8989X_PN_SHIFT 3
#define SC8989X_TS_PROFILE_MASK 0x04
#define SC8989X_TS_PROFILE_SHIFT 2

#define BQ25890_ITERM_STEP 64
#define BQ25890_ITERM_OFFSET 64

#define BQ25890_ITC_STEP 64
#define BQ25890_ITC_OFFSET 64

#define BQ25890_VOTG_STEP 64
#define BQ25890_VOTG_OFFSET 4550;

enum sc8989x_vbus_stat {
	VBUS_STAT_NO_INPUT = 0,
	VBUS_STAT_SDP,
	VBUS_STAT_CDP,
	VBUS_STAT_DCP,
	VBUS_STAT_HVDCP,
	VBUS_STAT_UNKOWN,
	VBUS_STAT_NONSTAND,
	VBUS_STAT_OTG,
};

enum sc8989x_chg_stat {
	CHG_STAT_NOT_CHARGE = 0,
	CHG_STAT_PRE_CHARGE,
	CHG_STAT_FAST_CHARGE,
	CHG_STAT_CHARGE_DONE,
};

enum sc8989x_adc_channel {
	SC8989X_ADC_VBAT,
	SC8989X_ADC_VSYS,
	SC8989X_ADC_VBUS,
	SC8989X_ADC_ICC,
	SC8989X_ADC_IBUS,

};

enum {
	SC8989X_VBUSSTAT_NOINPUT = 0,
	SC8989X_VBUSSTAT_SDP,
	SC8989X_VBUSSTAT_CDP,
	SC8989X_VBUSSTAT_DCP,
	SC8989X_VBUSSTAT_HVDCP,
	SC8989X_VBUSSTAT_FLOAT,
	SC8989X_VBUSSTAT_NON_STD,
	SC8989X_VBUSSTAT_OTG,
};

enum sc8989x_fields {
	EN_HIZ,
	EN_ILIM,
	IINDPM,
	DP_DRIVE,
	DM_DRIVE,
	VINDPM_OS,
	CONV_START,
	CONV_RATE,
	BOOST_FRE,
	ICO_EN,
	HVDCP_EN,
	FORCE_DPDM,
	AUTO_DPDM_EN,
	FORCE_DSEL,
	WD_RST,
	OTG_CFG,
	CHG_CFG,
	VSYS_MIN,
	VBATMIN_SEL,
	EN_PUMPX,
	ICC,
	ITC,
	ITERM,
	CV,
	VBAT_LOW,
	VRECHG,
	EN_ITERM,
	STAT_DIS,
	TWD,
	EN_TIMER,
	TCHG,
	JEITA_ISET,
	BAT_COMP,
	VCLAMP,
	TJREG,
	FORCE_ICO,
	TMR2X_EN,
	BATFET_DIS,
	JETTA_VSET_WARM,
	BATFET_DLY,
	BATFET_RST_EN,
	PUMPX_UP,
	PUMPX_DN,
	V_OTG,
	PFM_OTG_DIS,
	IBOOST_LIM,
	VBUS_STAT,
	CHG_STAT,
	PG_STAT,
	VSYS_STAT,
	VBOOST_STAT,
	FORCE_VINDPM,
	VINDPM,
	ADC_VBAT,
	ADC_VSYS,
	VBUS_GD,
	ADC_VBUS,
	ADC_ICC,
	VINDPM_STAT,
	IINDPM_STAT,
	REG_RST,
	ICO_STAT,
	PN,
	NTC_PROFILE,
	DEV_VERSION,
	VBAT_REG_LSB,
	ADC_IBUS,
	DP3P3V_DM0V_EN,
	F_VINDPM_TRACK,
	F_MAX_FIELDS,
};

enum sc8989x_reg_range {
	SC8989X_IINDPM,
	SC8989X_ICHG,
	SC8989X_IBOOST,
	SC8989X_VBAT_REG,
	SC8989X_VINDPM,
	SC8989X_ITERM,
	SC8989X_VBAT,
	SC8989X_VSYS,
	SC8989X_VBUS,
	SC8989X_ICC,
	SC8989X_IBUS,
	SC8989X_VOTG,
	SC8989X_ITC,

	BQ2589X_ICHG,
	BQ2589X_ITERM,
	BQ2589X_VOTG,
	BQ2589X_ITC,
};

struct reg_range {
	u32 min;
	u32 max;
	u32 step;
	u32 offset;
	const u32 *table;
	u16 num_table;
	bool round_up;
};

struct sc8989x_cfg_e {
	const char *chg_name;
	int ico_en;
	int hvdcp_en;
	int auto_dpdm_en;
	int vsys_min;
	int vbatmin_sel;
	int itrick;
	int iterm;
	int vbat_cv;
	int vbat_low;
	int vrechg;
	int en_term;
	int stat_dis;
	int wd_time;
	int en_timer;
	int charge_timer;
	int bat_comp;
	int vclamp;
	int votg;
	int iboost;
	int force_vindpm;
	int vindpm;

	int bq_iterm;
	int bq_itrick;
	int bq_votg;
};

static struct sc8989x_cfg_e sc8989x_default_cfg = {
	.chg_name = "primary_chg",
	.ico_en = 0,
	.hvdcp_en = 1,
	.auto_dpdm_en = 1,
	.vsys_min = 5,
	.vbatmin_sel = 0,
	.itrick = 3,
	.iterm = 4,
	.vbat_cv = 23,
	.vbat_low = 1,
	.vrechg = 0,
	.en_term = 1,
	.stat_dis = 0,
	.wd_time = 0,
	.en_timer = 1,
	.charge_timer = 2,
	.bat_comp = 0,
	.vclamp = 0,
	.votg = 12,
	.iboost = 7,
	.force_vindpm = 0,
	.vindpm = 20,
	.bq_iterm = 4,
	.bq_itrick = 3,
	.bq_votg = 8,
};

struct sc8989x_chip {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct charger_device *chg_dev;
	struct delayed_work icl_step_dwork;
	struct delayed_work chg_enable_dwork;
	bool stopping;
	bool irq_registered;

	int chg_type;

	int icl_goal;
	int bc12_done_flag;

	int psy_usb_type;

	int irq_gpio;
	int irq;

	int input_suspend;

	struct delayed_work force_detect_dwork;
	int force_detect_count;

	int power_good;
	int vbus_good;
	uint8_t dev_id;

	int part_no;
	struct power_supply_desc psy_desc;
	struct sc8989x_cfg_e *cfg;
	struct power_supply *psy;

	u32 delta_time;
	ktime_t usb_plug_in_time;
};

static const u32 sc8989x_iboost[] = {
	500, 750, 1200, 1400, 1650, 1875, 2150, 2450,
};

#define SC8989X_CHG_RANGE(_min, _max, _step, _offset, _ru)                     \
	{                                                                      \
		.min = _min, .max = _max, .step = _step, .offset = _offset,    \
		.round_up = _ru,                                               \
	}

#define SC8989X_CHG_RANGE_T(_table, _ru)                                       \
	{                                                                      \
		.table = _table, .num_table = ARRAY_SIZE(_table),              \
		.round_up = _ru,                                               \
	}

static const struct reg_range sc8989x_reg_range_ary[] = {
	[SC8989X_IINDPM] = SC8989X_CHG_RANGE(100, 3250, 50, 100, false),
	[SC8989X_ICHG] = SC8989X_CHG_RANGE(0, 5040, 60, 0, false),
	[SC8989X_ITERM] = SC8989X_CHG_RANGE(30, 930, 60, 30, false),
	[SC8989X_VBAT_REG] = SC8989X_CHG_RANGE(3840, 4848, 16, 3840, false),
	[SC8989X_VINDPM] = SC8989X_CHG_RANGE(3900, 15300, 100, 2600, false),
	[SC8989X_IBOOST] = SC8989X_CHG_RANGE_T(sc8989x_iboost, false),
	[SC8989X_VBAT] = SC8989X_CHG_RANGE(2304, 4848, 20, 2304, false),
	[SC8989X_VSYS] = SC8989X_CHG_RANGE(2304, 4848, 20, 2304, false),
	[SC8989X_VBUS] = SC8989X_CHG_RANGE(2600, 15300, 100, 2600, false),
	[SC8989X_ICC] = SC8989X_CHG_RANGE(0, 6350, 50, 0, false),
	[SC8989X_IBUS] = SC8989X_CHG_RANGE(0, 6350, 50, 0, false),
	[SC8989X_VOTG] = SC8989X_CHG_RANGE(3900, 5400, 100, 3900, false),
	[SC8989X_ITC] = SC8989X_CHG_RANGE(60, 960, 60, 60, false),

	[BQ2589X_ICHG] = SC8989X_CHG_RANGE(0, 5056, 64, 0, false),
	[BQ2589X_ITERM] = SC8989X_CHG_RANGE(64, 1024, 64, 64, false),
	[BQ2589X_VOTG] = SC8989X_CHG_RANGE(4550, 5510, 64, 4550, false),
	[BQ2589X_ITC] = SC8989X_CHG_RANGE(64, 1024, 64, 64, false),
};

static const struct reg_field sc8989x_reg_fields[] = {

	[EN_HIZ] = REG_FIELD(0x00, 7, 7),
	[EN_ILIM] = REG_FIELD(0x00, 6, 6),
	[IINDPM] = REG_FIELD(0x00, 0, 5),

	[DP_DRIVE] = REG_FIELD(0x01, 5, 7),
	[DM_DRIVE] = REG_FIELD(0x01, 2, 4),
	[VINDPM_OS] = REG_FIELD(0x01, 0, 0),

	[CONV_START] = REG_FIELD(0x02, 7, 7),
	[CONV_RATE] = REG_FIELD(0x02, 6, 6),
	[BOOST_FRE] = REG_FIELD(0x02, 5, 5),
	[ICO_EN] = REG_FIELD(0x02, 4, 4),
	[HVDCP_EN] = REG_FIELD(0x02, 3, 3),
	[FORCE_DPDM] = REG_FIELD(0x02, 1, 1),
	[AUTO_DPDM_EN] = REG_FIELD(0x02, 0, 0),

	[FORCE_DSEL] = REG_FIELD(0x03, 7, 7),
	[WD_RST] = REG_FIELD(0x03, 6, 6),
	[OTG_CFG] = REG_FIELD(0x03, 5, 5),
	[CHG_CFG] = REG_FIELD(0x03, 4, 4),
	[VSYS_MIN] = REG_FIELD(0x03, 1, 3),
	[VBATMIN_SEL] = REG_FIELD(0x03, 0, 0),

	[EN_PUMPX] = REG_FIELD(0x04, 7, 7),
	[ICC] = REG_FIELD(0x04, 0, 6),

	[ITC] = REG_FIELD(0x05, 4, 7),
	[ITERM] = REG_FIELD(0x05, 0, 3),

	[CV] = REG_FIELD(0x06, 2, 7),
	[VBAT_LOW] = REG_FIELD(0x06, 1, 1),
	[VRECHG] = REG_FIELD(0x06, 0, 0),

	[EN_ITERM] = REG_FIELD(0x07, 7, 7),
	[STAT_DIS] = REG_FIELD(0x07, 6, 6),
	[TWD] = REG_FIELD(0x07, 4, 5),
	[EN_TIMER] = REG_FIELD(0x07, 3, 3),
	[TCHG] = REG_FIELD(0x07, 1, 2),
	[JEITA_ISET] = REG_FIELD(0x07, 0, 0),

	[BAT_COMP] = REG_FIELD(0x08, 5, 7),
	[VCLAMP] = REG_FIELD(0x08, 2, 4),
	[TJREG] = REG_FIELD(0x08, 0, 1),

	[FORCE_ICO] = REG_FIELD(0x09, 7, 7),
	[TMR2X_EN] = REG_FIELD(0x09, 6, 6),
	[BATFET_DIS] = REG_FIELD(0x09, 5, 5),
	[JETTA_VSET_WARM] = REG_FIELD(0x09, 4, 4),
	[BATFET_DLY] = REG_FIELD(0x09, 3, 3),
	[BATFET_RST_EN] = REG_FIELD(0x09, 2, 2),
	[PUMPX_UP] = REG_FIELD(0x09, 1, 1),
	[PUMPX_DN] = REG_FIELD(0x09, 0, 0),

	[V_OTG] = REG_FIELD(0x0A, 4, 7),
	[PFM_OTG_DIS] = REG_FIELD(0x0A, 3, 3),
	[IBOOST_LIM] = REG_FIELD(0x0A, 0, 2),

	[VBUS_STAT] = REG_FIELD(0x0B, 5, 7),
	[CHG_STAT] = REG_FIELD(0x0B, 3, 4),
	[PG_STAT] = REG_FIELD(0x0B, 2, 2),
	[VSYS_STAT] = REG_FIELD(0x0B, 0, 0),

	[VBOOST_STAT] REG_FIELD(0X0C, 6, 6),

	[FORCE_VINDPM] = REG_FIELD(0x0D, 7, 7),
	[VINDPM] = REG_FIELD(0x0D, 0, 6),

	[ADC_VBAT] = REG_FIELD(0x0E, 0, 6),

	[ADC_VSYS] = REG_FIELD(0x0F, 0, 6),

	[VBUS_GD] = REG_FIELD(0x11, 7, 7),
	[ADC_VBUS] = REG_FIELD(0x11, 0, 6),

	[ADC_ICC] = REG_FIELD(0x12, 0, 6),

	[VINDPM_STAT] = REG_FIELD(0x13, 7, 7),
	[IINDPM_STAT] = REG_FIELD(0x13, 6, 6),

	[REG_RST] = REG_FIELD(0x14, 7, 7),
	[ICO_STAT] = REG_FIELD(0x14, 6, 6),
	[PN] = REG_FIELD(0x14, 3, 5),
	[NTC_PROFILE] = REG_FIELD(0x14, 2, 2),
	[DEV_VERSION] = REG_FIELD(0x14, 0, 1),

	[VBAT_REG_LSB] = REG_FIELD(0x40, 6, 6),

	[ADC_IBUS] = REG_FIELD(0x86, 1, 7),

	[DP3P3V_DM0V_EN] = REG_FIELD(0x83, 5, 5),

	[F_VINDPM_TRACK] = REG_FIELD(0x85, 1, 2),
};

static const struct regmap_config sc8989x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xFF,
};

static u8 val2reg(enum sc8989x_reg_range id, u32 val)
{
	int i;
	u8 reg;
	const struct reg_range *range = &sc8989x_reg_range_ary[id];

	if (!range)
		return val;

	if (range->table) {
		if (val <= range->table[0])
			return 0;
		for (i = 1; i < range->num_table - 1; i++) {
			if (val == range->table[i])
				return i;
			if (val > range->table[i] && val < range->table[i + 1])
				return range->round_up ? i + 1 : i;
		}
		return range->num_table - 1;
	}
	if (val <= range->min)
		reg = (range->min - range->offset) / range->step;
	else if (val >= range->max)
		reg = (range->max - range->offset) / range->step;
	else if (range->round_up)
		reg = (val - range->offset) / range->step + 1;
	else
		reg = (val - range->offset) / range->step;
	return reg;
}

static u32 reg2val(enum sc8989x_reg_range id, u8 reg)
{
	const struct reg_range *range = &sc8989x_reg_range_ary[id];
	if (!range)
		return reg;
	return range->table ? range->table[reg] :
			      range->offset + range->step * reg;
}

static int sc8989x_field_read(struct sc8989x_chip *sc,
			      enum sc8989x_fields field_id, int *val)
{
	int ret;

	ret = regmap_field_read(sc->rmap_fields[field_id], val);
	if (ret < 0) {
		dev_err(sc->dev, "sc8989x read field %d fail: %d\n", field_id,
			ret);
	}

	return ret;
}

static int sc8989x_field_write(struct sc8989x_chip *sc,
			       enum sc8989x_fields field_id, int val)
{
	int ret;

	ret = regmap_field_write(sc->rmap_fields[field_id], val);
	if (ret < 0) {
		dev_err(sc->dev, "sc8989x write field %d fail: %d\n", field_id,
			ret);
	}

	return ret;
}

static int sc8989x_set_key(struct sc8989x_chip *sc)
{
	regmap_write(sc->regmap, SC8989X_REG7D, SC8989X_KEY1);
	regmap_write(sc->regmap, SC8989X_REG7D, SC8989X_KEY2);
	regmap_write(sc->regmap, SC8989X_REG7D, SC8989X_KEY3);
	return regmap_write(sc->regmap, SC8989X_REG7D, SC8989X_KEY4);
}

static int sc8989x_set_wa(struct sc8989x_chip *sc)
{
	int ret;
	int val;

	ret = regmap_read(sc->regmap, SC8989X_DPDM3, &val);
	if (ret < 0) {
		sc8989x_set_key(sc);
	}

	regmap_write(sc->regmap, SC8989X_DPDM3, SC8989X_PRIVATE);

	return sc8989x_set_key(sc);
}

__maybe_unused static int sc8989x_set_vbat_lsb(struct sc8989x_chip *sc, bool en)
{
	int ret;
	int val;

	ret = sc8989x_field_read(sc, VBAT_REG_LSB, &val);
	if (ret < 0) {
		sc8989x_set_key(sc);
	}

	sc8989x_field_write(sc, VBAT_REG_LSB, en);

	return sc8989x_set_key(sc);

	return 0;
}

__maybe_unused static int sc8989x_adc_ibus_en(struct sc8989x_chip *sc, bool en)
{
	int ret;
	int val;

	ret = regmap_read(sc->regmap, SC8989X_ADC_EN, &val);
	if (ret < 0) {
		sc8989x_set_key(sc);
	}
	ret = regmap_read(sc->regmap, SC8989X_ADC_EN, &val);
	val = en ? val | BIT(2) : val & ~BIT(2);

	regmap_write(sc->regmap, SC8989X_ADC_EN, val);

	return sc8989x_set_key(sc);

	return 0;
}

__maybe_unused static int sc8989x_get_adc_ibus(struct sc8989x_chip *sc,
					       int *val)
{
	int ret;
	int reg = 0;

	if (sc->part_no != SC8989X) {
		*val = 0;
		return 0;
	}

	ret = sc8989x_field_read(sc, ADC_IBUS, &reg);
	if (ret < 0) {
		sc8989x_set_key(sc);
	}
	ret = sc8989x_field_read(sc, ADC_IBUS, &reg);
	if (ret < 0) {
		sc8989x_set_key(sc);
		return -1;
	}

	*val = reg2val(SC8989X_IBUS, (u8)reg);

	return sc8989x_set_key(sc);
}

__maybe_unused static int __sc8989x_get_adc(struct sc8989x_chip *sc,
					    enum sc8989x_adc_channel chan,
					    int *val)
{
	int reg_val, ret = 0;
	enum sc8989x_fields field_id;
	enum sc8989x_reg_range range_id;

	msleep(30);
	switch (chan) {
	case SC8989X_ADC_VBAT:
		field_id = ADC_VBAT;
		range_id = SC8989X_VBAT;
		break;
	case SC8989X_ADC_VSYS:
		field_id = ADC_VSYS;
		range_id = SC8989X_VSYS;
		break;
	case SC8989X_ADC_VBUS:
		field_id = ADC_VBUS;
		range_id = SC8989X_VBUS;
		break;
	case SC8989X_ADC_ICC:
		field_id = ADC_ICC;
		range_id = SC8989X_ICC;
		break;
	case SC8989X_ADC_IBUS:
		sc8989x_get_adc_ibus(sc, val);
		dev_info(sc->dev, "get_adc channel: %d val: %d", chan, *val);
		return 0;
	default:
		goto err;
	}

	ret = sc8989x_field_read(sc, field_id, &reg_val);
	if (ret < 0)
		goto err;
	*val = reg2val(range_id, reg_val);
	dev_info(sc->dev, "get_adc channel: %d val: %d", chan, *val);
	return 0;
err:
	dev_err(sc->dev, "get_adc fail channel: %d", chan);
	return -EINVAL;
}

static int __maybe_unused sc8989x_get_vendor_id(struct charger_device *chg_dev,
						u32 *vendor_id)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	if (sc == NULL) {
		pr_err("%s: sc8989x is null\n", __func__);
		return -EINVAL;
	}

	*vendor_id = sc->part_no;

	return 0;
}

static int sc8989x_get_ibus_adc(struct charger_device *chg_dev, u32 *ibus)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int value, ret;

	ret = __sc8989x_get_adc(sc, SC8989X_ADC_IBUS, &value);
	if (!ret)
		*ibus = value * 1000;
	return ret;
}

static int sc8989x_get_vbus_adc(struct charger_device *chg_dev, u32 *vbus)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int value, ret;

	ret = __sc8989x_get_adc(sc, SC8989X_ADC_VBUS, &value);
	if (!ret)
		*vbus = value * 1000;
	return ret;
}

__maybe_unused static int sc8989x_set_hiz(struct sc8989x_chip *sc, bool enable)
{
	int reg_val = enable ? 1 : 0;

	return sc8989x_field_write(sc, EN_HIZ, reg_val);
}

__maybe_unused static int sc8989x_check_chg_enabled(struct sc8989x_chip *sc,
						    bool *enable);

static void sc8989x_write_iindpm(struct sc8989x_chip *sc, int curr_ma)
{
	int reg_val = 0;
	if (sc->input_suspend) {
		curr_ma = 100;
	}
	reg_val = val2reg(SC8989X_IINDPM, curr_ma);

	sc8989x_field_write(sc, EN_ILIM, 0);
	sc8989x_field_write(sc, IINDPM, reg_val);
}

static int sc8989x_set_iindpm(struct sc8989x_chip *sc, int curr_ma)
{
	int ret = 0;

	if (sc->part_no == SC8989X) {
		if (READ_ONCE(sc->stopping) || !sc->vbus_good) {
			cancel_delayed_work(&sc->icl_step_dwork);
			sc8989x_write_iindpm(sc, curr_ma);
			dev_info(sc->dev, "sc vbus absent, set icl to %dmA",
				 curr_ma);
		} else {
			dev_info(sc->dev, "sc new icl:%dmA, last icl:%dmA",
				 curr_ma, sc->icl_goal);
			sc->icl_goal = curr_ma;
			if (!sc->bc12_done_flag)
				dev_info(sc->dev,
					 "sc bc1.2 ongoing, set iindpm skip");
			else if (sc->chg_type == STANDARD_HOST) {
				schedule_delayed_work(&sc->icl_step_dwork,
						      msecs_to_jiffies(100));
				dev_info(sc->dev, "sc sdp set new icl:%dmA",
					 curr_ma);
			} else {
				sc8989x_write_iindpm(sc, curr_ma);
				dev_info(sc->dev, "sc write icl:%dmA", curr_ma);
			}
		}
	} else {
		sc8989x_write_iindpm(sc, curr_ma);
		dev_info(sc->dev, "ti write icl to %dmA", curr_ma);
	}
	return ret;
}

static int sc8989x_get_iindpm(struct sc8989x_chip *sc, int *curr_ma)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, IINDPM, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read iindpm failed(%d)\n", ret);
		return ret;
	}

	*curr_ma = reg2val(SC8989X_IINDPM, reg_val);

	return ret;
}

static int sc8989x_set_dpdm_hvdcp_9v(struct sc8989x_chip *sc)
{
	sc8989x_field_write(sc, DP_DRIVE, 0X6);
	return sc8989x_field_write(sc, DM_DRIVE, 0X2);
}

static int sc8989x_set_dpdm_hiz(struct sc8989x_chip *sc)
{
	sc8989x_field_write(sc, DP_DRIVE, 0);
	return sc8989x_field_write(sc, DM_DRIVE, 0);
}

__maybe_unused static int sc8989x_reset_wdt(struct sc8989x_chip *sc)
{
	return sc8989x_field_write(sc, WD_RST, 1);
}

static int sc8989x_set_chg_enable(struct sc8989x_chip *sc, bool enable)
{
	int old_en, ret = 0;
	int reg_val = enable ? 1 : 0;

	sc8989x_field_read(sc, CHG_CFG, &old_en);
	old_en = !!old_en;
	if ((enable && old_en) || (!enable && !old_en)) {
		dev_info(sc->dev, "charge already %s",
			 old_en ? "enabled" : "disabled");
		return 0;
	}

	if (sc->part_no == SC8989X) {
		if (!reg_val) {
			dev_info(sc->dev, "disable charge now");
			ret = sc8989x_field_write(sc, CHG_CFG, 0);
			cancel_delayed_work(&sc->chg_enable_dwork);
			cancel_delayed_work(&sc->icl_step_dwork);
		} else {
			if (READ_ONCE(sc->stopping) || !sc->vbus_good) {
				cancel_delayed_work(&sc->chg_enable_dwork);
				cancel_delayed_work(&sc->icl_step_dwork);
				dev_info(sc->dev,
					 "vbus absent, enable charge skip");
			} else {
				if (!sc->bc12_done_flag)
					dev_info(
						sc->dev,
						"sc bc1.2 ongoing, enable charge skip");
				else if (sc->chg_type == STANDARD_HOST) {
					sc8989x_write_iindpm(sc, 100);
					schedule_delayed_work(
						&sc->chg_enable_dwork,
						msecs_to_jiffies(100));
					schedule_delayed_work(
						&sc->icl_step_dwork,
						msecs_to_jiffies(200));
					dev_info(
						sc->dev,
						"sc set icl to 100mA before charge enable");
				} else {
					ret = sc8989x_field_write(sc, CHG_CFG,
								  1);
					dev_info(sc->dev,
						 "sc charge enable now");
				}
			}
		}
	} else {
		ret = sc8989x_field_write(sc, CHG_CFG, reg_val);
		dev_info(sc->dev, "ti charge %s now",
			 reg_val ? "enable" : "disable");
	}

	return ret;
}

__maybe_unused static int sc8989x_check_chg_enabled(struct sc8989x_chip *sc,
						    bool *enable)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, CHG_CFG, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read charge enable failed(%d)\n", ret);
		return ret;
	}
	*enable = !!reg_val;

	return ret;
}

__maybe_unused static int sc8989x_set_otg_enable(struct sc8989x_chip *sc,
						 bool enable)
{
	int reg_val = enable ? 1 : 0;

	int old_en;

	sc8989x_field_read(sc, OTG_CFG, &old_en);
	old_en = !!old_en;
	if ((enable && old_en) || (!enable && !old_en)) {
		dev_info(sc->dev, "otg already %s",
			 old_en ? "enabled" : "disabled");
		return 0;
	}

	return sc8989x_field_write(sc, OTG_CFG, reg_val);
}

__maybe_unused static int sc8989x_set_iboost(struct sc8989x_chip *sc,
					     int curr_ma)
{
	int reg_val = val2reg(SC8989X_IBOOST, curr_ma);

	return sc8989x_field_write(sc, IBOOST_LIM, reg_val);
}

static int sc8989x_set_ichg(struct sc8989x_chip *sc, int curr_ma)
{
	int reg_val = 0;

	if (sc->part_no == BQ25890) {
		reg_val = val2reg(BQ2589X_ICHG, curr_ma);
	} else {
		reg_val = val2reg(SC8989X_ICHG, curr_ma);
	}

	return sc8989x_field_write(sc, ICC, reg_val);
}

static int sc8989x_get_ichg(struct sc8989x_chip *sc, int *curr_ma)
{
	int ret, reg_val;
	ret = sc8989x_field_read(sc, ICC, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read ICC failed(%d)\n", ret);
		return ret;
	}

	if (sc->part_no == BQ25890) {
		*curr_ma = reg2val(BQ2589X_ICHG, reg_val);
	} else {
		*curr_ma = reg2val(SC8989X_ICHG, reg_val);
	}

	return ret;
}

static int sc8989x_set_term_curr(struct sc8989x_chip *sc, int curr_ma)
{
	int reg_val = 0;

	if (sc->part_no == BQ25890) {
		reg_val = val2reg(BQ2589X_ITERM, curr_ma);
	} else {
		reg_val = val2reg(SC8989X_ITERM, curr_ma);
	}

	return sc8989x_field_write(sc, ITERM, reg_val);
}

static int sc8989x_get_term_curr(struct sc8989x_chip *sc, int *curr_ma)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, ITERM, &reg_val);
	if (ret)
		return ret;

	if (sc->part_no == BQ25890) {
		*curr_ma = reg2val(BQ2589X_ITERM, reg_val);
	} else {
		*curr_ma = reg2val(SC8989X_ITERM, reg_val);
	}

	return ret;
}

static int sc8989x_set_trick_curr(struct sc8989x_chip *sc, int curr_ma)
{
	int reg_val = 0;

	if (sc->part_no == BQ25890) {
		reg_val = val2reg(BQ2589X_ITC, curr_ma);
	} else {
		reg_val = val2reg(SC8989X_ITC, curr_ma);
	}

	return sc8989x_field_write(sc, ITC, reg_val);
}

__maybe_unused static int sc8989x_get_trick_curr(struct sc8989x_chip *sc,
						 int *curr_ma)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, ITC, &reg_val);
	if (ret)
		return ret;

	if (sc->dev_id == BQ25890) {
		*curr_ma = reg2val(BQ2589X_ITC, reg_val);
	} else {
		*curr_ma = reg2val(SC8989X_ITC, reg_val);
	}

	return ret;
}

__maybe_unused static int sc8989x_set_safet_timer(struct sc8989x_chip *sc,
						  bool enable)
{
	int reg_val = enable ? 1 : 0;

	return sc8989x_field_write(sc, EN_TIMER, reg_val);
}

__maybe_unused static int sc8989x_check_safet_timer(struct sc8989x_chip *sc,
						    bool *enabled)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, EN_TIMER, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read ICEN_TIMERC failed(%d)\n", ret);
		return ret;
	}

	*enabled = reg_val ? true : false;

	return ret;
}

static int sc8989x_set_vbat(struct sc8989x_chip *sc, int volt_mv)
{
	int reg_val = val2reg(SC8989X_VBAT_REG, volt_mv);

	return sc8989x_field_write(sc, CV, reg_val);
}

static int sc8989x_get_vbat(struct sc8989x_chip *sc, int *volt_mv)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, CV, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read vbat reg failed(%d)\n", ret);
		return ret;
	}

	*volt_mv = reg2val(SC8989X_VBAT_REG, reg_val);

	return ret;
}

static int sc8989x_set_vindpm(struct sc8989x_chip *sc, int volt_mv)
{
	int vbus_val = 0;
	int reg_val = 0;
	int ret = 0;

	ret = __sc8989x_get_adc(sc, SC8989X_ADC_VBUS, &vbus_val);
	if (ret)
		return ret;
	pr_err("%s: vbus = %d mV\n", __func__, vbus_val);
	if (vbus_val < 6000) {
		if (sc->chg_type == STANDARD_CHARGER) {
			volt_mv = 4500;
		} else {
			volt_mv = 4600;
		}
	} else {
		if (volt_mv < 4500)
			volt_mv = 4500;
	}
	reg_val = val2reg(SC8989X_VINDPM, volt_mv);

	sc8989x_field_write(sc, FORCE_VINDPM, 1);

	return sc8989x_field_write(sc, VINDPM, reg_val);
}

static int sc8989x_get_vindpm(struct sc8989x_chip *sc, int *volt_mv)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, VINDPM, &reg_val);
	if (ret)
		return ret;

	*volt_mv = reg2val(SC8989X_VINDPM, reg_val);

	return ret;
}

static int sc8989x_force_dpdm(struct sc8989x_chip *sc)
{
	int ret;
	int val;

	if (!is_usb_rdy())
		return -EAGAIN;
	Charger_Detect_Init();
	dev_err(sc->dev, "sc8989x_force_dpdm\n");
	sc8989x_set_vindpm(sc, 4800);
	ret = regmap_read(sc->regmap, 0x02, &val);
	if (ret < 0) {
		return ret;
	}
	val |= 0x02;
	ret = regmap_write(sc->regmap, 0x02, val);

	sc->power_good = 0;

	sc->bc12_done_flag = 0;

	return ret;
}

static int sc8989x_reset_dpdm(struct sc8989x_chip *sc)
{
	int ret;
	int val;

	dev_err(sc->dev, "sc8989x_reset_dpdm\n");
	ret = regmap_read(sc->regmap, 0x02, &val);
	if (ret < 0) {
		return ret;
	}
	if (val & 0x02) {
		val &= 0xfd;
		ret = regmap_write(sc->regmap, 0x02, val);
	}

	return ret;
}

static int sc8989x_get_charge_stat(struct sc8989x_chip *sc)
{
	int ret;
	int chg_stat, vbus_stat;

	ret = sc8989x_field_read(sc, CHG_STAT, &chg_stat);
	if (ret)
		return ret;

	ret = sc8989x_field_read(sc, VBUS_STAT, &vbus_stat);
	if (ret)
		return ret;

	if (vbus_stat == VBUS_STAT_OTG) {
		return POWER_SUPPLY_STATUS_DISCHARGING;
	} else if (chg_stat == CHG_STAT_CHARGE_DONE) {
		return POWER_SUPPLY_STATUS_FULL;
	} else if (sc->vbus_good) {
		return POWER_SUPPLY_STATUS_CHARGING;
	} else {
		switch (chg_stat) {
		case CHG_STAT_NOT_CHARGE:
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		case CHG_STAT_PRE_CHARGE:
		case CHG_STAT_FAST_CHARGE:
			return POWER_SUPPLY_STATUS_CHARGING;
		case CHG_STAT_CHARGE_DONE:
			return POWER_SUPPLY_STATUS_FULL;
		}
	}

	return POWER_SUPPLY_STATUS_UNKNOWN;
}

__maybe_unused static int sc8989x_check_charge_done(struct sc8989x_chip *sc,
						    bool *chg_done)
{
	int ret, reg_val;

	ret = sc8989x_field_read(sc, CHG_STAT, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read charge stat failed(%d)\n", ret);
		return ret;
	}

	*chg_done = (reg_val == CHG_STAT_CHARGE_DONE) ? true : false;
	return ret;
}
static DEFINE_MUTEX(sc8989x_i2c_lock);
static int sc8989x_read_byte(struct sc8989x_chip *sc, u8 *data, u8 reg)
{
	int ret;

	mutex_lock(&sc8989x_i2c_lock);
	ret = i2c_smbus_read_byte_data(sc->client, reg);
	if (ret < 0) {
		pr_info("failed to read 0x%.2x\n", reg);
		mutex_unlock(&sc8989x_i2c_lock);
		return ret;
	}

	*data = (u8)ret;
	mutex_unlock(&sc8989x_i2c_lock);

	return 0;
}
static int sc8989x_detect_device(struct sc8989x_chip *sc)
{
	int ret;
	u8 data;

	ret = sc8989x_read_byte(sc, &data, SC8989X_REG_14);
	if (ret == 0) {
		sc->part_no = (data & SC8989X_PN_MASK) >> SC8989X_PN_SHIFT;
	}

	return ret;
}

static int sc8989x_dump_register(struct sc8989x_chip *sc)
{
	int ret;
	int i;
	int val;

	for (i = 0; i <= 0x14; i++) {
		ret = regmap_read(sc->regmap, i, &val);
		if (ret < 0) {
			return ret;
		}
		dev_info(sc->dev, "%s REG%02x = 0x%02x\n", __func__, i, val);
	}

	return 0;
}

static int sc8989x_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s\n", __func__);

	ret = sc8989x_set_chg_enable(sc, true);
	if (ret) {
		dev_err(sc->dev, "Failed to enable charging:%d\n", ret);
	}

	return ret;
}

static int sc8989x_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s\n", __func__);

	ret = sc8989x_set_chg_enable(sc, false);
	if (ret) {
		dev_err(sc->dev, "Failed to disable charging:%d\n", ret);
	}
	return ret;
}

static int sc8989x_enable(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8989x_set_chg_enable(sc, en);
	if (!en && sc->input_suspend == 1) {
		sc8989x_write_iindpm(sc, 100);
	}

	dev_info(sc->dev, "%s charger %s\n", en ? "enable" : "disable",
		 !ret ? "successfully" : "failed");

	return ret;
}

static int sc8989x_is_enabled(struct charger_device *chg_dev, bool *enabled)
{
	int ret;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8989x_check_chg_enabled(sc, enabled);
	dev_info(sc->dev, "charger is %s\n",
		 *enabled ? "charging" : "not charging");

	return ret;
}

static int sc8989x_get_charging_current(struct charger_device *chg_dev,
					u32 *curr)
{
	int ret = 0;
	int curr_ma;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8989x_get_ichg(sc, &curr_ma);
	if (!ret) {
		*curr = curr_ma * 1000;
	}

	return ret;
}

static int sc8989x_set_charging_current(struct charger_device *chg_dev,
					u32 curr)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int sc_max_charge_current = 3550000;
	dev_info(sc->dev, "%s: charge curr = %duA\n", __func__, curr);
	if (sc->part_no == SC8989X && curr > sc_max_charge_current) {
		curr = sc_max_charge_current;
		dev_info(sc->dev, "%s Reset charge curr to %duA for sc8989x\n",
			 __func__, curr);
	}
	if (sc->input_suspend) {
		curr = 0;
	}

	return sc8989x_set_ichg(sc, curr / 1000);
}

static int sc8989x_get_input_current(struct charger_device *chg_dev, u32 *curr)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int curr_ma;
	int ret;

	dev_info(sc->dev, "%s\n", __func__);

	ret = sc8989x_get_iindpm(sc, &curr_ma);
	if (!ret) {
		*curr = curr_ma * 1000;
	}

	return ret;
}

static int sc8989x_set_input_current(struct charger_device *chg_dev, u32 curr)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int max_input_curr_limit_uA = 2000000;

	if (sc->part_no == SC8989X && sc->bc12_done_flag == 0) {
		dev_info(sc->dev, "%s %duA, bc12 is doing, skip\n", __func__,
			 curr);
		return 0;
	}

	dev_info(sc->dev, "%s: iindpm curr = %duA\n", __func__, curr);
	if (curr > max_input_curr_limit_uA) {
		curr = max_input_curr_limit_uA;
		dev_info(sc->dev, "%s Reset input current to %duA\n", __func__,
			 curr);
	}
	return sc8989x_set_iindpm(sc, curr / 1000);
}

static int sc8989x_get_constant_voltage(struct charger_device *chg_dev,
					u32 *volt)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int volt_mv;
	int ret;

	dev_info(sc->dev, "%s\n", __func__);

	ret = sc8989x_get_vbat(sc, &volt_mv);
	if (!ret) {
		*volt = volt_mv * 1000;
	}

	return ret;
}

static int sc8989x_set_constant_voltage(struct charger_device *chg_dev,
					u32 volt)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s: charge volt = %duV\n", __func__, volt);

	return sc8989x_set_vbat(sc, volt / 1000);
}

static int sc8989x_kick_wdt(struct charger_device *chg_dev)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s\n", __func__);
	return sc8989x_reset_wdt(sc);
}

static int sc8989x_set_ivl(struct charger_device *chg_dev, u32 volt)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s: vindpm volt = %d\n", __func__, volt);

	return sc8989x_set_vindpm(sc, volt / 1000);
}

static int sc8989x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = sc8989x_check_charge_done(sc, done);

	dev_info(sc->dev, "%s: charge %s done\n", __func__,
		 *done ? "is" : "not");
	return ret;
}

static int sc8989x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	*curr = 60 * 1000;
	dev_err(sc->dev, "%s\n", __func__);
	return 0;
}

static int sc8989x_dump_registers(struct charger_device *chg_dev)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s\n", __func__);

	return sc8989x_dump_register(sc);
}

static int sc8989x_send_ta_current_pattern(struct charger_device *chg_dev,
					   bool is_increase)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int ret;
	int i;

	dev_info(sc->dev, "%s: %s\n", __func__,
		 is_increase ? "pumpx up" : "pumpx dn");

	ret = sc8989x_set_iindpm(sc, 100);
	if (ret)
		return ret;
	msleep(10);

	for (i = 0; i < 6; i++) {
		if (i < 3) {
			sc8989x_set_iindpm(sc, 800);
			is_increase ? msleep(100) : msleep(300);
		} else {
			sc8989x_set_iindpm(sc, 800);
			is_increase ? msleep(300) : msleep(100);
		}
		sc8989x_set_iindpm(sc, 100);
		msleep(100);
	}

	sc8989x_set_iindpm(sc, 800);
	msleep(500);

	sc8989x_set_iindpm(sc, 100);
	msleep(100);

	return sc8989x_set_iindpm(sc, 1500);
}

static int sc8989x_send_ta20_current_pattern(struct charger_device *chg_dev,
					     u32 uV)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	u8 val = 0;
	int i;

	if (uV < 5500000) {
		uV = 5500000;
	} else if (uV > 15000000) {
		uV = 15000000;
	}

	val = (uV - 5500000) / 500000;

	dev_info(sc->dev, "%s ta20 vol=%duV, val=%d\n", __func__, uV, val);

	sc8989x_set_iindpm(sc, 100);
	msleep(150);
	for (i = 4; i >= 0; i--) {
		sc8989x_set_iindpm(sc, 800);
		(val & (1 << i)) ? msleep(100) : msleep(50);
		sc8989x_set_iindpm(sc, 100);
		(val & (1 << i)) ? msleep(50) : msleep(100);
	}
	sc8989x_set_iindpm(sc, 800);
	msleep(150);
	sc8989x_set_iindpm(sc, 100);
	msleep(240);

	return sc8989x_set_iindpm(sc, 800);
}

static int sc8989x_set_ta20_reset(struct charger_device *chg_dev)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	int curr;
	int ret;

	ret = sc8989x_get_iindpm(sc, &curr);
	if (ret)
		return ret;
	ret = sc8989x_set_iindpm(sc, 100);
	if (ret)
		return ret;
	msleep(300);
	return sc8989x_set_iindpm(sc, curr);
}

static int sc8989x_get_adc(struct charger_device *chg_dev,
			   enum adc_channel chan, int *min, int *max)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
	enum sc8989x_adc_channel sc_chan;
	int ret;

	switch (chan) {
	case ADC_CHANNEL_VBAT:
		sc_chan = SC8989X_ADC_VBAT;
		break;
	case ADC_CHANNEL_VSYS:
		sc_chan = SC8989X_ADC_VSYS;
		break;
	case ADC_CHANNEL_VBUS:
		sc_chan = SC8989X_ADC_VBUS;
		break;
	case ADC_CHANNEL_IBUS:
		sc_chan = SC8989X_ADC_IBUS;
		break;
	default:
		return -95;
	}

	ret = __sc8989x_get_adc(sc, sc_chan, min);
	if (ret)
		return ret;
	*min = *min * 1000;
	*max = *min;
	return 0;
}

static int sc8989x_set_votg(struct sc8989x_chip *sc, int volt_mv);
static int sc8989x_set_otg(struct charger_device *chg_dev, bool enable)
{
	int ret;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	if (sc->part_no == BQ25890) {
		if (enable) {
			sc8989x_set_votg(sc, 4998);
			sc8989x_set_iboost(sc, 1400);
		} else {
			sc8989x_set_votg(sc, 4550);
			sc8989x_set_iboost(sc, 500);
			msleep(800);
		}
	}

	ret = sc8989x_set_otg_enable(sc, enable);
	ret |= sc8989x_set_chg_enable(sc, !enable);

	dev_info(sc->dev, "%s OTG %s\n", enable ? "enable" : "disable",
		 !ret ? "successfully" : "failed");

	return ret;
}

static int sc8989x_set_safety_timer(struct charger_device *chg_dev, bool enable)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s  %s\n", __func__, enable ? "enable" : "disable");

	return sc8989x_set_safet_timer(sc, enable);
}

static int sc8989x_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *enabled)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	return sc8989x_check_safet_timer(sc, enabled);
}

static int sc8989x_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s otg curr = %d\n", __func__, curr);
	return sc8989x_set_iboost(sc, curr / 1000);
}

static int sc8989x_set_votg(struct sc8989x_chip *sc, int volt_mv)
{
	int reg_val = 0;

	if (sc->part_no == BQ25890) {
		reg_val = val2reg(BQ2589X_VOTG, volt_mv);
	} else {
		reg_val = val2reg(SC8989X_VOTG, volt_mv);
	}

	return sc8989x_field_write(sc, V_OTG, reg_val);
}

__maybe_unused static int sc8989x_get_votg(struct sc8989x_chip *sc,
					   int *volt_mv)
{
	int ret, reg_val;
	ret = sc8989x_field_read(sc, V_OTG, &reg_val);
	if (ret) {
		dev_err(sc->dev, "read VOTG failed(%d)\n", ret);
		return ret;
	}

	if (sc->part_no == BQ25890) {
		*volt_mv = reg2val(BQ2589X_VOTG, reg_val);
	} else {
		*volt_mv = reg2val(SC8989X_VOTG, reg_val);
	}

	return ret;
}

static int sc8989x_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	power_supply_changed(sc->psy);
	return 0;
}

static int sc8989x_enable_hz(struct charger_device *chg_dev, bool enable)
{
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	dev_info(sc->dev, "%s %s\n", __func__, enable ? "enable" : "disable");

	return sc8989x_set_hiz(sc, enable);
}

static int sc8989x_enable_powerpath(struct charger_device *chg_dev, bool en)
{
	return 0;
#if 0
    struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);
    u32 mivr = (en ? sc->cfg->vindpm : SC8989X_VINDPM_MAX);
    int reg_val = 0;

    dev_info(sc->dev, "%s en = %d\n", __func__, en);

    reg_val = val2reg(SC8989X_VINDPM, mivr);
    sc8989x_field_write(sc, FORCE_VINDPM, true);
    return sc8989x_field_write(sc, VINDPM, reg_val);
#endif
}

static int sc8989x_is_powerpath_enabled(struct charger_device *chg_dev,
					bool *en)
{
	int ret;
	int val = 0;
	struct sc8989x_chip *sc = dev_get_drvdata(&chg_dev->dev);

	ret = sc8989x_field_read(sc, VINDPM, &val);
	if (!ret) {
		val = reg2val(SC8989X_VINDPM, (u8)val);
		*en = (val == SC8989X_VINDPM_MAX) ? false : true;
	}
	return ret;
}

static struct charger_ops sc8989x_chg_ops = {

	.plug_in = sc8989x_plug_in,
	.plug_out = sc8989x_plug_out,
	.enable = sc8989x_enable,
	.is_enabled = sc8989x_is_enabled,
	.get_charging_current = sc8989x_get_charging_current,
	.set_charging_current = sc8989x_set_charging_current,
	.get_input_current = sc8989x_get_input_current,
	.set_input_current = sc8989x_set_input_current,
	.get_constant_voltage = sc8989x_get_constant_voltage,
	.set_constant_voltage = sc8989x_set_constant_voltage,
	.kick_wdt = sc8989x_kick_wdt,
	.set_mivr = sc8989x_set_ivl,
	.is_charging_done = sc8989x_is_charging_done,
	.get_min_charging_current = sc8989x_get_min_ichg,
	.dump_registers = sc8989x_dump_registers,

	.enable_safety_timer = sc8989x_set_safety_timer,
	.is_safety_timer_enabled = sc8989x_is_safety_timer_enabled,

	.enable_powerpath = sc8989x_enable_powerpath,
	.is_powerpath_enabled = sc8989x_is_powerpath_enabled,

	.enable_otg = sc8989x_set_otg,
	.set_boost_current_limit = sc8989x_set_boost_ilmt,
	.enable_discharge = NULL,

	.send_ta_current_pattern = sc8989x_send_ta_current_pattern,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = sc8989x_send_ta20_current_pattern,
	.reset_ta = sc8989x_set_ta20_reset,
	.enable_cable_drop_comp = NULL,

	.get_adc = sc8989x_get_adc,
	.get_ibus_adc = sc8989x_get_ibus_adc,
	.get_vbus_adc = sc8989x_get_vbus_adc,

	.event = sc8989x_do_event,
	.enable_hz = sc8989x_enable_hz,

};

static const struct charger_properties sc8989x_chg_props = {
	.alias_name = "sc8989x_chg",
};

static void sc8989x_set_chg_type(struct sc8989x_chip *sc, int type)
{
	switch (type) {
	case VBUS_STAT_SDP:
		sc->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case VBUS_STAT_CDP:
		sc->psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case VBUS_STAT_NO_INPUT:
		sc->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case VBUS_STAT_HVDCP:
		sc->psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	default:
		sc->psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	}
	if (is_usb_rdy() && (type == VBUS_STAT_SDP || type == VBUS_STAT_CDP ||
			     type == VBUS_STAT_NO_INPUT || type == VBUS_STAT_UNKOWN ||
			     type == VBUS_STAT_NONSTAND))
		Charger_Detect_Release();
}

static void sc8989x_force_detection_dwork_handler(struct work_struct *work)
{
	int ret;
	struct sc8989x_chip *sc = container_of(work, struct sc8989x_chip,
					       force_detect_dwork.work);

	if (READ_ONCE(sc->stopping) || !sc->vbus_good) {
		dev_err(sc->dev, "%s: vbus not good\n", __func__);
		return;
	}

	ret = sc8989x_force_dpdm(sc);
	if (ret) {
		if (ret == -EAGAIN && !READ_ONCE(sc->stopping))
			schedule_delayed_work(&sc->force_detect_dwork,
					      msecs_to_jiffies(100));
		return;
	}

	sc->force_detect_count++;
}

static int sc8989x_get_charger_type(struct sc8989x_chip *sc)
{
	int ret;
	int reg_val = 0;
	static int old_reg_val = VBUS_STAT_NO_INPUT;
	int temp;

	ret = sc8989x_field_read(sc, VBUS_STAT, &reg_val);
	if (ret) {
		return ret;
	}

	if (sc->vbus_good == 0 && reg_val != VBUS_STAT_NO_INPUT) {
		msleep(100);

		ret = sc8989x_field_read(sc, VBUS_STAT, &reg_val);
		if (ret) {
			return ret;
		}
	}

	if (old_reg_val != VBUS_STAT_NO_INPUT &&
	    reg_val != VBUS_STAT_NO_INPUT && old_reg_val != reg_val) {
		msleep(50);
		temp = reg_val;
		ret = sc8989x_field_read(sc, VBUS_STAT, &reg_val);
		if (ret) {
			dev_err(sc->dev, "%s Read VBUS_STAT fail\n", __func__);
			reg_val = temp;
		}
		dev_info(sc->dev, "%s %d -> %d \n", __func__, temp, reg_val);
	}
	old_reg_val = reg_val;

	if (reg_val != VBUS_STAT_NO_INPUT && reg_val != VBUS_STAT_OTG) {
		sc->bc12_done_flag = 1;
	}

	switch (reg_val) {
	case VBUS_STAT_NO_INPUT:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sc->chg_type = CHARGER_UNKNOWN;
		break;
	case VBUS_STAT_SDP:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		sc->chg_type = STANDARD_HOST;
		break;
	case VBUS_STAT_CDP:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		sc->chg_type = CHARGING_HOST;
		break;
	case VBUS_STAT_DCP:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		sc->chg_type = STANDARD_CHARGER;
		break;
	case VBUS_STAT_HVDCP:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_ACA;
		sc->chg_type = HVDCP_CHARGER;
		sc8989x_set_dpdm_hvdcp_9v(sc);
		break;
	case VBUS_STAT_UNKOWN:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		sc->chg_type = NONSTANDARD_CHARGER;

		{
			if (sc->force_detect_count < 16) {
				schedule_delayed_work(&sc->force_detect_dwork,
						      msecs_to_jiffies(500));
			}
		}

		break;
	case VBUS_STAT_NONSTAND:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		sc->chg_type = NONSTANDARD_CHARGER;
		break;
	default:
		sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sc->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	}

	sc8989x_set_chg_type(sc, reg_val);

	dev_info(sc->dev, "%s vbus stat: 0x%02x\n", __func__, reg_val);

	return ret;
}

__maybe_unused static int sc8989x_set_vindpm_track(struct sc8989x_chip *sc,
						   enum vindpm_track track)
{
	int ret;
	int val;

	ret = sc8989x_field_read(sc, F_VINDPM_TRACK, &val);
	if (ret < 0) {
		sc8989x_set_key(sc);
	}
	sc8989x_field_write(sc, F_VINDPM_TRACK, track);

	return sc8989x_set_key(sc);
}

static void stat_adc_conversion(struct sc8989x_chip *sc, int enable)
{
	sc8989x_field_write(sc, CONV_START, enable);
	sc8989x_field_write(sc, CONV_RATE, enable);
}

static irqreturn_t sc8989x_irq_handler(int irq, void *data)
{
	int ret;
	int reg_val;
	bool prev_vbus_gd;
	int bus_ovp_stat;
	int vindpm_stat;
	int vbus_val;
	int vindpm_val;
	static int vindpm_retry = 0;
	struct sc8989x_chip *sc = (struct sc8989x_chip *)data;

	dev_info(sc->dev, "%s: sc8989x_irq_handler, msleep 10\n", __func__);

	msleep(10);

	ret = sc8989x_field_read(sc, VBUS_GD, &reg_val);
	if (ret) {
		return IRQ_HANDLED;
	}
	ret = sc8989x_field_read(sc, VBOOST_STAT, &bus_ovp_stat);
	if (ret) {
		return IRQ_HANDLED;
	}

	ret = sc8989x_field_read(sc, VINDPM_STAT, &vindpm_stat);
	if (ret) {
		return IRQ_HANDLED;
	}

	ret = sc8989x_get_vindpm(sc, &vindpm_val);
	if (ret < 0) {
		return IRQ_HANDLED;
	}

	dev_info(sc->dev, "%s: reg_val = %d\n", __func__, reg_val);

	prev_vbus_gd = sc->vbus_good;
	sc->vbus_good = !!reg_val;

	dev_info(sc->dev, "%s: prev_vbus_gd = %d, sc->vbus_good = %d\n",
		 __func__, prev_vbus_gd, sc->vbus_good);

	if (!prev_vbus_gd && sc->vbus_good) {
		sc->force_detect_count = 0;
		stat_adc_conversion(sc, true);
		sc8989x_field_write(sc, EN_ILIM, false);
		if (sc->part_no == SC8989X) {
			ret = sc8989x_set_vindpm_track(sc, SC8989X_TRACK_300);
		}
		dev_info(sc->dev, "%s: adapter/usb inserted\n", __func__);

		ret = sc8989x_reset_dpdm(sc);
		if (ret) {
			dev_err(sc->dev, "%s: reset dpdm failed(%d)\n",
				__func__, ret);
		}
		schedule_delayed_work(&sc->force_detect_dwork,
				      msecs_to_jiffies(100));

		sc->usb_plug_in_time = ktime_get();

	} else if (prev_vbus_gd && !sc->vbus_good) {
		sc->bc12_done_flag = 0;
		cancel_delayed_work(&sc->chg_enable_dwork);
		cancel_delayed_work(&sc->icl_step_dwork);

		if (sc->part_no == BQ25890) {
			sc8989x_set_iindpm(sc, 100);
		} else {
			sc8989x_set_iindpm(sc, 250);
		}
		vindpm_retry = 0;
		dev_info(sc->dev, "%s: adapter/usb removed\n", __func__);
		stat_adc_conversion(sc, false);
		sc8989x_set_dpdm_hiz(sc);
		sc->force_detect_count = 0;
	}

	if (bus_ovp_stat) {
		if (sc->part_no == BQ25890) {
			pr_err("bus ovp happened,retry otg enable!\n");
			sc8989x_set_otg_enable(sc, false);
			sc8989x_set_chg_enable(sc, true);

			ret = sc8989x_set_iboost(sc, 1875);
			msleep(200);

			sc8989x_set_otg_enable(sc, true);
			sc8989x_set_chg_enable(sc, false);

			msleep(300);
			ret = sc8989x_set_iboost(sc, 1400);
		}
	}

	if (vindpm_stat) {
		ret = __sc8989x_get_adc(sc, SC8989X_ADC_VBUS, &vbus_val);
		if (sc->chg_type == HVDCP_CHARGER && vbus_val < 8000 &&
		    vindpm_val == 8300 && vindpm_retry < 2) {
			pr_err("%s: vbus = %d mV\n", __func__, vbus_val);
			ret = sc8989x_force_dpdm(sc);
			vindpm_retry++;
		}
	}

	sc8989x_get_charger_type(sc);
	power_supply_changed(sc->psy);

	sc8989x_dump_register(sc);
	return IRQ_HANDLED;
}

static int sc8989x_parse_dt(struct sc8989x_chip *sc)
{
	struct device_node *np = sc->dev->of_node;
	int i;
	int ret = 0;
	struct {
		char *name;
		int *conv_data;
	} props[] = {
		{ "sc,sc8989x,ico-en", &(sc->cfg->ico_en) },
		{ "sc,sc8989x,hvdcp-en", &(sc->cfg->hvdcp_en) },
		{ "sc,sc8989x,auto-dpdm-en", &(sc->cfg->auto_dpdm_en) },
		{ "sc,sc8989x,vsys-min", &(sc->cfg->vsys_min) },
		{ "sc,sc8989x,vbatmin-sel", &(sc->cfg->vbatmin_sel) },
		{ "sc,sc8989x,itrick", &(sc->cfg->itrick) },
		{ "sc,sc8989x,iterm", &(sc->cfg->iterm) },
		{ "sc,sc8989x,vbat-cv", &(sc->cfg->vbat_cv) },
		{ "sc,sc8989x,vbat-low", &(sc->cfg->vbat_low) },
		{ "sc,sc8989x,vrechg", &(sc->cfg->vrechg) },
		{ "sc,sc8989x,en-term", &(sc->cfg->en_term) },
		{ "sc,sc8989x,stat-dis", &(sc->cfg->stat_dis) },
		{ "sc,sc8989x,wd-time", &(sc->cfg->wd_time) },
		{ "sc,sc8989x,en-timer", &(sc->cfg->en_timer) },
		{ "sc,sc8989x,charge-timer", &(sc->cfg->charge_timer) },
		{ "sc,sc8989x,bat-comp", &(sc->cfg->bat_comp) },
		{ "sc,sc8989x,vclamp", &(sc->cfg->vclamp) },
		{ "sc,sc8989x,votg", &(sc->cfg->votg) },
		{ "sc,sc8989x,iboost", &(sc->cfg->iboost) },
		{ "sc,sc8989x,vindpm", &(sc->cfg->vindpm) },

		{ "bq,bq25890,votg", &(sc->cfg->bq_votg) },
		{ "bq,bq25890,iterm", &(sc->cfg->bq_iterm) },
		{ "bq,bq25890,itrick", &(sc->cfg->bq_itrick) },
	};

	sc->irq_gpio = of_get_named_gpio(np, "sc,intr-gpio", 0);
	if (sc->irq_gpio < 0)
		return sc->irq_gpio;

	if (of_property_read_string(np, "charger_name", &sc->cfg->chg_name) < 0)
		dev_err(sc->dev, "%s no charger name\n", __func__);

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name,
					   props[i].conv_data);
		if (ret < 0) {
			dev_err(sc->dev, "%s not find\n", props[i].name);
			continue;
		}
	}

	return 0;
}

static int sc8989x_init_device(struct sc8989x_chip *sc)
{
	int ret = 0;
	int i;
	int bq_cfg;

	struct {
		enum sc8989x_fields field_id;
		int conv_data;
	} props[] = {
		{ ICO_EN, sc->cfg->ico_en },
		{ HVDCP_EN, sc->cfg->hvdcp_en },
		{ AUTO_DPDM_EN, sc->cfg->auto_dpdm_en },
		{ VSYS_MIN, sc->cfg->vsys_min },
		{ VBATMIN_SEL, sc->cfg->vbatmin_sel },
		{ ITC, sc->cfg->itrick },
		{ ITERM, sc->cfg->iterm },
		{ CV, sc->cfg->vbat_cv },
		{ VBAT_LOW, sc->cfg->vbat_low },
		{ VRECHG, sc->cfg->vrechg },
		{ EN_ITERM, sc->cfg->en_term },
		{ STAT_DIS, sc->cfg->stat_dis },
		{ TWD, sc->cfg->wd_time },
		{ EN_TIMER, sc->cfg->en_timer },
		{ TCHG, sc->cfg->charge_timer },
		{ BAT_COMP, sc->cfg->bat_comp },
		{ VCLAMP, sc->cfg->vclamp },
		{ V_OTG, sc->cfg->votg },
		{ IBOOST_LIM, sc->cfg->iboost },
		{ VINDPM, sc->cfg->vindpm },
	};

	ret = sc8989x_field_write(sc, REG_RST, 1);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		dev_info(sc->dev, "%d--->%d\n", props[i].field_id,
			 props[i].conv_data);
		ret = sc8989x_field_write(sc, props[i].field_id,
					  props[i].conv_data);
		if (ret)
			return ret;
	}
	sc8989x_adc_ibus_en(sc, true);

	if (sc->part_no == BQ25890) {
		bq_cfg = (sc->cfg->bq_iterm * BQ25890_ITERM_STEP) +
			 BQ25890_ITERM_OFFSET;
		sc8989x_set_term_curr(sc, bq_cfg);

		bq_cfg = (sc->cfg->bq_itrick * BQ25890_ITC_STEP) +
			 BQ25890_ITC_OFFSET;
		sc8989x_set_trick_curr(sc, bq_cfg);

		bq_cfg = (sc->cfg->bq_votg * BQ25890_VOTG_STEP) +
			 BQ25890_VOTG_OFFSET;
		sc8989x_set_votg(sc, bq_cfg);
	}

	sc8989x_field_write(sc, BATFET_RST_EN, 0);

	if (sc->part_no == SC8989X) {
		sc8989x_set_wa(sc);
	}

	return sc8989x_dump_register(sc);
}

static int sc8989x_register_interrupt(struct sc8989x_chip *sc)
{
	int ret = 0;

	ret = devm_gpio_request(sc->dev, sc->irq_gpio, "chr-irq");
	if (ret < 0) {
		dev_err(sc->dev, "failed to request GPIO%d ; ret = %d",
			sc->irq_gpio, ret);
		return ret;
	}
	ret = gpio_direction_input(sc->irq_gpio);
	if (ret < 0) {
		dev_err(sc->dev, "failed to set GPIO%d ; ret = %d",
			sc->irq_gpio, ret);
		return ret;
	}
	sc->irq = gpio_to_irq(sc->irq_gpio);
	if (sc->irq < 0) {
		dev_err(sc->dev, "failed gpio to irq GPIO%d ; ret = %d",
			sc->irq_gpio, sc->irq);
		return sc->irq;
	}

	ret = devm_request_threaded_irq(sc->dev, sc->irq, NULL,
					sc8989x_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"chr_stat", sc);
	if (ret < 0) {
		dev_err(sc->dev, "request thread irq failed:%d\n", ret);
		return ret;
	} else {
		dev_err(sc->dev, "request thread irq pass:%d  sc->irq =%d\n",
			ret, sc->irq);
	}

	return 0;
}

static void determine_initial_status(struct sc8989x_chip *sc)
{
	sc8989x_irq_handler(sc->irq, (void *)sc);
}

static ssize_t sc8989x_show_registers(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct sc8989x_chip *sc = dev_get_drvdata(dev);
	uint8_t addr;
	unsigned int val;
	uint8_t tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8989x");
	for (addr = 0x00; addr <= 0x14; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc8989x_store_register(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct sc8989x_chip *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int val;
	unsigned int reg;
	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x14)
		regmap_write(sc->regmap, (unsigned char)reg,
			     (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc8989x_show_registers,
		   sc8989x_store_register);

static struct attribute *sc8989x_attrs[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group sc8989x_attr_group = {
	.attrs = sc8989x_attrs,
};

static enum power_supply_property sc8989x_chg_psy_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,

};

static enum power_supply_usb_type sc8989x_chg_psy_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_PD,
};

static int sc8989x_chg_property_is_writeable(struct power_supply *psy,
					     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static int sc8989x_chg_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct sc8989x_chip *sc = power_supply_get_drvdata(psy);
	int ret = 0;
	int data = 0;

	if (!sc)
		return -EINVAL;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		if (sc->part_no == SC8989X)
			val->strval = "SouthChip";
		else if (sc->part_no == BQ25890)
			val->strval = "TI";
		else
			val->strval = "Unknown";
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sc->vbus_good;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = sc8989x_get_charge_stat(sc);
		if (ret < 0)
			break;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sc8989x_get_ichg(sc, &data);
		if (ret)
			break;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sc8989x_get_vbat(sc, &data);
		if (ret < 0)
			break;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sc8989x_get_iindpm(sc, &data);
		if (ret < 0)
			break;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sc8989x_get_vindpm(sc, &data);
		if (ret < 0)
			break;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = sc8989x_get_term_curr(sc, &data);
		if (ret < 0)
			break;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		if (sc->vbus_good && sc8989x_pd_is_online() &&
		    (sc->psy_usb_type == POWER_SUPPLY_USB_TYPE_SDP ||
		     sc->psy_usb_type == POWER_SUPPLY_USB_TYPE_UNKNOWN))
			val->intval = POWER_SUPPLY_USB_TYPE_PD;
		else
			val->intval = sc->psy_usb_type;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (sc->psy_usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			val->intval = 500000;
		else
			val->intval = 1500000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (sc->psy_usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			val->intval = 5000000;
		else
			val->intval = 12000000;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = sc->psy_desc.type;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = __sc8989x_get_adc(sc, SC8989X_ADC_ICC, &val->intval);
		val->intval = val->intval * 1000;
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int sc8989x_chg_set_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    const union power_supply_propval *val)
{
	struct sc8989x_chip *sc = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		dev_info(sc->dev, "%s  %d\n", __func__, val->intval);
		if (val->intval == 2) {
			schedule_delayed_work(&sc->force_detect_dwork,
					      msecs_to_jiffies(300));
		} else if (val->intval == 4) {
			sc->psy_desc.type = POWER_SUPPLY_TYPE_USB;
			sc->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		}
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = sc8989x_set_chg_enable(sc, !!val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sc8989x_set_ichg(sc, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sc8989x_set_vbat(sc, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sc8989x_set_iindpm(sc, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sc8989x_set_vindpm(sc, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = sc8989x_set_term_curr(sc, val->intval / 1000);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static char *sc8989x_psy_supplied_to[] = {
	"battery",
	"mtk-master-charger",
};

static const struct power_supply_desc sc8989x_psy_desc = {
	.name = "primary_chg",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = sc8989x_chg_psy_usb_types,
	.num_usb_types = ARRAY_SIZE(sc8989x_chg_psy_usb_types),
	.properties = sc8989x_chg_psy_properties,
	.num_properties = ARRAY_SIZE(sc8989x_chg_psy_properties),
	.property_is_writeable = sc8989x_chg_property_is_writeable,
	.get_property = sc8989x_chg_get_property,
	.set_property = sc8989x_chg_set_property,
};

static int sc8989x_psy_register(struct sc8989x_chip *sc)
{
	struct power_supply_config cfg = {
		.drv_data = sc,
		.of_node = sc->dev->of_node,
		.supplied_to = sc8989x_psy_supplied_to,
		.num_supplicants = ARRAY_SIZE(sc8989x_psy_supplied_to),
	};

	memcpy(&sc->psy_desc, &sc8989x_psy_desc, sizeof(sc->psy_desc));
	sc->psy = devm_power_supply_register(sc->dev, &sc->psy_desc, &cfg);
	return IS_ERR(sc->psy) ? PTR_ERR(sc->psy) : 0;
}

static struct of_device_id sc8989x_of_device_id[] = {
	{
		.compatible = "southchip,sc89890h",
	},
	{
		.compatible = "southchip,sc89890w",
	},
	{
		.compatible = "southchip,sc89895",
	},
	{
		.compatible = "southchip,sc8950",
	},
	{},
};

MODULE_DEVICE_TABLE(of, sc8989x_of_device_id);

static void sc8989x_chg_enable_dwork_handler(struct work_struct *work)
{
	struct sc8989x_chip *sc =
		container_of(work, struct sc8989x_chip, chg_enable_dwork.work);

	if (READ_ONCE(sc->stopping))
		return;
	if (sc->vbus_good) {
		sc8989x_field_write(sc, CHG_CFG, 1);
		dev_info(sc->dev, "charge enable now");
	} else
		dev_info(sc->dev, "vbus not good, not enable charge");
}

static void sc8989x_icl_step_dwork_handler(struct work_struct *work)
{
	int iindpm_regval, iindpm_nowval;
	struct sc8989x_chip *sc =
		container_of(work, struct sc8989x_chip, icl_step_dwork.work);

	if (READ_ONCE(sc->stopping))
		return;
	if (READ_ONCE(sc->stopping) || !sc->vbus_good ||
	    sc->chg_type != STANDARD_HOST) {
		dev_info(sc->dev, "vbus drop out");
		return;
	}

	sc8989x_field_read(sc, IINDPM, &iindpm_regval);
	iindpm_nowval = val2reg(SC8989X_IINDPM, sc->icl_goal);
	if (iindpm_nowval == iindpm_regval)
		dev_info(sc->dev, "icl reached %dmA", sc->icl_goal);
	else if (iindpm_nowval > iindpm_regval) {
		iindpm_nowval = iindpm_regval + 1;
		sc8989x_field_write(sc, IINDPM, iindpm_nowval);
		schedule_delayed_work(&sc->icl_step_dwork,
				      msecs_to_jiffies(100));
		dev_info(sc->dev, "icl increase to %dmA", iindpm_nowval);
	} else {
		sc8989x_field_write(sc, IINDPM, iindpm_nowval);
		dev_info(sc->dev, "icl reduce to %dmA", iindpm_nowval);
	}
}

static void sc8989x_stop(void *data)
{
	struct sc8989x_chip *sc = data;

	if (READ_ONCE(sc->stopping))
		return;
	WRITE_ONCE(sc->stopping, true);
	if (sc->chg_dev) {
		charger_device_unregister(sc->chg_dev);
		sc->chg_dev = NULL;
	}
	cancel_delayed_work_sync(&sc->force_detect_dwork);
	cancel_delayed_work_sync(&sc->chg_enable_dwork);
	cancel_delayed_work_sync(&sc->icl_step_dwork);
	device_init_wakeup(sc->dev, false);
}

static int sc8989x_charger_probe(struct i2c_client *client)
{
	struct sc8989x_chip *sc;
	int ret, i;

	sc = devm_kzalloc(&client->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;
	sc->dev = &client->dev;
	sc->client = client;
	ret = sc8989x_detect_device(sc);
	if (ret)
		return dev_err_probe(sc->dev, ret,
				     "charger identification failed\n");
	if (sc->part_no != SC8989X && sc->part_no != BQ25890)
		return -ENODEV;
	sc->regmap = devm_regmap_init_i2c(client, &sc8989x_regmap_config);
	if (IS_ERR(sc->regmap))
		return PTR_ERR(sc->regmap);
	for (i = 0; i < ARRAY_SIZE(sc8989x_reg_fields); i++) {
		sc->rmap_fields[i] = devm_regmap_field_alloc(
			sc->dev, sc->regmap, sc8989x_reg_fields[i]);
		if (IS_ERR(sc->rmap_fields[i]))
			return PTR_ERR(sc->rmap_fields[i]);
	}
	sc->cfg = devm_kmemdup(sc->dev, &sc8989x_default_cfg,
			       sizeof(sc8989x_default_cfg), GFP_KERNEL);
	if (!sc->cfg)
		return -ENOMEM;
	INIT_DELAYED_WORK(&sc->force_detect_dwork,
			  sc8989x_force_detection_dwork_handler);
	INIT_DELAYED_WORK(&sc->chg_enable_dwork,
			  sc8989x_chg_enable_dwork_handler);
	INIT_DELAYED_WORK(&sc->icl_step_dwork, sc8989x_icl_step_dwork_handler);
	i2c_set_clientdata(client, sc);
	ret = sc8989x_parse_dt(sc);
	if (ret)
		return dev_err_probe(sc->dev, ret,
				     "charger resources unavailable\n");
	ret = sc8989x_init_device(sc);
	if (ret)
		return ret;
	ret = sc8989x_psy_register(sc);
	if (ret)
		return ret;
	sc->chg_dev =
		charger_device_register(sc->cfg->chg_name, sc->dev, sc,
					&sc8989x_chg_ops, &sc8989x_chg_props);
	if (IS_ERR(sc->chg_dev))
		return PTR_ERR(sc->chg_dev);
	ret = devm_add_action_or_reset(sc->dev, sc8989x_stop, sc);
	if (ret)
		return ret;
	ret = sc8989x_register_interrupt(sc);
	if (ret)
		return ret;
	sc->irq_registered = true;
	ret = device_init_wakeup(sc->dev, true);
	if (ret)
		return ret;
	ret = devm_device_add_group(sc->dev, &sc8989x_attr_group);
	if (ret)
		return ret;
	determine_initial_status(sc);
	dev_info(sc->dev, "SC8989x charger ready, part 0x%x\n", sc->part_no);
	return 0;
}

static void sc8989x_charger_shutdown(struct i2c_client *client)
{
	struct sc8989x_chip *sc = i2c_get_clientdata(client);

	if (sc->irq_registered)
		disable_irq(sc->irq);
	sc8989x_stop(sc);
}

#ifdef CONFIG_PM_SLEEP
static int sc8989x_suspend(struct device *dev)
{
	struct sc8989x_chip *sc = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev))
		disable_irq(sc->irq);

	return 0;
}

static int sc8989x_resume(struct device *dev)
{
	struct sc8989x_chip *sc = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	enable_irq(sc->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(sc->irq);

	return 0;
}

static const struct dev_pm_ops sc8989x_pm_ops = { SET_SYSTEM_SLEEP_PM_OPS(
	sc8989x_suspend, sc8989x_resume) };
#endif

static struct i2c_driver sc8989x_charger_driver = {
    .driver = {
            .name = "sc8989x",
            .owner = THIS_MODULE,
            .of_match_table = of_match_ptr(sc8989x_of_device_id),
#ifdef CONFIG_PM_SLEEP
            .pm = &sc8989x_pm_ops,
#endif
            },
    .probe = sc8989x_charger_probe,
    .shutdown = sc8989x_charger_shutdown,
};

module_i2c_driver(sc8989x_charger_driver);

MODULE_DESCRIPTION("SC SC8989X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip <Aiden-yu@southchip.com>");
