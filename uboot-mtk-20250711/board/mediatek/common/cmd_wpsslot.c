// SPDX-License-Identifier: GPL-2.0
/*
 * Z8105AX WPS/Mesh button A/B slot switch command.
 *
 * This is scoped to the normal/per-slot Z8105AX A/B builds. The abandoned
 * shared-rootfs-data defconfig is intentionally out of scope.
 */

#include <button.h>
#include <command.h>
#include <time.h>

#include "dual_boot.h"

#define WPS_SLOT_BUTTON_LABEL		"wps"
#define WPS_SLOT_HOLD_SECONDS		3

static int wpsslot_switch_to_next_slot(void)
{
	u32 current_slot, target_slot;
	int ret;

	current_slot = dual_boot_get_current_slot();
	target_slot = dual_boot_get_next_slot();

	printf("WPS/Mesh recovery: switching A/B slot %u -> %u\n",
	       current_slot, target_slot);

	ret = dual_boot_set_slot_invalid(target_slot, false, false);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_MTK_DUAL_BOOT_ENABLE_RETRY)) {
		ret = dual_boot_set_boot_count(target_slot, 0);
		if (ret)
			printf("Warning: failed to reset bootcount for slot %u (%d)\n",
			       target_slot, ret);
	}

	ret = dual_boot_set_current_slot(target_slot);
	if (ret)
		return ret;

	printf("WPS/Mesh recovery: slot %u selected for next boot\n",
	       target_slot);

	return 0;
}

static int do_wpsslot(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	const char *button_label = WPS_SLOT_BUTTON_LABEL;
	struct udevice *dev;
	ulong ts;
	int ret, counter = 0;

	if (argc > 1)
		button_label = argv[1];

	ret = button_get_by_label(button_label, &dev);
	if (ret) {
		printf("WPS/Mesh button '%s' not found (err=%d)\n",
		       button_label, ret);
		return CMD_RET_SUCCESS;
	}

	if (!button_get_state(dev))
		return CMD_RET_SUCCESS;

	printf("WPS/Mesh button is pressed for: %2d second(s)", counter++);

	ts = get_timer(0);

	while (button_get_state(dev) && counter <= WPS_SLOT_HOLD_SECONDS) {
		if (get_timer(ts) < 1000)
			continue;

		ts = get_timer(0);
		printf("\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s)", counter++);
	}

	printf("\n");

	if (counter <= WPS_SLOT_HOLD_SECONDS)
		return CMD_RET_SUCCESS;

	ret = wpsslot_switch_to_next_slot();
	if (ret) {
		printf("WPS/Mesh recovery: failed to switch slot (%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	wpsslot, 2, 0, do_wpsslot,
	"check WPS/Mesh button and switch to next A/B slot",
	"[button-label]"
);

