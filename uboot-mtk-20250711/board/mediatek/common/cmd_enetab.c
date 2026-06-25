// SPDX-License-Identifier: GPL-2.0
/*
 * EnetLite A/B state management command.
 */

#include <command.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <stdio.h>
#include <string.h>

#include "dual_boot.h"

static void enetab_print_state(const struct enetlite_ab_state *state)
{
	printf("schema=%u\n", state->schema);
	printf("state=%s\n", enetlite_ab_state_name(state->state));
	printf("confirmed_slot=%u\n", state->confirmed_slot);
	printf("good_mask=0x%x\n", state->good_mask);

	if (state->target_slot >= 0)
		printf("target_slot=%d\n", state->target_slot);
	if (state->operation != ENETLITE_AB_OP_NONE)
		printf("operation=%s\n",
		       enetlite_ab_operation_name(state->operation));
	if (state->tries_left >= 0)
		printf("tries_left=%d\n", state->tries_left);
}

static int enetab_parse_slot(const char *value, u32 *slot)
{
	if (!strcmp(value, "0")) {
		*slot = 0;
		return 0;
	}

	if (!strcmp(value, "1")) {
		*slot = 1;
		return 0;
	}

	return -EINVAL;
}

static int do_enetab(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	struct enetlite_ab_state state;
	u32 slot, good_mask;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "show")) {
		ret = enetlite_ab_state_load(&state);
		if (ret) {
			printf("A/B state invalid or missing: %d\n", ret);
			return CMD_RET_FAILURE;
		}

		enetab_print_state(&state);
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "init")) {
		if (argc < 3 || argc > 4)
			return CMD_RET_USAGE;

		ret = enetab_parse_slot(argv[2], &slot);
		if (ret)
			return CMD_RET_USAGE;

		good_mask = 1U << slot;
		if (argc == 4) {
			if (!strcmp(argv[3], "both-good"))
				good_mask = 0x3;
			else
				return CMD_RET_USAGE;
		}

		ret = enetlite_ab_init(slot, good_mask, true);
		if (ret) {
			printf("Failed to initialize A/B state: %d\n", ret);
			return CMD_RET_FAILURE;
		}

		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "mark-good")) {
		if (argc != 3)
			return CMD_RET_USAGE;

		ret = enetab_parse_slot(argv[2], &slot);
		if (ret)
			return CMD_RET_USAGE;

		ret = enetlite_ab_mark_good(slot);
		if (ret) {
			printf("Failed to mark slot %u good: %d\n", slot, ret);
			return CMD_RET_FAILURE;
		}

		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "rollback-complete")) {
		if (argc != 2)
			return CMD_RET_USAGE;

		ret = enetlite_ab_complete_rollback();
		if (ret) {
			printf("Failed to complete rollback: %d\n", ret);
			return CMD_RET_FAILURE;
		}

		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "abort-install")) {
		if (argc != 2)
			return CMD_RET_USAGE;

		ret = enetlite_ab_abort_install();
		if (ret) {
			printf("Failed to abort install: %d\n", ret);
			return CMD_RET_FAILURE;
		}

		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	enetab, 4, 0, do_enetab,
	"manage EnetLite A/B state",
	"show\n"
	"enetab init <0|1> [both-good]\n"
	"enetab mark-good <0|1>\n"
	"enetab rollback-complete\n"
	"enetab abort-install"
);
