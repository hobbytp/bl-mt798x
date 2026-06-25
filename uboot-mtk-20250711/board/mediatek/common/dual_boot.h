/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * A/B System implementation
 */

#ifndef _DUAL_BOOT_H_
#define _DUAL_BOOT_H_

#include <linux/types.h>

#define DUAL_BOOT_MAX_SLOTS			2

#define PART_FIRMWARE_NAME	"firmware"
#define PART_KERNEL_NAME	"kernel"
#define PART_ROOTFS_NAME	"rootfs"
#define PART_ROOTFS_DATA_NAME	"rootfs_data"

#define ENETLITE_AB_SCHEMA			1
#ifndef CONFIG_ENETLITE_AB_MAX_TRIES
#define CONFIG_ENETLITE_AB_MAX_TRIES		3
#endif

enum enetlite_ab_state_id {
	ENETLITE_AB_STABLE,
	ENETLITE_AB_INSTALLING,
	ENETLITE_AB_TRIAL,
};

enum enetlite_ab_operation {
	ENETLITE_AB_OP_NONE,
	ENETLITE_AB_OP_UPGRADE_OTHER,
	ENETLITE_AB_OP_UPGRADE_SAME,
	ENETLITE_AB_OP_MANUAL_SWITCH,
};

enum enetlite_ab_boot_reason {
	ENETLITE_AB_REASON_NORMAL,
	ENETLITE_AB_REASON_TRIAL,
	ENETLITE_AB_REASON_ROLLBACK,
	ENETLITE_AB_REASON_HARD_FALLBACK,
	ENETLITE_AB_REASON_RECOVERY,
	ENETLITE_AB_REASON_INVALID_STATE,
};

enum enetlite_ab_boot_path {
	ENETLITE_AB_BOOT_PATH_SLOT,
	ENETLITE_AB_BOOT_PATH_RECOVERY,
};

struct enetlite_ab_state {
	u32 schema;
	enum enetlite_ab_state_id state;
	u32 confirmed_slot;
	u32 good_mask;
	int target_slot;
	enum enetlite_ab_operation operation;
	int tries_left;
};

struct enetlite_ab_boot_decision {
	u32 actual_slot;
	u32 confirmed_slot;
	u32 good_mask;
	int target_slot;
	enum enetlite_ab_state_id state;
	enum enetlite_ab_operation operation;
	int tries_left;
	enum enetlite_ab_boot_reason reason;
	enum enetlite_ab_boot_path boot_path;
};

void dual_boot_disable(void);
int enetlite_ab_get_confirmed_slot(u32 *slot);
int enetlite_ab_get_inactive_slot(u32 *slot);

int dual_boot_set_defaults(void *fdt);

const char *enetlite_ab_state_name(enum enetlite_ab_state_id state);
const char *enetlite_ab_operation_name(enum enetlite_ab_operation op);
const char *enetlite_ab_boot_reason_name(enum enetlite_ab_boot_reason reason);
const char *enetlite_ab_boot_path_name(enum enetlite_ab_boot_path path);
int enetlite_ab_state_load(struct enetlite_ab_state *state);
int enetlite_ab_state_validate(const struct enetlite_ab_state *state);
int enetlite_ab_state_commit(const struct enetlite_ab_state *state);
int enetlite_ab_init(u32 confirmed_slot, u32 good_mask, bool force);
int enetlite_ab_begin_install(enum enetlite_ab_operation op, u32 actual_slot);
int enetlite_ab_activate_trial(u32 tries);
int enetlite_ab_mark_good(u32 actual_slot);
int enetlite_ab_complete_rollback(void);
int enetlite_ab_abort_install(void);
int enetlite_ab_cancel_trial(void);
int enetlite_ab_preflight_manual_trial(u32 target_slot);
int enetlite_ab_start_manual_trial(u32 target_slot);
const struct enetlite_ab_boot_decision *enetlite_ab_last_decision(void);

struct dual_boot_slot {
	const char *kernel;
	const char *rootfs;
	const char *rootfs_data;
};

extern const struct dual_boot_slot dual_boot_slots[DUAL_BOOT_MAX_SLOTS];

struct dual_boot_priv {
	int (*boot_slot)(struct dual_boot_priv *priv, u32 slot, bool do_boot);
};

int dual_boot(struct dual_boot_priv *priv, bool do_boot);

#endif /* _DUAL_BOOT_H_ */
