/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * $Revision: 119282 $
 * $Date: 2023-05-25 16:25:39 +0800 (週四, 25 五月 2023) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#ifndef 	_LINUX_NVT_TOUCH_H
#define		_LINUX_NVT_TOUCH_H

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "nt36xxx_mem_map.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define HAVE_VFS_WRITE
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
#define reinit_completion(x) INIT_COMPLETION(*(x))
#endif

#include "../lct_tp/lct_tp_common.h"
#include "../lct_tp/lct_tp_gesture.h"
#include "../xiaomi/xiaomi_touch.h"
#include "mtk_disp_notify.h"
#include "mi_disp_esd_check.h"

extern void set_lcd_reset_gpio_keep_high_1st(bool enable);
extern void set_prox_lcd_reset_gpio_keep_high1(bool enable);

#define NVT_DEBUG 1

#define NVTTOUCH_RST_PIN 980
#define NVTTOUCH_INT_PIN 943

#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING

#define BUS_TRANSFER_LENGTH  256

#if LCT_TP_DATA_DUMP_EN
#define SPI_TANSFER_LENGTH  256
#endif

#define NVT_SPI_NAME "NVT-ts-spi"

#if NVT_DEBUG
#define NVT_LOG(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#else
#define NVT_LOG(fmt, args...)    pr_info("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#endif
#define NVT_ERR(fmt, args...)    pr_err("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)

#define NVT_TS_NAME "NVTCapacitiveTouchScreen"
#define NVT_PEN_NAME "NVTCapacitivePen"

#define TOUCH_MAX_WIDTH 720
#define TOUCH_MAX_HEIGHT 1600
#define PEN_MAX_WIDTH 1440
#define PEN_MAX_HEIGHT 3200
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0
extern bool nvt_gesture_flag;
#if TOUCH_KEY_NUM > 0
extern const uint16_t touch_key_array[TOUCH_KEY_NUM];
#endif
#define TOUCH_FORCE_NUM 1000

#define PEN_PRESSURE_MAX (4095)
#define PEN_DISTANCE_MAX (1)
#define PEN_TILT_MIN (-60)
#define PEN_TILT_MAX (60)

#define NVT_TOUCH_SUPPORT_HW_RST 0

#define NVT_TOUCH_PROC 1
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_MP 1
#define NVT_SAVE_TEST_DATA_IN_FILE 1
#define MT_PROTOCOL_B 1
#define WAKEUP_GESTURE 1
#define WAKEUP_OFF 4
#define WAKEUP_ON 5
#if WAKEUP_GESTURE
extern const uint16_t gesture_key_array[];
#endif
#define BOOT_UPDATE_FIRMWARE 1
#define BOOT_UPDATE_FIRMWARE_NAME "novatek_ts_hdl_fw.bin"
#define MP_UPDATE_FIRMWARE_NAME   "novatek_ts_hdl_mp.bin"
#define POINT_DATA_CHECKSUM 1
#define POINT_DATA_CHECKSUM_LEN 65
#define NVT_PM_WAIT_BUS_RESUME_COMPLETE 1

#define NVT_TOUCH_ESD_PROTECT 0
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500
#define NVT_TOUCH_WDT_RECOVERY 1

#define CHECK_PEN_DATA_CHECKSUM 0

#if BOOT_UPDATE_FIRMWARE
#define SIZE_4KB 4096
#define FLASH_SECTOR_SIZE SIZE_4KB
#define FW_BIN_VER_OFFSET (fw_need_write_size - SIZE_4KB)
#define FW_BIN_VER_BAR_OFFSET (FW_BIN_VER_OFFSET + 1)
#define NVT_FLASH_END_FLAG_LEN 3
#define NVT_FLASH_END_FLAG_ADDR (fw_need_write_size - NVT_FLASH_END_FLAG_LEN)
#endif

#define NVT_MTK_DRM_NOTIFY 1

struct nvt_ts_data {
	struct spi_device *client;
	struct input_dev *input_dev;
	struct delayed_work nvt_fwu_work;
	uint16_t addr;
	int8_t phys[32];
	struct notifier_block disp_notifier;
	uint8_t fw_ver;
	uint8_t x_num;
	uint8_t y_num;
	uint8_t max_touch_num;
	uint8_t max_button_num;
	uint32_t int_trigger_type;
	int32_t irq_gpio;
	uint32_t irq_flags;
	int32_t reset_gpio;
	uint32_t reset_flags;
	struct mutex lock;
	const struct nvt_ts_mem_map *mmap;
	uint8_t hw_crc;
	uint8_t auto_copy;
	uint16_t nvt_pid;
	uint8_t *rbuf;
	uint8_t *xbuf;
	struct mutex xbuf_lock;
  	bool irq_enabled;
	bool irq_wake_enabled;
	bool common_initialized;
	bool gesture_initialized;
	bool disp_notifier_registered;
	bool gesture_wait_flag;
	bool gesture_wait_enable;
	bool palm_sensor_enable;
	struct workqueue_struct *resume_wq;
	struct work_struct resume_work;
	bool pen_support;
	uint8_t x_gang_num;
	uint8_t y_gang_num;
	struct input_dev *pen_input_dev;
	int8_t pen_phys[32];
	uint32_t chip_ver_trim_addr;
	uint32_t swrst_sif_addr;
	uint32_t crc_err_flag_addr;
#if NVT_PM_WAIT_BUS_RESUME_COMPLETE
	bool dev_pm_suspend;
	struct completion dev_pm_resume_completion;
#endif
};

#if NVT_TOUCH_PROC
struct nvt_flash_data{
	rwlock_t lock;
};
#endif

typedef enum {
	RESET_STATE_INIT = 0xA0,
	RESET_STATE_REK,
	RESET_STATE_REK_FINISH,
	RESET_STATE_NORMAL_RUN,
	RESET_STATE_MAX  = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
    EVENT_MAP_HOST_CMD                      = 0x50,
    EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE   = 0x51,
    EVENT_MAP_RESET_COMPLETE                = 0x60,
    EVENT_MAP_FWINFO                        = 0x78,
    EVENT_MAP_PROJECTID                     = 0x9A,
} SPI_EVENT_MAP;

#define SPI_WRITE_MASK(a)	(a | 0x80)
#define SPI_READ_MASK(a)	(a & 0x7F)

#define DUMMY_BYTES (1)
#define NVT_TRANSFER_LEN	(63*1024)
#define NVT_READ_LEN		(2*1024)
#define NVT_XBUF_LEN		(NVT_TRANSFER_LEN+1+DUMMY_BYTES)

typedef enum {
	NVTWRITE = 0,
	NVTREAD  = 1
} NVT_SPI_RW;

extern struct nvt_ts_data *ts;

int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len);
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len);
void nvt_bootloader_reset(void);
void nvt_eng_reset(void);
void nvt_sw_reset(void);
void nvt_sw_reset_idle(void);
void nvt_boot_ready(void);
void nvt_fw_crc_enable(void);
void nvt_tx_auto_copy_mode(void);
void nvt_read_fw_history(uint32_t fw_history_addr);
int32_t nvt_update_firmware(char *firmware_name);
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state);
int32_t nvt_get_fw_info(void);
int32_t nvt_clear_fw_status(void);
int32_t nvt_check_fw_status(void);
int32_t nvt_set_page(uint32_t addr);
int32_t nvt_wait_auto_copy(void);
int32_t nvt_write_addr(uint32_t addr, uint8_t data);
#if NVT_TOUCH_ESD_PROTECT
extern void nvt_esd_check_enable(uint8_t enable);
#endif

#ifdef LCT_TP_COMMON
extern int lct_nvt_tp_selftest_callback(unsigned char cmd);
#endif

extern int32_t nvt_set_pocket_palm_switch(uint8_t pocket_palm_switch);
extern int32_t nvt_set_proximity_power_saving_switch(uint8_t proximity_power_saving_switch);
#endif
