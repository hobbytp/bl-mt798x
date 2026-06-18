#!/bin/sh

TOOLCHAIN=${TOOLCHAIN:-aarch64-linux-gnu-}
DEFAULT_UBOOT_DIR=uboot-mtk-20250711
DEFAULT_ATF_DIR=atf-20250711
#UBOOT_DIR=uboot-mtk-20220606
UBOOT_DIR=${UBOOT_DIR:-$DEFAULT_UBOOT_DIR}
#ATF_DIR=atf-20220606-637ba581b
ATF_DIR=${ATF_DIR:-$DEFAULT_ATF_DIR}

resolve_config_root() {
	cfg_root="$1/configs"

	if [ -d "$cfg_root" ]; then
		printf '%s\n' "$cfg_root"
		return 0
	fi

	if [ -f "$cfg_root" ]; then
		redirected_cfg_root=$(sed -n '1p' "$cfg_root")

		if [ -z "$redirected_cfg_root" ]; then
			return 1
		fi

		case "$redirected_cfg_root" in
			/*|[A-Za-z]:/*)
				printf '%s\n' "$redirected_cfg_root"
				;;
			*)
				printf '%s\n' "$1/$redirected_cfg_root"
				;;
		esac
		return 0
	fi

	return 1
}

find_fiptool() {
	for candidate in \
		"$ATF_DIR/tools/fiptool/fiptool" \
		"$ATF_DIR/tools/fiptool/fiptool.exe"
	do
		if [ -x "$candidate" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done

	return 1
}

build_generic_fiptool() {
	make -C "$ATF_DIR/tools/fiptool" clean
	make -C "$ATF_DIR/tools/fiptool" all
}

enetlite_build_id() {
	short_sha=$(git rev-parse --short=12 HEAD 2>/dev/null || true)
	[ -z "$short_sha" ] && short_sha="unknown"

	build_id="g$short_sha"

	if git status --porcelain --untracked-files=no -- "$UBOOT_DIR" "$ATF_DIR" build.sh 2>/dev/null | grep -q .; then
		build_id="${build_id}-dirty"
	fi

	printf '%s\n' "$build_id"
}

if [ -z "$SOC" ] || [ -z "$BOARD" ]; then
	echo "Usage: SOC=[mt7981|mt7986] BOARD=<board name> MULTI_LAYOUT=[0|1] $0"
	echo "Defaults: UBOOT_DIR=$DEFAULT_UBOOT_DIR ATF_DIR=$DEFAULT_ATF_DIR"
	echo "eg: SOC=mt7981 BOARD=360t7 $0"
	echo "eg: SOC=mt7981 BOARD=wr30u MULTI_LAYOUT=1 $0"
	echo "eg: SOC=mt7981 BOARD=cmcc_rax3000m-emmc $0"
	echo "eg: SOC=mt7981 BOARD=z8105ax $0"
	echo "eg: SOC=mt7981 BOARD=z8105ax UBOOT_CFG=mt7981_z8105ax_shared_rootfs_data_defconfig $0"
	echo "eg: SOC=mt7986 BOARD=redmi_ax6000 MULTI_LAYOUT=1 $0"
	echo "eg: SOC=mt7986 BOARD=jdcloud_re-cp-03 $0"
	exit 1
fi

# Check if Python is installed on the system
command -v python3
[ "$?" != "0" ] && { echo "Error: Python is not installed on this system."; exit 0; }

echo "Trying cross compiler..."
command -v "${TOOLCHAIN}gcc"
[ "$?" != "0" ] && { echo "${TOOLCHAIN}gcc not found!"; exit 0; }
export CROSS_COMPILE="$TOOLCHAIN"

ATF_CONFIG_ROOT=$(resolve_config_root "$ATF_DIR")
[ -z "$ATF_CONFIG_ROOT" ] && {
	echo "ATF config root not found under $ATF_DIR"
	exit 1
}

UBOOT_CONFIG_ROOT="$UBOOT_DIR/configs"
DEFAULT_ATF_CFG="${SOC}_${BOARD}_defconfig"
DEFAULT_UBOOT_CFG="${SOC}_${BOARD}_defconfig"
ATF_CFG=${ATF_CFG:-$DEFAULT_ATF_CFG}

if [ ! -f "$ATF_CONFIG_ROOT/$ATF_CFG" ]; then
	echo "$ATF_CONFIG_ROOT/$ATF_CFG not found!"
	exit 1
fi

if grep -Eq "CONFIG_FLASH_DEVICE_EMMC=y|_BOOT_DEVICE_EMMC=y" "$ATF_CONFIG_ROOT/$ATF_CFG" ; then
	# No fixed-mtdparts or multilayout for EMMC
	fixedparts=0
	multilayout=0
else
	# Build fixed-mtdparts by default for NAND
	fixedparts=${FIXED_MTDPARTS:-1}
	multilayout=${MULTI_LAYOUT:-0}
	if [ "$multilayout" = "1" ]; then
		DEFAULT_UBOOT_CFG="${SOC}_${BOARD}_multi_layout_defconfig"
	fi
fi

UBOOT_CFG=${UBOOT_CFG:-$DEFAULT_UBOOT_CFG}

for file in "$ATF_CONFIG_ROOT/$ATF_CFG" "$UBOOT_CONFIG_ROOT/$UBOOT_CFG"; do
	if [ ! -f "$file" ]; then
		echo "$file not found!"
		exit 1
	fi
done

echo "Building for: ${SOC}_${BOARD}, fixed-mtdparts: $fixedparts, multi-layout: $multilayout"
echo "u-boot dir: $UBOOT_DIR"
echo "atf dir: $ATF_DIR"
echo "u-boot config root: $UBOOT_CONFIG_ROOT"
echo "atf config root: $ATF_CONFIG_ROOT"
echo "u-boot config: $UBOOT_CFG"
echo "atf config: $ATF_CFG"

ENETLITE_BUILD_ID=
if grep -q '^CONFIG_ENETLITE_BOOTINFO=y' "$UBOOT_CONFIG_ROOT/$UBOOT_CFG"; then
	ENETLITE_BUILD_ID=$(enetlite_build_id)
	echo "eNetLiteBoot build id: $ENETLITE_BUILD_ID"
fi

echo "Build u-boot..."
rm -f "$UBOOT_DIR/u-boot.bin"
cp -f "$UBOOT_CONFIG_ROOT/$UBOOT_CFG" "$UBOOT_DIR/.config"
if [ "$fixedparts" = "1" ]; then
	echo "Build u-boot with fixed-mtdparts!"
	echo "CONFIG_MEDIATEK_UBI_FIXED_MTDPARTS=y" >> "$UBOOT_DIR/.config"
	echo "CONFIG_MTK_FIXED_MTD_MTDPARTS=y" >> "$UBOOT_DIR/.config"
fi
if [ -n "$ENETLITE_BUILD_ID" ]; then
	echo "CONFIG_ENETLITE_BOOTLOADER_BUILD_ID=\"$ENETLITE_BUILD_ID\"" >> "$UBOOT_DIR/.config"
fi
make -C "$UBOOT_DIR" olddefconfig
make -C "$UBOOT_DIR" clean
make -C "$UBOOT_DIR" -j $(nproc) all
if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	echo "u-boot build done!"
else
	echo "u-boot build fail!"
	exit 1
fi

echo "Build atf..."
if [ -e "$ATF_DIR/makefile" ]; then
	ATF_MKFILE="makefile"
else
	ATF_MKFILE="Makefile"
fi
make -C "$ATF_DIR" -f "$ATF_MKFILE" clean CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN"
rm -rf "$ATF_DIR/build"
make -C "$ATF_DIR" -f "$ATF_MKFILE" "$ATF_CFG" CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN"
make -C "$ATF_DIR" -f "$ATF_MKFILE" all CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN" CONFIG_BL33="../$UBOOT_DIR/u-boot.bin" BL33="../$UBOOT_DIR/u-boot.bin" -j $(nproc)

mkdir -p "output"
if [ -f "$ATF_DIR/build/$SOC/release/fip.bin" ]; then
	FIP_NAME="${SOC}_${BOARD}-fip"
	FIP_ONLY_NAME="${SOC}_${BOARD}-fip-only"
	if [ "$BOARD" = "z8105ax" ]; then
		case "$UBOOT_CFG" in
			*_shared_rootfs_data_defconfig)
				FIP_NAME="${FIP_NAME}-shared-rootfs-data"
				FIP_ONLY_NAME="${FIP_ONLY_NAME}-shared-rootfs-data"
				;;
			*)
				FIP_NAME="${FIP_NAME}-per-slot"
				FIP_ONLY_NAME="${FIP_ONLY_NAME}-per-slot"
				;;
		esac
	fi
	if [ "$fixedparts" = "1" ]; then
		FIP_NAME="${FIP_NAME}-fixed-parts"
		FIP_ONLY_NAME="${FIP_ONLY_NAME}-fixed-parts"
	fi
	if [ "$multilayout" = "1" ]; then
		FIP_NAME="${FIP_NAME}-multi-layout"
		FIP_ONLY_NAME="${FIP_ONLY_NAME}-multi-layout"
	fi
	cp -f "$ATF_DIR/build/$SOC/release/fip.bin" "output/$FIP_NAME.bin"
	cp -f "$ATF_DIR/build/$SOC/release/fip.bin" "output/$FIP_ONLY_NAME.bin"
	cat > "output/$FIP_ONLY_NAME.txt" <<EOF
This image is identical to output/$FIP_NAME.bin.
Recommended use: FIP-only validation path, keep the existing BL2 unchanged.

SOC=$SOC
BOARD=$BOARD
ATF_DIR=$ATF_DIR
ATF_CFG=$ATF_CFG
UBOOT_DIR=$UBOOT_DIR
UBOOT_CFG=$UBOOT_CFG
ENETLITE_BUILD_ID=${ENETLITE_BUILD_ID:-}

Before flashing:
- Verify the image size fits in the target FIP partition.
- Back up the current FIP / Factory / u-boot-env partitions.
- Flash the FIP partition only; do not erase or overwrite BL2.
EOF
	FIPTOOL_BIN=$(find_fiptool || true)
	if [ -n "$FIPTOOL_BIN" ]; then
		if "$FIPTOOL_BIN" info "output/$FIP_NAME.bin" > "output/$FIP_NAME.info.txt" 2>/dev/null; then
			cp -f "output/$FIP_NAME.info.txt" "output/$FIP_ONLY_NAME.info.txt"
			echo "Generated FIP ToC info: output/$FIP_NAME.info.txt"
		else
			echo "Warning: fiptool info failed for output/$FIP_NAME.bin"
		fi
	else
		echo "Warning: fiptool binary not found, skip FIP ToC dump"
	fi
	if [ "$BOARD" = "z8105ax" ]; then
		FIP_COMPAT_NAME="${FIP_ONLY_NAME}-no-mtk-chksum"
		BL31_BIN="$ATF_DIR/build/$SOC/release/bl31.bin"
		if [ ! -f "$BL31_BIN" ]; then
			echo "BL31 image not found: $BL31_BIN"
			exit 1
		fi
		echo "Build generic fiptool for Z8105AX no-mtk-chksum FIP..."
		if ! build_generic_fiptool; then
			echo "generic fiptool build failed"
			exit 1
		fi
		FIPTOOL_BIN=$(find_fiptool || true)
		if [ -z "$FIPTOOL_BIN" ]; then
			echo "generic fiptool binary not found"
			exit 1
		fi
		if ! "$FIPTOOL_BIN" create \
			--soc-fw "$BL31_BIN" \
			--nt-fw "$UBOOT_DIR/u-boot.bin" \
			"output/$FIP_COMPAT_NAME.bin"; then
			echo "$FIP_COMPAT_NAME build failed"
			exit 1
		fi
		if ! "$FIPTOOL_BIN" info "output/$FIP_COMPAT_NAME.bin" > "output/$FIP_COMPAT_NAME.info.txt"; then
			echo "$FIP_COMPAT_NAME info dump failed"
			exit 1
		fi
		cat > "output/$FIP_COMPAT_NAME.txt" <<EOF
This image is a Z8105AX FIP-only image without the MediaTek FIP checksum entry.
Recommended use: upgrade FIP once from the older U-Boot boot menu that rejects
the MediaTek checksum ToC entry.

It still contains BL31 and BL33/U-Boot, and it keeps the existing BL2 unchanged.
ENETLITE_BUILD_ID=${ENETLITE_BUILD_ID:-}
Z8105AX U-Boot keeps CONFIG_MTK_UPGRADE_FIP_VERIFY enabled and uses a stable
compatible prefix accepted by the older boot menu:

compatible = "mediatek,mt7981", "mediatek,mt7981-rfb", "zbtlink,z8105ax";

Before flashing:
- Verify the image size fits in the target FIP partition.
- Back up the current FIP / Factory / u-boot-env partitions.
- Flash the FIP partition only; do not erase or overwrite BL2.
EOF
		echo "$FIP_COMPAT_NAME build done (old U-Boot menu compatible)"
	fi
	echo "$FIP_NAME build done"
	echo "$FIP_ONLY_NAME build done (keep BL2 / flash FIP only)"
else
	echo "fip build fail!"
	exit 1
fi
if grep -Eq "(^_|CONFIG_TARGET_ALL_NO_SEC_BOOT=y)" "$ATF_CONFIG_ROOT/$ATF_CFG"; then
	if [ -f "$ATF_DIR/build/$SOC/release/bl2.img" ]; then
		BL2_NAME="${SOC}_${BOARD}-bl2"
		cp -f "$ATF_DIR/build/$SOC/release/bl2.img" "output/$BL2_NAME.bin"
		echo "$BL2_NAME build done"
	else
		echo "bl2 build fail!"
		exit 1
	fi
fi

