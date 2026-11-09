/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LCT_TP_COMMON_OPS_H
#define _LCT_TP_COMMON_OPS_H

#include <linux/kobject.h>

struct tp_common_ops {
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count);
};

int tp_common_set_double_tap_ops(struct tp_common_ops *ops);

#endif
