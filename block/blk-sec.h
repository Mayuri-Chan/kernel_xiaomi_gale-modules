/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_SEC_H
#define BLK_SEC_H

static inline void blk_sec_stat_account_init(struct request_queue *q)
{
}

static inline void blk_sec_stat_account_exit(struct elevator_queue *eq)
{
}

static inline void blk_sec_stat_account_io_prepare(struct request *rq,
		void *ptr_pio)
{
}

static inline void blk_sec_stat_account_io_complete(struct request *rq,
		unsigned int data_size, void *pio)
{
}

static inline void blk_sec_stat_account_io_finish(struct request *rq,
		void *ptr_pio)
{
}

static inline int blk_sec_wb_ctrl(bool enable, int req_type)
{
	return 0;
}

static inline int blk_sec_wb_ctrl_async(bool enable, int req_type)
{
	return 0;
}

static inline bool blk_sec_wb_is_supported(struct gendisk *gd)
{
	return false;
}

#endif // BLK_SEC_H
