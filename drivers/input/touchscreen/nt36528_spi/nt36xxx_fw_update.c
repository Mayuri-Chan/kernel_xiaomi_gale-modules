/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * $Revision: 110851 $
 * $Date: 2022-12-27 17:56:41 +0800 (週二, 27 十二月 2022) $
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

#include <linux/firmware.h>
#include <linux/gpio.h>

#include "nt36xxx.h"

#if BOOT_UPDATE_FIRMWARE

static ktime_t start, end;
const struct firmware *fw_entry = NULL;
static size_t fw_need_write_size = 0;
static uint8_t *fwbuf = NULL;

struct nvt_ts_bin_map {
	char name[12];
	uint32_t BIN_addr;
	uint32_t SRAM_addr;
	uint32_t size;
	uint32_t crc;
};

static struct nvt_ts_bin_map *bin_map;

static int32_t nvt_get_fw_need_write_size(const struct firmware *fw_entry)
{
	int32_t i = 0;
	int32_t total_sectors_to_check = 0;

	total_sectors_to_check = fw_entry->size / FLASH_SECTOR_SIZE;

	for (i = total_sectors_to_check; i > 0; i--) {

		if (strncmp(&fw_entry->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), NVT end flag\n", fw_need_write_size, fw_need_write_size);
			return 0;
		}

		if (strncmp(&fw_entry->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "MOD", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), MOD end flag\n", fw_need_write_size, fw_need_write_size);
			return 0;
		}
	}

	NVT_ERR("end flag \"NVT\" \"MOD\" not found!\n");
	return -1;
}

static int32_t nvt_download_init(void)
{

	if (fwbuf == NULL) {
		fwbuf = (uint8_t *)kzalloc((NVT_TRANSFER_LEN + 1 + DUMMY_BYTES), GFP_KERNEL);
		if(fwbuf == NULL) {
			NVT_ERR("kzalloc for fwbuf failed!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static uint32_t CheckSum(const u8 *data, size_t len)
{
	uint32_t i = 0;
	uint32_t checksum = 0;

	for (i = 0 ; i < len+1 ; i++)
		checksum += data[i];

	checksum += len;
	checksum = ~checksum +1;

	return checksum;
}

static uint32_t byte_to_word(const uint8_t *data)
{
	return data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
}

static uint32_t partition = 0;
static uint8_t ilm_dlm_num = 2;
static uint8_t cascade_2nd_header_info = 0;
static uint8_t spi_dma_div_cnt_val = 0;
static int32_t nvt_bin_header_parser(const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t pos = 0x00;
	uint32_t end = 0x00;
	uint8_t info_sec_num = 0;
	uint8_t ovly_sec_num = 0;
	uint8_t ovly_info = 0;
	uint8_t find_bin_header = 0;

	end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);

	cascade_2nd_header_info = (fwdata[0x20] & 0x02) >> 1;
	NVT_LOG("cascade_2nd_header_info = %d\n", cascade_2nd_header_info);

	if (cascade_2nd_header_info) {
		pos = 0x30;
		while (pos < (end / 2)) {
			info_sec_num ++;
			pos += 0x10;
		}

		info_sec_num = info_sec_num + 1;

		spi_dma_div_cnt_val = fwdata[0x29] & 0x01;
	} else {
		pos = 0x30;
		while (pos < end) {
			info_sec_num ++;
			pos += 0x10;
		}
	}

	ovly_info = (fwdata[0x28] & 0x10) >> 4;
	ovly_sec_num = (ovly_info) ? (fwdata[0x28] & 0x0F) : 0;

	partition = ilm_dlm_num + ovly_sec_num + info_sec_num;
	NVT_LOG("ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d\n",
			ovly_info, ilm_dlm_num, ovly_sec_num, info_sec_num, partition);

	bin_map = (struct nvt_ts_bin_map *)kzalloc((partition+1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);
	if(bin_map == NULL) {
		NVT_ERR("kzalloc for bin_map failed!\n");
		return -ENOMEM;
	}

	for (list = 0; list < partition; list++) {

		if (list < ilm_dlm_num) {
			bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list*12]);
			bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list*12]);
			bin_map[list].size = byte_to_word(&fwdata[8 + list*12]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[0x18 + list*4]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
							bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}
			if (list == 0)
				sprintf(bin_map[list].name, "ILM");
			else if (list == 1)
				sprintf(bin_map[list].name, "DLM");
		}

		if ((list >= ilm_dlm_num) && (list < (ilm_dlm_num + info_sec_num))) {
			if (find_bin_header == 0) {

				pos = 0x30 + (0x10 * (list - ilm_dlm_num));
			} else if (find_bin_header && cascade_2nd_header_info) {

				pos = end - 0x10;
			}

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos+4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos+12]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
							bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}

			if ((bin_map[list].BIN_addr < end) && (bin_map[list].size != 0)) {
				sprintf(bin_map[list].name, "Header");
				find_bin_header = 1;
			} else {
				sprintf(bin_map[list].name, "Info-%d", (list - ilm_dlm_num));
			}
		}

		if (list >= (ilm_dlm_num + info_sec_num)) {

			pos = bin_map[1].BIN_addr + (0x10 * (list- ilm_dlm_num - info_sec_num));

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos+4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos+12]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
							bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}
			sprintf(bin_map[list].name, "Overlay-%d", (list- ilm_dlm_num - info_sec_num));
		}

		if ((bin_map[list].BIN_addr + bin_map[list].size) > fwsize) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
					bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
			return -EINVAL;
		}

	}

	return 0;
}

static void update_firmware_release(void)
{
	if (fw_entry) {
		release_firmware(fw_entry);
	}

	fw_entry = NULL;
}

static int32_t update_firmware_request(char *filename)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	if (NULL == filename) {
		return -ENOENT;
	}

	while (1) {
		NVT_LOG("filename is %s\n", filename);

		ret = request_firmware(&fw_entry, filename, &ts->client->dev);
		if (ret) {
			NVT_ERR("firmware load failed, ret=%d\n", ret);
			goto request_fail;
		}

		if (nvt_get_fw_need_write_size(fw_entry)) {
			NVT_ERR("get fw need to write size fail!\n");
			ret = -EINVAL;
			goto invalid;
		}

		if (*(fw_entry->data + FW_BIN_VER_OFFSET) + *(fw_entry->data + FW_BIN_VER_BAR_OFFSET) != 0xFF) {
			NVT_ERR("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
			NVT_ERR("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n", *(fw_entry->data+FW_BIN_VER_OFFSET), *(fw_entry->data+FW_BIN_VER_BAR_OFFSET));
			ret = -ENOEXEC;
			goto invalid;
		}

		ret = nvt_bin_header_parser(fw_entry->data, fw_entry->size);
		if (ret) {
			NVT_ERR("bin header parser failed\n");
			goto invalid;
		} else {
			break;
		}

invalid:
		update_firmware_release();
		if (!IS_ERR_OR_NULL(bin_map)) {
			kfree(bin_map);
			bin_map = NULL;
		}

request_fail:
		retry++;
		if(unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			break;
		}
	}

	return ret;
}

static int32_t nvt_write_sram(const u8 *fwdata,
		uint32_t SRAM_addr, uint32_t size, uint32_t BIN_addr)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint16_t len = 0;
	int32_t count = 0;

	if (size % NVT_TRANSFER_LEN)
		count = (size / NVT_TRANSFER_LEN) + 1;
	else
		count = (size / NVT_TRANSFER_LEN);

	for (i = 0 ; i < count ; i++) {
		len = (size < NVT_TRANSFER_LEN) ? size : NVT_TRANSFER_LEN;

		ret = nvt_set_page(SRAM_addr);
		if (ret) {
			NVT_ERR("set page failed, ret = %d\n", ret);
			return ret;
		}

		fwbuf[0] = SRAM_addr & 0x7F;
		memcpy(fwbuf+1, &fwdata[BIN_addr], len);
		ret = CTP_SPI_WRITE(ts->client, fwbuf, len+1);
		if (ret) {
			NVT_ERR("write to sram failed, ret = %d\n", ret);
			return ret;
		}

		SRAM_addr += NVT_TRANSFER_LEN;
		BIN_addr += NVT_TRANSFER_LEN;
		size -= NVT_TRANSFER_LEN;
	}

	return ret;
}

static int32_t nvt_write_firmware(const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t BIN_addr, SRAM_addr, size;
	int32_t ret = 0;

	memset(fwbuf, 0, (NVT_TRANSFER_LEN+1));

	for (list = 0; list < partition; list++) {

		SRAM_addr = bin_map[list].SRAM_addr;
		size = bin_map[list].size;
		BIN_addr = bin_map[list].BIN_addr;

		if ((BIN_addr + size) > fwsize) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
					BIN_addr, BIN_addr + size);
			ret = -EINVAL;
			goto out;
		}

		if (!size)
			continue;
		else
			size = size +1;

		ret = nvt_write_sram(fwdata, SRAM_addr, size, BIN_addr);
		if (ret) {
			NVT_ERR("sram program failed, ret = %d\n", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int32_t nvt_check_fw_checksum(void)
{
	uint32_t fw_checksum = 0;
	uint32_t len = partition*4;
	uint32_t list = 0;
	int32_t ret = 0;

	memset(fwbuf, 0, (len+1));

	nvt_set_page(ts->mmap->R_ILM_CHECKSUM_ADDR);

	fwbuf[0] = (ts->mmap->R_ILM_CHECKSUM_ADDR) & 0x7F;
	ret = CTP_SPI_READ(ts->client, fwbuf, len+1);
	if (ret) {
		NVT_ERR("Read fw checksum failed\n");
		return ret;
	}

	for (list = 0; list < partition; list++) {
		fw_checksum = byte_to_word(&fwbuf[1+list*4]);

		if(!bin_map[list].size)
			continue;

		if (bin_map[list].crc != fw_checksum) {
			NVT_ERR("[%d] BIN_checksum=0x%08X, FW_checksum=0x%08X\n",
					list, bin_map[list].crc, fw_checksum);
			ret = -EIO;
		}
	}

	return ret;
}

static void nvt_set_bld_crc_bank(uint32_t DES_ADDR, uint32_t SRAM_ADDR,
		uint32_t LENGTH_ADDR, uint32_t size,
		uint32_t G_CHECKSUM_ADDR, uint32_t crc)
{

	nvt_set_page(DES_ADDR);
	fwbuf[0] = DES_ADDR & 0x7F;
	fwbuf[1] = (SRAM_ADDR) & 0xFF;
	fwbuf[2] = (SRAM_ADDR >> 8) & 0xFF;
	fwbuf[3] = (SRAM_ADDR >> 16) & 0xFF;
	CTP_SPI_WRITE(ts->client, fwbuf, 4);

	fwbuf[0] = LENGTH_ADDR & 0x7F;
	fwbuf[1] = (size) & 0xFF;
	fwbuf[2] = (size >> 8) & 0xFF;
	fwbuf[3] = (size >> 16) & 0xFF;
	if (ts->hw_crc == HWCRC_LEN_2Bytes) {
		CTP_SPI_WRITE(ts->client, fwbuf, 3);
	} else if (ts->hw_crc >= HWCRC_LEN_3Bytes) {
		CTP_SPI_WRITE(ts->client, fwbuf, 4);
	}

	fwbuf[0] = G_CHECKSUM_ADDR & 0x7F;
	fwbuf[1] = (crc) & 0xFF;
	fwbuf[2] = (crc >> 8) & 0xFF;
	fwbuf[3] = (crc >> 16) & 0xFF;
	fwbuf[4] = (crc >> 24) & 0xFF;
	CTP_SPI_WRITE(ts->client, fwbuf, 5);

	return;
}

static void nvt_set_bld_hw_crc(void)
{

	nvt_set_bld_crc_bank(ts->mmap->ILM_DES_ADDR, bin_map[0].SRAM_addr,
			ts->mmap->ILM_LENGTH_ADDR, bin_map[0].size,
			ts->mmap->G_ILM_CHECKSUM_ADDR, bin_map[0].crc);

	nvt_set_bld_crc_bank(ts->mmap->DLM_DES_ADDR, bin_map[1].SRAM_addr,
			ts->mmap->DLM_LENGTH_ADDR, bin_map[1].size,
			ts->mmap->G_DLM_CHECKSUM_ADDR, bin_map[1].crc);
}

static void nvt_read_bld_hw_crc(void)
{
	uint8_t buf[8] = {0};
	uint32_t g_crc = 0, r_crc = 0;

	nvt_set_page(ts->mmap->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = ts->mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(ts->client, buf, 2);
	NVT_ERR("crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d\n",
			(buf[1] >> 2) & 0x01, (buf[1] >> 0) & 0x01, (buf[1] >> 1) & 0x01);

	nvt_set_page(ts->mmap->G_ILM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->G_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(ts->mmap->R_ILM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->R_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	NVT_ERR("ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
			bin_map[0].crc, g_crc, r_crc);

	nvt_set_page(ts->mmap->G_DLM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->G_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(ts->mmap->R_DLM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->R_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	NVT_ERR("dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
			bin_map[1].crc, g_crc, r_crc);

	return;
}

static void nvt_spi_dma_setup(void)
{
	uint8_t buf[33] = {0};

	if (ts->mmap->SPI_DMA_VAL_ADDR) {
		nvt_set_page(bin_map[1].SRAM_addr);
		buf[0] = bin_map[1].SRAM_addr & 0x7F;
		CTP_SPI_WRITE(ts->client, buf, 33);

		nvt_set_page(ts->mmap->SPI_DMA_VAL_ADDR);
		buf[0] = ts->mmap->SPI_DMA_VAL_ADDR & 0x7F;
		buf[1] = 0x35;
		buf[2] = 0x32;
		buf[3] = 0xAA;
		buf[4] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 5);

		NVT_LOG("set spi dma val finish\n");
	} else {
		NVT_ERR("spi dma val addr is NULL\n");
	}
}

static int32_t nvt_download_firmware_hw_crc(void)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	start = ktime_get();

	while (1) {

		nvt_bootloader_reset();

		nvt_set_bld_hw_crc();

		if (cascade_2nd_header_info) {

			if (spi_dma_div_cnt_val) {
				nvt_spi_dma_setup();
			}

			nvt_tx_auto_copy_mode();

			ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
			if (ret) {
				NVT_ERR("Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}

			ret = nvt_wait_auto_copy();
			if (ret) {
				NVT_ERR("wait auto copy failed. (%d)\n", ret);
				goto fail;
			}
		} else {
			ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
			if (ret) {
				NVT_ERR("Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}
		}

		nvt_fw_crc_enable();

		nvt_boot_ready();

		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)\n", ret);
			goto fail;
		} else {
			break;
		}

fail:
		retry++;
		if(unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			nvt_read_bld_hw_crc();
			break;
		}
	}

	end = ktime_get();

	return ret;
}

static int32_t nvt_download_firmware(void)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	start = ktime_get();

	while (1) {

#if NVT_TOUCH_SUPPORT_HW_RST
		gpio_set_value(ts->reset_gpio, 0);
		mdelay(1);
#endif
		nvt_eng_reset();
#if NVT_TOUCH_SUPPORT_HW_RST
		gpio_set_value(ts->reset_gpio, 1);
		mdelay(10);
#endif
		nvt_bootloader_reset();

		nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);

		ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
		if (ret) {
			NVT_ERR("Write_Firmware failed. (%d)\n", ret);
			goto fail;
		}

		nvt_boot_ready();

		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)\n", ret);
			goto fail;
		}

		ret = nvt_check_fw_checksum();
		if (ret) {
			NVT_ERR("firmware checksum not match, retry=%d\n", retry);
			goto fail;
		} else {
			break;
		}

fail:
		retry++;
		if(unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			break;
		}
	}

	end = ktime_get();

	return ret;
}

int32_t nvt_update_firmware(char *firmware_name)
{
	int32_t ret = 0;

	ret = update_firmware_request(firmware_name);
	if (ret) {
		xiaomi_touch_mievent_report_str(0x365c07ea, 0, "TpFirmwareLoadFail", "novatek");
		NVT_ERR("update_firmware_request failed. (%d)\n", ret);
		goto request_firmware_fail;
	}

	ret = nvt_download_init();
	if (ret) {
		NVT_ERR("Download Init failed. (%d)\n", ret);
		goto download_fail;
	}

	if (ts->hw_crc)
		ret = nvt_download_firmware_hw_crc();
	else
		ret = nvt_download_firmware();
	if (ret) {
		xiaomi_touch_mievent_report_str(0x365c07ea, 0, "TpFirmwareLoadFail", "novatek");
		NVT_ERR("Download Firmware failed. (%d)\n", ret);
		goto download_fail;
	}

	NVT_LOG("Update firmware success! <%ld us>\n",
			(long) ktime_us_delta(end, start));

	ret = nvt_get_fw_info();
	if (ret) {
		NVT_ERR("nvt_get_fw_info failed. (%d)\n", ret);
	}

download_fail:
	if (!IS_ERR_OR_NULL(bin_map)) {
		kfree(bin_map);
		bin_map = NULL;
	}

	update_firmware_release();
request_firmware_fail:

	return ret;
}

void Boot_Update_Firmware(struct work_struct *work)
{
	mutex_lock(&ts->lock);
	nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	mutex_unlock(&ts->lock);
#if LCT_TP_USB_PLUGIN
	if (!IS_ERR_OR_NULL(g_touchscreen_usb_pulgin.event_callback))
		g_touchscreen_usb_pulgin.valid = true;
	if (g_touchscreen_usb_pulgin.valid && g_touchscreen_usb_pulgin.usb_plugged_in)
		g_touchscreen_usb_pulgin.event_callback();
#endif
}
#endif
