// SPDX-License-Identifier: GPL-2.0
/*
 * Z8105AX reset + WPS/Mesh button A/B slot switch command.
 *
 * This is scoped to the normal/per-slot Z8105AX A/B builds. The abandoned
 * shared-rootfs-data defconfig is intentionally out of scope.
 */

#include <button.h>
#include <command.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <time.h>

#include "dual_boot.h"

#define WPS_SLOT_BUTTON_LABEL		"wps"
#define WPS_SLOT_RESET_BUTTON_LABEL	"reset"
#define WPS_SLOT_HOLD_SECONDS		3
#define WPS_SLOT_RELEASE_TIMEOUT_MS	30000U
#define WPS_SLOT_RELEASE_POLL_US	10000U

static int wpsslot_switch_to_next_slot(void)
{
	u32 confirmed_slot, target_slot;
	int ret;

	ret = enetlite_ab_cancel_trial();
	if (!ret) {
		printf("WPS/Mesh recovery: active A/B trial cancelled; confirmed slot will boot as rollback\n");
		return 0;
	}

	if (ret == -EBUSY) {
		ret = enetlite_ab_abort_install();
		if (ret)
			return ret;
		printf("WPS/Mesh recovery: interrupted A/B install aborted; confirmed slot will boot\n");
		return 0;
	}

	if (ret != -ENOENT)
		return ret;

	ret = enetlite_ab_get_confirmed_slot(&confirmed_slot);
	if (ret)
		return ret;

	ret = enetlite_ab_get_inactive_slot(&target_slot);
	if (ret)
		return ret;

	printf("WPS/Mesh recovery: switching A/B slot %u -> %u\n",
	       confirmed_slot, target_slot);

	ret = enetlite_ab_preflight_manual_trial(target_slot);
	if (ret) {
		printf("WPS/Mesh recovery: target slot %u preflight failed (%d)\n",
		       target_slot, ret);
		return ret;
	}

	ret = enetlite_ab_start_manual_trial(target_slot);
	if (ret)
		return ret;

	printf("WPS/Mesh recovery: slot %u selected for trial boot\n",
	       target_slot);

	return 0;
}

static int wpsslot_button_pressed(struct udevice *dev)
{
	return button_get_state(dev) == BUTTON_ON;
}

static void wpsslot_wait_release(struct udevice *wps_dev,
				 struct udevice *reset_dev)
{
	ulong ts = get_timer(0);

	while (wpsslot_button_pressed(wps_dev) ||
	       wpsslot_button_pressed(reset_dev)) {
		if (get_timer(ts) >= WPS_SLOT_RELEASE_TIMEOUT_MS) {
			printf("Warning: RESET/WPS buttons still pressed after %u ms, continuing\n",
			       WPS_SLOT_RELEASE_TIMEOUT_MS);
			break;
		}

		udelay(WPS_SLOT_RELEASE_POLL_US);
	}
}

static int do_wpsslot(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	const char *wps_label = WPS_SLOT_BUTTON_LABEL;
	const char *reset_label = WPS_SLOT_RESET_BUTTON_LABEL;
	struct udevice *wps_dev, *reset_dev;
	ulong ts;
	int ret, counter = 0;

	if (argc > 1)
		wps_label = argv[1];
	if (argc > 2)
		reset_label = argv[2];

	ret = button_get_by_label(wps_label, &wps_dev);
	if (ret) {
		printf("WPS/Mesh button '%s' not found (err=%d)\n",
		       wps_label, ret);
		return CMD_RET_SUCCESS;
	}

	ret = button_get_by_label(reset_label, &reset_dev);
	if (ret) {
		printf("RESET button '%s' not found (err=%d)\n",
		       reset_label, ret);
		return CMD_RET_SUCCESS;
	}

	if (!wpsslot_button_pressed(wps_dev) ||
	    !wpsslot_button_pressed(reset_dev))
		return CMD_RET_SUCCESS;

	printf("RESET + WPS/Mesh buttons are pressed for: %2d second(s)",
	       counter++);

	ts = get_timer(0);

	while (wpsslot_button_pressed(wps_dev) &&
	       wpsslot_button_pressed(reset_dev) &&
	       counter <= WPS_SLOT_HOLD_SECONDS) {
		if (get_timer(ts) < 1000)
			continue;

		ts = get_timer(0);
		printf("\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s)", counter++);
	}

	printf("\n");

	if (counter <= WPS_SLOT_HOLD_SECONDS) {
		wpsslot_wait_release(wps_dev, reset_dev);
		return CMD_RET_SUCCESS;
	}

	ret = wpsslot_switch_to_next_slot();
	wpsslot_wait_release(wps_dev, reset_dev);
	if (ret) {
		printf("WPS/Mesh recovery: failed to switch slot (%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	wpsslot, 3, 0, do_wpsslot,
	"check RESET + WPS/Mesh buttons and switch to next A/B slot",
	"[wps-button-label [reset-button-label]]"
);
