// SPDX-License-Identifier: GPL-2.0+
/*
 * eNetLite bootloader branding and build metadata.
 */

#include <enetlite_bootinfo.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <stdio.h>
#include <version_string.h>

const char *enetlite_bootinfo_name(void)
{
	return CONFIG_ENETLITE_BOOTLOADER_NAME;
}

const char *enetlite_bootinfo_version(void)
{
	return CONFIG_ENETLITE_BOOTLOADER_VERSION;
}

const char *enetlite_bootinfo_build_id(void)
{
	return CONFIG_ENETLITE_BOOTLOADER_BUILD_ID;
}

const char *enetlite_bootinfo_version_full(void)
{
	static char version_full[128];

	if (!version_full[0])
		snprintf(version_full, sizeof(version_full), "%s build %s",
			 enetlite_bootinfo_version(),
			 enetlite_bootinfo_build_id());

	return version_full;
}

const char *enetlite_bootinfo_web_version(void)
{
	static char web_version[512];

	if (!web_version[0])
		snprintf(web_version, sizeof(web_version),
			 "<strong>%s</strong><br>"
			 "Version: %s<br>"
			 "Build: %s<br>"
			 "U-Boot: %s",
			 enetlite_bootinfo_name(),
			 enetlite_bootinfo_version(),
			 enetlite_bootinfo_build_id(),
			 version_string);

	return web_version;
}

void enetlite_bootinfo_print(void)
{
	printf("\n%s\n", enetlite_bootinfo_name());
	printf("Version: %s\n", enetlite_bootinfo_version());
	printf("Build: %s\n\n", enetlite_bootinfo_build_id());
}

static int set_chosen_string(void *fdt, int chosenoff, const char *prop,
			     const char *value)
{
	int ret;

	ret = fdt_setprop(fdt, chosenoff, prop, value, strlen(value) + 1);
	if (ret < 0)
		printf("WARNING: could not set %s %s.\n", prop,
		       fdt_strerror(ret));

	return ret;
}

int enetlite_bootinfo_fdt_fixup(void *fdt, int chosenoff)
{
	int ret;

	ret = set_chosen_string(fdt, chosenoff, "enetlite,bootloader-name",
				enetlite_bootinfo_name());
	if (ret < 0)
		return ret;

	ret = set_chosen_string(fdt, chosenoff, "enetlite,bootloader-version",
				enetlite_bootinfo_version());
	if (ret < 0)
		return ret;

	ret = set_chosen_string(fdt, chosenoff, "enetlite,bootloader-build-id",
				enetlite_bootinfo_build_id());
	if (ret < 0)
		return ret;

	return set_chosen_string(fdt, chosenoff,
				 "enetlite,bootloader-version-full",
				 enetlite_bootinfo_version_full());
}

