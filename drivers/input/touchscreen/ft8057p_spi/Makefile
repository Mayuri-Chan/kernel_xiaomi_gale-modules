# Makefile for the focaltech touchscreen drivers.


ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/

obj-$(CONFIG_TOUCHSCREEN_FT8057P_SPI) += ft8057p_spi.o
ft8057p_spi-y := focaltech_core.o \
		focaltech_ex_fun.o \
		focaltech_ex_mode.o \
		focaltech_gesture.o \
		focaltech_esdcheck.o \
		focaltech_point_report_check.o

ft8057p_spi-y += focaltech_flash_spi.o


ft8057p_spi-y += focaltech_spi_v2.o


ft8057p_spi-y += focaltech_proximity.o



ft8057p_spi-y += focaltech_test/focaltech_test.o
ft8057p_spi-y += focaltech_test/focaltech_test_ini.o
ft8057p_spi-y += focaltech_test/supported_ic/focaltech_test_ft8057p.o

ccflags-y += -I$(src)/../lct_tp
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
