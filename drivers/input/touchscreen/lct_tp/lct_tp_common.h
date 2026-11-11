/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LCT_TP_COMMON_H
#define _LCT_TP_COMMON_H

#include <linux/types.h>
#include "lct_tp_info.h"
#include "lct_tp_work.h"
#include "lct_tp_selftest.h"
#include "lct_tp_grip_area.h"
#include "lct_tp_gesture.h"
#include "lct_tp_proximity_switch.h"

#define LCT_TP_COMMON
#define LCT_TP_DATA_DUMP_EN 1
#define LCT_TP_DATA_DUMP "tp_data_dump"
#define LCT_TP_INFO_EN 1
#define LCT_TP_WORK_EN 1
#define LCT_TP_SELFTEST_EN 1
#define LCT_TP_GRIP_AREA_EN 1
#define LCT_TP_GESTURE_EN 0
#define LCT_TP_PALM_EN 0
#define LCT_TP_USB_PLUGIN 1
#define LCT_TP_PROXIMITY_EN 1

typedef struct lct_tp_mifunc {
	char *tp_info_buff;
	char *tp_lockdown_info_buff;
	int (*tp_info_cb)(const char *);
	int (*tp_work_cb)(bool enable_tp);
	int (*tp_selftest_cb)(unsigned char cmd);
	int (*get_screen_angle_cb)(void);
	int (*set_screen_angle_cb)(int angle);
	int (*tp_gesture_cb)(bool enable_tp);
	int (*tp_palm_cb)(bool enable_tp);
	int (*tp_proximity_cb)(bool enable_tp);
} lct_tp_mifunc_t;

typedef struct touchscreen_usb_plugin_data {
	bool valid;
	bool usb_plugged_in;
	void (*event_callback)(void);
} touchscreen_usb_plugin_data_t;

extern char tp_lockdowninfo[24];
extern int tp_select_proximity;
extern touchscreen_usb_plugin_data_t g_touchscreen_usb_pulgin;
extern bool ps_send_touch_event_probe;
extern int (*tp_ps_send_touch_event)(int32_t data);

int lct_tp_create_procfs(lct_tp_mifunc_t *funcs);
void lct_remove_procfs(void);
bool lct_tp_panel_match(const char *name);
int ps_send_touch_event(int32_t data);

#define set_lct_tp_proximity_status set_lct_tp_proximity_switch_status
#define get_lct_tp_proximity_status get_lct_tp_proximity_switch_status

#endif
