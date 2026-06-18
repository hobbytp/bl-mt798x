/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __ENETLITE_BOOTINFO_H__
#define __ENETLITE_BOOTINFO_H__

#if defined(CONFIG_ENETLITE_BOOTINFO) && !defined(CONFIG_XPL_BUILD)

const char *enetlite_bootinfo_name(void);
const char *enetlite_bootinfo_version(void);
const char *enetlite_bootinfo_build_id(void);
const char *enetlite_bootinfo_version_full(void);
const char *enetlite_bootinfo_web_version(void);
void enetlite_bootinfo_print(void);
int enetlite_bootinfo_fdt_fixup(void *fdt, int chosenoff);

#else

static inline const char *enetlite_bootinfo_name(void)
{
	return "";
}

static inline const char *enetlite_bootinfo_version(void)
{
	return "";
}

static inline const char *enetlite_bootinfo_build_id(void)
{
	return "";
}

static inline const char *enetlite_bootinfo_version_full(void)
{
	return "";
}

static inline const char *enetlite_bootinfo_web_version(void)
{
	return NULL;
}

static inline void enetlite_bootinfo_print(void)
{
}

static inline int enetlite_bootinfo_fdt_fixup(void *fdt, int chosenoff)
{
	return 0;
}

#endif

#endif /* __ENETLITE_BOOTINFO_H__ */

