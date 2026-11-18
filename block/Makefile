# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel block layer
#
ccflags-y += -I$(srctree)/block/

ssg-$(CONFIG_MQ_IOSCHED_SSG)	:= ssg-iosched.o ssg-stat.o
ssg-$(CONFIG_MQ_IOSCHED_SSG_CGROUP)	+= ssg-cgroup.o
obj-$(CONFIG_MQ_IOSCHED_SSG)	+= ssg.o
