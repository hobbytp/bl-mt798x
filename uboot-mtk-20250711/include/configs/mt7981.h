/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Configuration for MediaTek MT7981 SoC
 *
 * Copyright (C) 2022 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#ifndef __MT7981_H
#define __MT7981_H

#include <linux/stringify.h>

/* Extra environment variables */
#ifdef CONFIG_MTK_DEFAULT_FIT_BOOT_CONF
#define FIT_BOOT_CONF_ENV	"bootconf=" CONFIG_MTK_DEFAULT_FIT_BOOT_CONF "\0"
#else
#define FIT_BOOT_CONF_ENV
#endif

#ifdef CONFIG_MTK_DUAL_BOOT_ROOTFS_DATA_SIZE
#ifndef DUAL_BOOT_ROOTFS_DATA_SIZE_ENV
#define DUAL_BOOT_ROOTFS_DATA_SIZE_ENV	"dual_boot.rootfs_data_size_mib"
#endif
#define DUAL_BOOT_ROOTFS_DATA_SIZE_ENV_SETTING	\
	DUAL_BOOT_ROOTFS_DATA_SIZE_ENV "=" \
	__stringify(CONFIG_MTK_DUAL_BOOT_ROOTFS_DATA_SIZE) "\0"
#else
#define DUAL_BOOT_ROOTFS_DATA_SIZE_ENV_SETTING
#endif

#define CFG_EXTRA_ENV_SETTINGS	\
	FIT_BOOT_CONF_ENV	\
	DUAL_BOOT_ROOTFS_DATA_SIZE_ENV_SETTING

#endif

