// SPDX-License-Identifier: GPL-2.0
/*
 * Enetlite A/B boot state machine.
 */

#include <env.h>
#include <image.h>
#include <malloc.h>
#include <stdio.h>
#include <vsprintf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "dual_boot.h"
#include "boot_helper.h"
#include "rootdisk.h"

#define ENETLITE_AB_MAX_TRIES		CONFIG_ENETLITE_AB_MAX_TRIES

#define AB_ENV_SCHEMA			"dual_boot.schema"
#define AB_ENV_STATE			"dual_boot.state"
#define AB_ENV_CONFIRMED_SLOT		"dual_boot.confirmed_slot"
#define AB_ENV_GOOD_MASK		"dual_boot.good_mask"
#define AB_ENV_TARGET_SLOT		"dual_boot.target_slot"
#define AB_ENV_OPERATION		"dual_boot.operation"
#define AB_ENV_TRIES_LEFT		"dual_boot.tries_left"

static const char * const ab_env_keys[] = {
	AB_ENV_SCHEMA,
	AB_ENV_STATE,
	AB_ENV_CONFIRMED_SLOT,
	AB_ENV_GOOD_MASK,
	AB_ENV_TARGET_SLOT,
	AB_ENV_OPERATION,
	AB_ENV_TRIES_LEFT,
};

const struct dual_boot_slot dual_boot_slots[DUAL_BOOT_MAX_SLOTS] = {
	{
#ifdef CONFIG_MTK_DUAL_BOOT_ITB_IMAGE
		.kernel = PART_FIRMWARE_NAME,
#ifdef CONFIG_MTK_DUAL_BOOT_SLOT_1_ROOTFS_DATA_NAME
		.rootfs_data = PART_ROOTFS_DATA_NAME,
#endif
#else
		.kernel = PART_KERNEL_NAME,
		.rootfs = PART_ROOTFS_NAME,
#endif
	},
#ifdef CONFIG_MTK_DUAL_BOOT
	{
#ifdef CONFIG_MTK_DUAL_BOOT_ITB_IMAGE
		.kernel = CONFIG_MTK_DUAL_BOOT_SLOT_1_FIRMWARE_NAME,
#ifdef CONFIG_MTK_DUAL_BOOT_SLOT_1_ROOTFS_DATA_NAME
		.rootfs_data = CONFIG_MTK_DUAL_BOOT_SLOT_1_ROOTFS_DATA_NAME,
#endif
#else
		.kernel = CONFIG_MTK_DUAL_BOOT_SLOT_1_KERNEL_NAME,
		.rootfs = CONFIG_MTK_DUAL_BOOT_SLOT_1_ROOTFS_NAME,
#endif
	},
#endif
};

static bool dual_boot_disabled;
static struct enetlite_ab_boot_decision last_decision;
static bool last_decision_valid;
static bool manual_trial_first_boot;
static u32 manual_trial_first_boot_target;
static bool manual_trial_preflight;
static u32 manual_trial_preflight_target;

struct env_snapshot {
	const char *key;
	char *value;
};

void dual_boot_disable(void)
{
	dual_boot_disabled = true;
}

const char *enetlite_ab_state_name(enum enetlite_ab_state_id state)
{
	switch (state) {
	case ENETLITE_AB_STABLE:
		return "stable";
	case ENETLITE_AB_INSTALLING:
		return "installing";
	case ENETLITE_AB_TRIAL:
		return "trial";
	default:
		return "unknown";
	}
}

const char *enetlite_ab_operation_name(enum enetlite_ab_operation op)
{
	switch (op) {
	case ENETLITE_AB_OP_NONE:
		return "none";
	case ENETLITE_AB_OP_UPGRADE_OTHER:
		return "upgrade-other";
	case ENETLITE_AB_OP_UPGRADE_SAME:
		return "upgrade-same";
	case ENETLITE_AB_OP_MANUAL_SWITCH:
		return "manual-switch";
	default:
		return "unknown";
	}
}

const char *enetlite_ab_boot_reason_name(enum enetlite_ab_boot_reason reason)
{
	switch (reason) {
	case ENETLITE_AB_REASON_NORMAL:
		return "normal";
	case ENETLITE_AB_REASON_TRIAL:
		return "trial";
	case ENETLITE_AB_REASON_ROLLBACK:
		return "rollback";
	case ENETLITE_AB_REASON_HARD_FALLBACK:
		return "hard-fallback";
	case ENETLITE_AB_REASON_RECOVERY:
		return "recovery";
	case ENETLITE_AB_REASON_INVALID_STATE:
		return "invalid-state";
	default:
		return "unknown";
	}
}

const char *enetlite_ab_boot_path_name(enum enetlite_ab_boot_path path)
{
	switch (path) {
	case ENETLITE_AB_BOOT_PATH_SLOT:
		return "slot";
	case ENETLITE_AB_BOOT_PATH_RECOVERY:
		return "recovery";
	default:
		return "unknown";
	}
}

static int enetlite_ab_parse_state(const char *value,
				   enum enetlite_ab_state_id *state)
{
	if (!strcmp(value, "stable")) {
		*state = ENETLITE_AB_STABLE;
		return 0;
	}

	if (!strcmp(value, "installing")) {
		*state = ENETLITE_AB_INSTALLING;
		return 0;
	}

	if (!strcmp(value, "trial")) {
		*state = ENETLITE_AB_TRIAL;
		return 0;
	}

	return -EINVAL;
}

static int enetlite_ab_parse_operation(const char *value,
				       enum enetlite_ab_operation *op)
{
	if (!strcmp(value, "upgrade-other")) {
		*op = ENETLITE_AB_OP_UPGRADE_OTHER;
		return 0;
	}

	if (!strcmp(value, "upgrade-same")) {
		*op = ENETLITE_AB_OP_UPGRADE_SAME;
		return 0;
	}

	if (!strcmp(value, "manual-switch")) {
		*op = ENETLITE_AB_OP_MANUAL_SWITCH;
		return 0;
	}

	return -EINVAL;
}

static int enetlite_ab_parse_u32_env(const char *key, u32 *value)
{
	const char *raw = env_get(key);
	char *end;
	ulong parsed;

	if (!raw)
		return -ENOENT;

	parsed = simple_strtoul(raw, &end, 10);
	if (end == raw || *end)
		return -EINVAL;

	*value = parsed;
	return 0;
}

static int enetlite_ab_parse_int_env(const char *key, int *value)
{
	u32 parsed;
	int ret;

	ret = enetlite_ab_parse_u32_env(key, &parsed);
	if (ret)
		return ret;

	*value = parsed;
	return 0;
}

static int enetlite_ab_slot_bit(u32 slot)
{
	if (slot >= DUAL_BOOT_MAX_SLOTS)
		return 0;

	return 1U << slot;
}

static void enetlite_ab_set_last_decision(
	const struct enetlite_ab_state *state, u32 actual_slot,
	enum enetlite_ab_boot_reason reason)
{
	memset(&last_decision, 0, sizeof(last_decision));

	last_decision.actual_slot = actual_slot;
	last_decision.confirmed_slot = state->confirmed_slot;
	last_decision.good_mask = state->good_mask;
	last_decision.target_slot = state->target_slot;
	last_decision.state = state->state;
	last_decision.operation = state->operation;
	last_decision.tries_left = state->tries_left;
	last_decision.reason = reason;
	if (reason == ENETLITE_AB_REASON_RECOVERY ||
	    reason == ENETLITE_AB_REASON_INVALID_STATE)
		last_decision.boot_path = ENETLITE_AB_BOOT_PATH_RECOVERY;
	else
		last_decision.boot_path = ENETLITE_AB_BOOT_PATH_SLOT;

	if (reason == ENETLITE_AB_REASON_INVALID_STATE) {
		last_decision.confirmed_slot = 0;
		last_decision.good_mask = 0;
		last_decision.target_slot = -1;
		last_decision.operation = ENETLITE_AB_OP_NONE;
		last_decision.tries_left = -1;
	}
	last_decision_valid = true;
}

const struct enetlite_ab_boot_decision *enetlite_ab_last_decision(void)
{
	if (!last_decision_valid)
		return NULL;

	return &last_decision;
}

int enetlite_ab_state_validate(const struct enetlite_ab_state *state)
{
	u32 confirmed_bit;

	if (!state)
		return -EINVAL;

	if (state->schema != ENETLITE_AB_SCHEMA)
		return -EINVAL;

	if (state->confirmed_slot >= DUAL_BOOT_MAX_SLOTS)
		return -EINVAL;

	if (!state->good_mask || state->good_mask & ~0x3)
		return -EINVAL;

	confirmed_bit = enetlite_ab_slot_bit(state->confirmed_slot);
	if (!(state->good_mask & confirmed_bit))
		return -EINVAL;

	switch (state->state) {
	case ENETLITE_AB_STABLE:
		if (state->target_slot != -1 ||
		    state->operation != ENETLITE_AB_OP_NONE ||
		    state->tries_left != -1)
			return -EINVAL;
		return 0;
	case ENETLITE_AB_INSTALLING:
		if (state->target_slot < 0 ||
		    state->target_slot >= DUAL_BOOT_MAX_SLOTS ||
		    state->target_slot != 1 - state->confirmed_slot ||
		    state->tries_left != -1)
			return -EINVAL;

		if (state->operation != ENETLITE_AB_OP_UPGRADE_OTHER &&
		    state->operation != ENETLITE_AB_OP_UPGRADE_SAME)
			return -EINVAL;

		if (state->good_mask & enetlite_ab_slot_bit(state->target_slot))
			return -EINVAL;

		return 0;
	case ENETLITE_AB_TRIAL:
		if (state->target_slot < 0 ||
		    state->target_slot >= DUAL_BOOT_MAX_SLOTS ||
		    state->target_slot != 1 - state->confirmed_slot ||
		    state->tries_left < 0 ||
		    state->tries_left > ENETLITE_AB_MAX_TRIES)
			return -EINVAL;

		if (state->operation != ENETLITE_AB_OP_UPGRADE_OTHER &&
		    state->operation != ENETLITE_AB_OP_UPGRADE_SAME &&
		    state->operation != ENETLITE_AB_OP_MANUAL_SWITCH)
			return -EINVAL;

		if (state->good_mask & enetlite_ab_slot_bit(state->target_slot))
			return -EINVAL;

		return 0;
	default:
		return -EINVAL;
	}
}

int enetlite_ab_state_load(struct enetlite_ab_state *state)
{
	const char *raw;
	int ret;

	if (!state)
		return -EINVAL;

	memset(state, 0, sizeof(*state));
	state->target_slot = -1;
	state->operation = ENETLITE_AB_OP_NONE;
	state->tries_left = -1;

	ret = enetlite_ab_parse_u32_env(AB_ENV_SCHEMA, &state->schema);
	if (ret)
		return ret;

	raw = env_get(AB_ENV_STATE);
	if (!raw)
		return -ENOENT;

	ret = enetlite_ab_parse_state(raw, &state->state);
	if (ret)
		return ret;

	ret = enetlite_ab_parse_u32_env(AB_ENV_CONFIRMED_SLOT,
					&state->confirmed_slot);
	if (ret)
		return ret;

	ret = enetlite_ab_parse_u32_env(AB_ENV_GOOD_MASK, &state->good_mask);
	if (ret)
		return ret;

	raw = env_get(AB_ENV_TARGET_SLOT);
	if (raw) {
		ret = enetlite_ab_parse_int_env(AB_ENV_TARGET_SLOT,
						&state->target_slot);
		if (ret)
			return ret;
	}

	raw = env_get(AB_ENV_OPERATION);
	if (raw) {
		ret = enetlite_ab_parse_operation(raw, &state->operation);
		if (ret)
			return ret;
	}

	raw = env_get(AB_ENV_TRIES_LEFT);
	if (raw) {
		ret = enetlite_ab_parse_int_env(AB_ENV_TRIES_LEFT,
						&state->tries_left);
		if (ret)
			return ret;
	}

	return enetlite_ab_state_validate(state);
}

static void enetlite_ab_apply_state_to_env(const struct enetlite_ab_state *state)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ab_env_keys); i++)
		env_set(ab_env_keys[i], NULL);

	env_set_ulong(AB_ENV_SCHEMA, state->schema);
	env_set(AB_ENV_STATE, enetlite_ab_state_name(state->state));
	env_set_ulong(AB_ENV_CONFIRMED_SLOT, state->confirmed_slot);
	env_set_ulong(AB_ENV_GOOD_MASK, state->good_mask);

	if (state->target_slot >= 0)
		env_set_ulong(AB_ENV_TARGET_SLOT, state->target_slot);

	if (state->operation != ENETLITE_AB_OP_NONE)
		env_set(AB_ENV_OPERATION,
			enetlite_ab_operation_name(state->operation));

	if (state->tries_left >= 0)
		env_set_ulong(AB_ENV_TRIES_LEFT, state->tries_left);
}

static void enetlite_ab_snapshot_free(struct env_snapshot *snapshots,
				      size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(snapshots[i].value);
}

int enetlite_ab_state_commit(const struct enetlite_ab_state *state)
{
	struct env_snapshot snapshots[ARRAY_SIZE(ab_env_keys)];
	int ret;
	size_t i;

	ret = enetlite_ab_state_validate(state);
	if (ret)
		return ret;

	memset(snapshots, 0, sizeof(snapshots));

	for (i = 0; i < ARRAY_SIZE(ab_env_keys); i++) {
		const char *value;

		snapshots[i].key = ab_env_keys[i];
		value = env_get(ab_env_keys[i]);
		if (value) {
			snapshots[i].value = strdup(value);
			if (!snapshots[i].value) {
				enetlite_ab_snapshot_free(snapshots, i);
				return -ENOMEM;
			}
		}
	}

	enetlite_ab_apply_state_to_env(state);

	ret = env_save();
	if (ret) {
		for (i = 0; i < ARRAY_SIZE(ab_env_keys); i++)
			env_set(snapshots[i].key, snapshots[i].value);
	}

	enetlite_ab_snapshot_free(snapshots, ARRAY_SIZE(ab_env_keys));

	return ret;
}

int enetlite_ab_init(u32 confirmed_slot, u32 good_mask, bool force)
{
	struct enetlite_ab_state current, state;
	int ret;

	if (!force && !enetlite_ab_state_load(&current))
		return -EEXIST;

	memset(&state, 0, sizeof(state));
	state.schema = ENETLITE_AB_SCHEMA;
	state.state = ENETLITE_AB_STABLE;
	state.confirmed_slot = confirmed_slot;
	state.good_mask = good_mask;
	state.target_slot = -1;
	state.operation = ENETLITE_AB_OP_NONE;
	state.tries_left = -1;

	ret = enetlite_ab_state_validate(&state);
	if (ret)
		return ret;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_begin_install(enum enetlite_ab_operation op, u32 actual_slot)
{
	struct enetlite_ab_state state;
	u32 other;
	int ret;

	if (actual_slot >= DUAL_BOOT_MAX_SLOTS)
		return -EINVAL;

	other = 1 - actual_slot;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_STABLE ||
	    actual_slot != state.confirmed_slot ||
	    actual_slot >= DUAL_BOOT_MAX_SLOTS)
		return -EINVAL;

	if (op == ENETLITE_AB_OP_UPGRADE_OTHER) {
		state.target_slot = other;
	} else if (op == ENETLITE_AB_OP_UPGRADE_SAME) {
		if (!(state.good_mask & enetlite_ab_slot_bit(other)))
			return -ENODEV;

		state.target_slot = actual_slot;
		state.confirmed_slot = other;
	} else {
		return -EINVAL;
	}

	state.good_mask &= ~enetlite_ab_slot_bit(state.target_slot);
	state.state = ENETLITE_AB_INSTALLING;
	state.operation = op;
	state.tries_left = -1;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_activate_trial(u32 tries)
{
	struct enetlite_ab_state state;
	int ret;

	if (tries > ENETLITE_AB_MAX_TRIES)
		return -EINVAL;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_INSTALLING)
		return -EINVAL;

	state.state = ENETLITE_AB_TRIAL;
	state.tries_left = tries;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_mark_good(u32 actual_slot)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_TRIAL ||
	    actual_slot != (u32)state.target_slot)
		return -EINVAL;

	state.state = ENETLITE_AB_STABLE;
	state.confirmed_slot = actual_slot;
	state.good_mask |= enetlite_ab_slot_bit(actual_slot);
	state.target_slot = -1;
	state.operation = ENETLITE_AB_OP_NONE;
	state.tries_left = -1;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_complete_rollback(void)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_TRIAL)
		return -EINVAL;

	state.state = ENETLITE_AB_STABLE;
	state.target_slot = -1;
	state.operation = ENETLITE_AB_OP_NONE;
	state.tries_left = -1;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_abort_install(void)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_INSTALLING)
		return -EINVAL;

	state.state = ENETLITE_AB_STABLE;
	state.target_slot = -1;
	state.operation = ENETLITE_AB_OP_NONE;
	state.tries_left = -1;

	return enetlite_ab_state_commit(&state);
}

int enetlite_ab_cancel_trial(void)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	switch (state.state) {
	case ENETLITE_AB_STABLE:
		return -ENOENT;
	case ENETLITE_AB_INSTALLING:
		return -EBUSY;
	case ENETLITE_AB_TRIAL:
		state.state = ENETLITE_AB_STABLE;
		state.target_slot = -1;
		state.operation = ENETLITE_AB_OP_NONE;
		state.tries_left = -1;
		return enetlite_ab_state_commit(&state);
	default:
		return -EINVAL;
	}
}

int enetlite_ab_preflight_manual_trial(u32 target_slot)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_STABLE ||
	    target_slot >= DUAL_BOOT_MAX_SLOTS ||
	    target_slot == state.confirmed_slot)
		return -EINVAL;

	manual_trial_preflight = true;
	manual_trial_preflight_target = target_slot;
	ret = board_boot_default(false);
	manual_trial_preflight = false;

	return ret;
}

int enetlite_ab_start_manual_trial(u32 target_slot)
{
	struct enetlite_ab_state state;
	int ret;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	if (state.state != ENETLITE_AB_STABLE ||
	    target_slot >= DUAL_BOOT_MAX_SLOTS ||
	    target_slot == state.confirmed_slot)
		return -EINVAL;

	state.state = ENETLITE_AB_TRIAL;
	state.target_slot = target_slot;
	state.operation = ENETLITE_AB_OP_MANUAL_SWITCH;
	state.tries_left = ENETLITE_AB_MAX_TRIES - 1;
	state.good_mask &= ~enetlite_ab_slot_bit(target_slot);

	ret = enetlite_ab_state_commit(&state);
	if (ret)
		return ret;

	manual_trial_first_boot = true;
	manual_trial_first_boot_target = target_slot;

	return 0;
}

int enetlite_ab_get_confirmed_slot(u32 *slot)
{
	struct enetlite_ab_state state;
	int ret;

	if (!slot)
		return -EINVAL;

	ret = enetlite_ab_state_load(&state);
	if (ret)
		return ret;

	*slot = state.confirmed_slot;

	return 0;
}

int enetlite_ab_get_inactive_slot(u32 *slot)
{
	u32 confirmed_slot;
	int ret;

	if (!slot)
		return -EINVAL;

	ret = enetlite_ab_get_confirmed_slot(&confirmed_slot);
	if (ret)
		return ret;

	*slot = (confirmed_slot + 1) % DUAL_BOOT_MAX_SLOTS;

	return 0;
}

static int enetlite_ab_boot_slot(struct dual_boot_priv *priv,
				 struct enetlite_ab_state *state, u32 slot,
				 enum enetlite_ab_boot_reason reason,
				 bool do_boot)
{
	enetlite_ab_set_last_decision(state, slot, reason);
	return priv->boot_slot(priv, slot, do_boot);
}

static int enetlite_ab_try_confirmed(struct dual_boot_priv *priv,
				     struct enetlite_ab_state *state,
				     enum enetlite_ab_boot_reason reason,
				     bool do_boot)
{
	return enetlite_ab_boot_slot(priv, state, state->confirmed_slot, reason,
				     do_boot);
}

int dual_boot(struct dual_boot_priv *priv, bool do_boot)
{
	struct enetlite_ab_state state, old_state;
	u32 other;
	int ret, saved_ret;

	ret = enetlite_ab_state_load(&state);
	if (ret) {
		memset(&state, 0, sizeof(state));
		state.schema = ENETLITE_AB_SCHEMA;
		state.state = ENETLITE_AB_STABLE;
		state.confirmed_slot = 0;
		state.good_mask = 1;
		state.target_slot = -1;
		state.operation = ENETLITE_AB_OP_NONE;
		state.tries_left = -1;
		enetlite_ab_set_last_decision(&state, 0,
					       ENETLITE_AB_REASON_INVALID_STATE);
		printf("A/B state is invalid, refusing to guess a boot slot (%d)\n",
		       ret);
		return ret;
	}

	switch (state.state) {
	case ENETLITE_AB_STABLE:
		if (manual_trial_preflight && !do_boot &&
		    manual_trial_preflight_target < DUAL_BOOT_MAX_SLOTS &&
		    manual_trial_preflight_target != state.confirmed_slot) {
			state.state = ENETLITE_AB_TRIAL;
			state.target_slot = manual_trial_preflight_target;
			state.operation = ENETLITE_AB_OP_MANUAL_SWITCH;
			state.tries_left = ENETLITE_AB_MAX_TRIES - 1;
			state.good_mask &= ~enetlite_ab_slot_bit(state.target_slot);

			printf("A/B manual trial preflight slot %u\n",
			       state.target_slot);
			return enetlite_ab_boot_slot(priv, &state,
						    state.target_slot,
						    ENETLITE_AB_REASON_TRIAL,
						    false);
		}

		ret = enetlite_ab_try_confirmed(priv, &state,
						ENETLITE_AB_REASON_NORMAL,
						do_boot);
		if (!ret || !do_boot)
			return ret;

		other = 1 - state.confirmed_slot;
		if (!(state.good_mask & enetlite_ab_slot_bit(other))) {
			printf("Confirmed slot %u failed and no known-good fallback exists\n",
			       state.confirmed_slot);
			enetlite_ab_set_last_decision(&state, 0,
						       ENETLITE_AB_REASON_RECOVERY);
			return ret;
		}

		printf("Confirmed slot %u failed, hard-fallback to slot %u\n",
		       state.confirmed_slot, other);

		state.good_mask &= ~enetlite_ab_slot_bit(state.confirmed_slot);
		state.confirmed_slot = other;
		state.target_slot = -1;
		state.operation = ENETLITE_AB_OP_NONE;
		state.tries_left = -1;

		saved_ret = enetlite_ab_state_commit(&state);
		if (saved_ret) {
			printf("Failed to persist hard-fallback state: %d\n",
			       saved_ret);
			return ret;
		}

		ret = enetlite_ab_try_confirmed(priv, &state,
						ENETLITE_AB_REASON_HARD_FALLBACK,
						do_boot);
		if (ret && do_boot) {
			printf("Hard-fallback slot %u failed, entering recovery\n",
			       state.confirmed_slot);
			enetlite_ab_set_last_decision(&state, 0,
						       ENETLITE_AB_REASON_RECOVERY);
		}

		return ret;
	case ENETLITE_AB_INSTALLING:
		printf("A/B install incomplete, booting confirmed slot %u\n",
		       state.confirmed_slot);
		ret = enetlite_ab_try_confirmed(priv, &state,
						ENETLITE_AB_REASON_NORMAL,
						do_boot);
		if (ret && do_boot) {
			printf("Confirmed slot %u failed during installing state, entering recovery\n",
			       state.confirmed_slot);
			enetlite_ab_set_last_decision(&state, 0,
						       ENETLITE_AB_REASON_RECOVERY);
		}

		return ret;
	case ENETLITE_AB_TRIAL:
		if (state.tries_left <= 0) {
			printf("A/B trial exhausted, rolling back to slot %u\n",
			       state.confirmed_slot);
			ret = enetlite_ab_boot_slot(priv, &state,
						    state.confirmed_slot,
						    ENETLITE_AB_REASON_ROLLBACK,
						    do_boot);
			if (ret && do_boot) {
				printf("Rollback slot %u failed, entering recovery\n",
				       state.confirmed_slot);
				enetlite_ab_set_last_decision(&state, 0,
							       ENETLITE_AB_REASON_RECOVERY);
			}

			return ret;
		}

		if (manual_trial_first_boot &&
		    state.operation == ENETLITE_AB_OP_MANUAL_SWITCH &&
		    state.target_slot == manual_trial_first_boot_target &&
		    state.tries_left == ENETLITE_AB_MAX_TRIES - 1) {
			if (do_boot)
				manual_trial_first_boot = false;

			printf("A/B manual trial boot slot %u, tries left: %d\n",
			       state.target_slot, state.tries_left);
			ret = enetlite_ab_boot_slot(priv, &state, state.target_slot,
						    ENETLITE_AB_REASON_TRIAL, do_boot);
			if (!ret || !do_boot)
				return ret;

			printf("A/B manual trial slot %u failed before Linux handoff, rolling back to slot %u\n",
			       state.target_slot, state.confirmed_slot);
			state.tries_left = 0;
			saved_ret = enetlite_ab_state_commit(&state);
			if (saved_ret)
				printf("Failed to persist manual trial rollback: %d\n",
				       saved_ret);

			ret = enetlite_ab_try_confirmed(priv, &state,
							ENETLITE_AB_REASON_ROLLBACK,
							do_boot);
			if (ret && do_boot) {
				printf("Rollback slot %u failed, entering recovery\n",
				       state.confirmed_slot);
				enetlite_ab_set_last_decision(&state, 0,
							       ENETLITE_AB_REASON_RECOVERY);
			}

			return ret;
		}

		old_state = state;
		state.tries_left--;

		if (do_boot) {
			ret = enetlite_ab_state_commit(&state);
			if (ret) {
				printf("Failed to persist A/B trial decrement: %d\n",
				       ret);
				ret = enetlite_ab_boot_slot(
					priv, &old_state, old_state.confirmed_slot,
					ENETLITE_AB_REASON_ROLLBACK,
					do_boot);
				if (ret && do_boot) {
					printf("Rollback slot %u failed, entering recovery\n",
					       old_state.confirmed_slot);
					enetlite_ab_set_last_decision(
						&old_state, 0,
						ENETLITE_AB_REASON_RECOVERY);
				}

				return ret;
			}
		}

		printf("A/B trial boot slot %u, tries left after pre-decrement: %d\n",
		       state.target_slot, state.tries_left);
		ret = enetlite_ab_boot_slot(priv, &state, state.target_slot,
					    ENETLITE_AB_REASON_TRIAL, do_boot);
		if (!ret || !do_boot)
			return ret;

		printf("A/B trial slot %u failed before Linux handoff, rolling back to slot %u\n",
		       state.target_slot, state.confirmed_slot);

		if (do_boot) {
			old_state = state;
			state.tries_left = 0;
			saved_ret = enetlite_ab_state_commit(&state);
			if (saved_ret) {
				printf("Failed to persist trial failure rollback: %d\n",
				       saved_ret);
				state = old_state;
			}
		}

		ret = enetlite_ab_try_confirmed(priv, &state,
						ENETLITE_AB_REASON_ROLLBACK,
						do_boot);
		if (ret && do_boot) {
			printf("Rollback slot %u failed, entering recovery\n",
			       state.confirmed_slot);
			enetlite_ab_set_last_decision(&state, 0,
						       ENETLITE_AB_REASON_RECOVERY);
		}

		return ret;
	default:
		return -EINVAL;
	}
}

static const char *enetlite_ab_overlay_volume(u32 slot)
{
	if (slot < DUAL_BOOT_MAX_SLOTS && dual_boot_slots[slot].rootfs_data)
		return dual_boot_slots[slot].rootfs_data;

	return PART_ROOTFS_DATA_NAME;
}

static int dual_boot_set_default_bootargs(void)
{
	const struct enetlite_ab_boot_decision *decision;
	static char actual_slot[16], confirmed_slot[16];

	decision = enetlite_ab_last_decision();
	if (!decision)
		return 0;

	snprintf(actual_slot, sizeof(actual_slot), "%u",
		 decision->actual_slot);
	snprintf(confirmed_slot, sizeof(confirmed_slot), "%u",
		 decision->confirmed_slot);

	if (bootargs_set("boot_param.enetlite_actual_boot_slot", actual_slot))
		return -1;

	if (bootargs_set("boot_param.enetlite_confirmed_slot", confirmed_slot))
		return -1;

	return 0;
}

static int dual_boot_set_fdt_defaults(void *fdt)
{
	const struct enetlite_ab_boot_decision *decision;
	const char *operation, *ab_state;

	decision = enetlite_ab_last_decision();
	if (!decision)
		return 0;

	if (decision->boot_path == ENETLITE_AB_BOOT_PATH_SLOT)
		rootdisk_set_fitblk_rootfs(fdt,
					   dual_boot_slots[decision->actual_slot].kernel);

	if (fdtargs_set_u32("enetlite,ab-abi-version", 1))
		return -1;

	if (fdtargs_set("enetlite,boot-path",
			enetlite_ab_boot_path_name(decision->boot_path)))
		return -1;

	if (decision->reason == ENETLITE_AB_REASON_INVALID_STATE)
		ab_state = "invalid";
	else
		ab_state = enetlite_ab_state_name(decision->state);

	if (fdtargs_set("enetlite,ab-state", ab_state))
		return -1;

	if (decision->boot_path == ENETLITE_AB_BOOT_PATH_SLOT) {
		if (fdtargs_set_u32("enetlite,actual-boot-slot",
				    decision->actual_slot))
			return -1;
		if (fdtargs_set_u32("enetlite,confirmed-slot",
				    decision->confirmed_slot))
			return -1;
		if (fdtargs_set_u32("enetlite,good-mask",
				    decision->good_mask))
			return -1;

		if (decision->target_slot >= 0 &&
		    fdtargs_set_u32("enetlite,target-slot",
				    decision->target_slot))
			return -1;

		operation = enetlite_ab_operation_name(decision->operation);
		if (decision->operation != ENETLITE_AB_OP_NONE &&
		    fdtargs_set("enetlite,operation", operation))
			return -1;

		if (decision->state == ENETLITE_AB_TRIAL &&
		    fdtargs_set_u32("enetlite,tries-left",
				    decision->tries_left))
			return -1;
	}

	if (fdtargs_set("enetlite,boot-reason",
			enetlite_ab_boot_reason_name(decision->reason)))
		return -1;
	if (fdtargs_set("enetlite,reset-reason", "unknown"))
		return -1;
	if (decision->boot_path == ENETLITE_AB_BOOT_PATH_SLOT) {
		if (fdtargs_set("enetlite,boot-firmware-volume",
				dual_boot_slots[decision->actual_slot].kernel))
			return -1;
		if (fdtargs_set("enetlite,boot-overlay-volume",
				enetlite_ab_overlay_volume(decision->actual_slot)))
			return -1;
	}

	return 0;
}

int dual_boot_set_defaults(void *fdt)
{
	int ret;

	if (dual_boot_disabled)
		return 0;

	if (IS_ENABLED(CONFIG_MTK_DUAL_BOOT_ITB_IMAGE))
		ret = dual_boot_set_fdt_defaults(fdt);
	else
		ret = dual_boot_set_default_bootargs();

	if (ret)
		panic("Error: failed to setup dual boot settings\n");

	return ret;
}
