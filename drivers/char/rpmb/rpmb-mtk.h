/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef _MTK_RPMB_H_
#define _MTK_RPMB_H_

#include <linux/types.h>

/**********************************************************
 * Function Declaration                                   *
 **********************************************************/
struct mmc_host;

#if IS_ENABLED(CONFIG_RPMB)
int mmc_rpmb_register(struct mmc_host *mmc);
#else
//int mmc_rpmb_register(...);
#endif

#if IS_ENABLED(CONFIG_MICROTRUST_TEE_SUPPORT)
#if !IS_ENABLED(CONFIG_TRUSTONIC_TEE_SUPPORT)
#ifndef RPMB_MULTI_BLOCK_ACCESS
#define RPMB_MULTI_BLOCK_ACCESS 1
#endif
#ifndef MAX_RPMB_TRANSFER_BLK
#define MAX_RPMB_TRANSFER_BLK (16U)
#endif
#ifndef MAX_RPMB_REQUEST_SIZE
#define MAX_RPMB_REQUEST_SIZE (512U*MAX_RPMB_TRANSFER_BLK)
#endif
#endif

#define RPMB_IOCTL_SOTER_WRITE_DATA   85
#define RPMB_IOCTL_SOTER_READ_DATA    86
#define RPMB_IOCTL_SOTER_GET_CNT      87
#define RPMB_IOCTL_SOTER_GET_WR_SIZE  88
#define RPMB_IOCTL_SOTER_SET_KEY      89

struct mmc_card;
struct s_rpmb;
struct rpmb_dev;

struct rpmb_infor {
	unsigned int size;
	unsigned char *data_frame;
};

extern int ut_rpmb_req_read_data(struct mmc_card *card, struct s_rpmb *param, u32 blk_cnt);
extern int ut_rpmb_req_write_data(struct mmc_card *card, struct s_rpmb *param, u32 blk_cnt);
extern int ut_rpmb_req_set_key(struct mmc_card *card, struct s_rpmb *param);
extern int ut_rpmb_req_get_wc(struct mmc_card *card, unsigned int *wc);
extern int ut_rpmb_req_get_max_wr_size(struct mmc_card *card, unsigned int *max_wr_size);
extern int ut_rpmb_req_set_key_ufs(struct s_rpmb *param);
extern u16 rpmb_get_rw_size(struct rpmb_dev *rdev);
#endif

#endif
