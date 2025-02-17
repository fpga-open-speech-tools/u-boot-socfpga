/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Intel Corporation <www.intel.com>
 *
 */

#ifndef __CONFIG_SOCFGPA_AGILEX_H__
#define __CONFIG_SOCFGPA_AGILEX_H__

#include <asm/arch/base_addr_s10.h>
#include <asm/arch/handoff_s10.h>

/*
 * U-Boot general configurations
 */
#define CONFIG_SYS_MONITOR_BASE		CONFIG_SYS_TEXT_BASE
#define CONFIG_LOADADDR			0x2000000
#define CONFIG_SYS_LOAD_ADDR		CONFIG_LOADADDR
#define CONFIG_REMAKE_ELF
/* sysmgr.boot_scratch_cold4 & 5 (64bit) will be used for PSCI_CPU_ON call */
#define CPU_RELEASE_ADDR		0xFFD12210
/*
 * sysmgr.boot_scratch_cold6 & 7 (64bit) will be used by master CPU to
 * store its VBAR_EL3 value. Other slave CPUs will read from this
 * location and update their VBAR_EL3 respectively
 */
#define VBAR_EL3_BASE_ADDR		0xFFD12218
/*
 * Share sysmgr.boot_scratch_cold6 & 7 (64bit) with VBAR_LE3_BASE_ADDR
 * Indicate L2 reset is done. HPS should trigger warm reset via RMR_EL3.
 */
#define L2_RESET_DONE_REG		0xFFD12218
/* Magic word to indicate L2 reset is completed */
#define L2_RESET_DONE_STATUS		0x1228E5E7
#define CONFIG_SYS_CACHELINE_SIZE	64
#define CONFIG_SYS_MEM_RESERVE_SECURE	0	/* using OCRAM, not DDR */

/*
 * U-Boot console configurations
 */
#define CONFIG_SYS_MAXARGS		64
#define CONFIG_SYS_CBSIZE		2048
#define CONFIG_SYS_PBSIZE		(CONFIG_SYS_CBSIZE + \
					sizeof(CONFIG_SYS_PROMPT) + 16)
#define CONFIG_SYS_BARGSIZE		CONFIG_SYS_CBSIZE

/* Extend size of kernel image for uncompression */
#define CONFIG_SYS_BOOTM_LEN		(32 * 1024 * 1024)

/*
 * U-Boot run time memory configurations
 */
#define CONFIG_SYS_INIT_RAM_ADDR	0xFFE00000
#define CONFIG_SYS_INIT_RAM_SIZE	0x40000
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_INIT_RAM_ADDR  \
					+ CONFIG_SYS_INIT_RAM_SIZE \
					- S10_HANDOFF_SIZE)
#define CONFIG_SYS_INIT_SP_OFFSET	(CONFIG_SYS_INIT_SP_ADDR)
#define CONFIG_SYS_MALLOC_LEN		(5 * 1024 * 1024)

/*
 * U-Boot environment configurations
 */
#define CONFIG_ENV_SIZE			0x1000
#define CONFIG_SYS_MMC_ENV_DEV		0	/* device 0 */
#define CONFIG_ENV_OFFSET		512	/* just after the MBR */

/*
 * QSPI support
 */
 #ifdef CONFIG_CADENCE_QSPI
/* Enable it if you want to use dual-stacked mode */
/*#define CONFIG_QSPI_RBF_ADDR		0x720000*/

/* Flash device info */

/*#define CONFIG_ENV_IS_IN_SPI_FLASH*/
#ifdef CONFIG_ENV_IS_IN_SPI_FLASH
#undef CONFIG_ENV_OFFSET
#undef CONFIG_ENV_SIZE
#define CONFIG_ENV_OFFSET		0x02080000
#define CONFIG_ENV_SIZE			(64 * 1024)
#define CONFIG_ENV_SECT_SIZE		(64 * 1024)
#endif /* CONFIG_ENV_IS_IN_SPI_FLASH */

#ifndef CONFIG_SPL_BUILD
#define CONFIG_MTD_DEVICE
#define CONFIG_MTD_PARTITIONS
#define MTDIDS_DEFAULT			"nor0=ff705000.spi.0"
#endif /* CONFIG_SPL_BUILD */

#ifndef __ASSEMBLY__
unsigned int cm_get_qspi_controller_clk_hz(void);
#define CONFIG_CQSPI_REF_CLK		cm_get_qspi_controller_clk_hz()
#endif

#endif /* CONFIG_CADENCE_QSPI */

/*
 * Boot arguments passed to the boot command. The value of
 * CONFIG_BOOTARGS goes into the environment value "bootargs".
 * Do note the value will override also the chosen node in FDT blob.
 */
#define CONFIG_BOOTARGS "earlycon"
#ifdef CONFIG_QSPI_BOOT
#define CONFIG_BOOTCOMMAND "sf probe; run qspiload;run linux_qspi_enable;" \
			   "rsu dtb; run qspiboot"
#else
#define CONFIG_BOOTCOMMAND "run fatscript; run mmcload;run linux_qspi_enable;" \
			   "run mmcboot"
#endif

#define CONFIG_EXTRA_ENV_SETTINGS \
	"qspibootimageaddr=0x020B0000\0" \
	"qspifdtaddr=0x02090000\0" \
	"bootimagesize=0x01400000\0" \
	"fdtimagesize=0x00010000\0" \
	"qspiload=sf read ${loadaddr} ${qspibootimageaddr} ${bootimagesize};" \
		"sf read ${fdt_addr} ${qspifdtaddr} ${fdtimagesize}\0" \
	"qspiboot=setenv bootargs earlycon root=/dev/mtdblock1 rw " \
		"rootfstype=jffs2 rootwait;booti ${loadaddr} - ${fdt_addr}\0" \
	"loadaddr=" __stringify(CONFIG_SYS_LOAD_ADDR) "\0" \
	"bootfile=Image\0" \
	"fdt_addr=8000000\0" \
	"fdtimage=socfpga_agilex_socdk.dtb\0" \
	"mmcroot=/dev/mmcblk0p2\0" \
	"mmcboot=setenv bootargs " CONFIG_BOOTARGS \
		" root=${mmcroot} rw rootwait;" \
		"booti ${loadaddr} - ${fdt_addr}\0" \
	"mmcload=mmc rescan;" \
		"load mmc 0:1 ${loadaddr} ${bootfile};" \
		"load mmc 0:1 ${fdt_addr} ${fdtimage}\0" \
	"linux_qspi_enable=if sf probe; then " \
		"echo Enabling QSPI at Linux DTB...;" \
		"fdt addr ${fdt_addr}; fdt resize;" \
		"fdt set /soc/spi@ff8d2000 status okay;" \
		"fdt set /soc/clkmgr/clocks/qspi_clk clock-frequency " \
		" ${qspi_clock}; fi; \0" \
	"scriptaddr=0x02100000\0" \
	"scriptfile=u-boot.scr\0" \
	"fatscript=if fatload mmc 0:1 ${scriptaddr} ${scriptfile};" \
		   "then source ${scriptaddr}; fi\0"

/*
 * Generic Interrupt Controller Definitions
 */
#define CONFIG_GICV2

/*
 * External memory configurations
 */
#define PHYS_SDRAM_1			0x0
#define PHYS_SDRAM_1_SIZE		(1 * 1024 * 1024 * 1024)
#define CONFIG_SYS_SDRAM_BASE		0
#define CONFIG_SYS_MEMTEST_START	0
#define CONFIG_SYS_MEMTEST_END		PHYS_SDRAM_1_SIZE - 0x200000

/*
 * Serial / UART configurations
 */
#define CONFIG_SYS_NS16550_CLK		100000000
#define CONFIG_SYS_NS16550_MEM32

/*
 * Timer & watchdog configurations
 */
#define COUNTER_FREQUENCY		400000000

/*
 * SDMMC configurations
 */
#ifdef CONFIG_CMD_MMC
#define CONFIG_SYS_MMC_MAX_BLK_COUNT	256
#endif
/*
 * Flash configurations
 */
#define CONFIG_SYS_MAX_FLASH_BANKS	1

/* Ethernet on SoC (EMAC) */
#if defined(CONFIG_CMD_NET)
#define CONFIG_DW_ALTDESCRIPTOR
#endif /* CONFIG_CMD_NET */

/*
 * L4 Watchdog
 */
#ifdef CONFIG_SPL_BUILD
#define CONFIG_HW_WATCHDOG
#define CONFIG_DESIGNWARE_WATCHDOG
#define CONFIG_DW_WDT_BASE		SOCFPGA_L4WD0_ADDRESS
#ifndef __ASSEMBLY__
#define CONFIG_DW_WDT_CLOCK_KHZ		100000
#endif
#define CONFIG_WATCHDOG_TIMEOUT_MSECS	3000
#endif

/*
 * SPL memory layout
 *
 * On chip RAM
 * 0xFFE0_0000 ...... Start of OCRAM
 * SPL code, rwdata
 * empty space
 * 0xFFEx_xxxx ...... Top of stack (grows down)
 * 0xFFEy_yyyy ...... Global Data
 * 0xFFEz_zzzz ...... Malloc prior relocation (size CONFIG_SYS_MALLOC_F_LEN)
 * 0xFFE3_F000 ...... Hardware handdoff blob (size 4KB)
 * 0xFFE3_FFFF ...... End of OCRAM
 *
 * SDRAM
 * 0x0000_0000 ...... Start of SDRAM_1
 * unused / empty space for image loading
 * Size 64MB   ...... MALLOC (size CONFIG_SYS_SPL_MALLOC_SIZE)
 * Size 1MB    ...... BSS (size CONFIG_SPL_BSS_MAX_SIZE)
 * 0x8000_0000 ...... End of SDRAM_1 (assume 2GB)
 *
 */
#define CONFIG_SPL_TARGET		"spl/u-boot-spl-dtb.hex"
#define CONFIG_SPL_TEXT_BASE		CONFIG_SYS_INIT_RAM_ADDR
#define CONFIG_SPL_MAX_SIZE		CONFIG_SYS_INIT_RAM_SIZE
#define CONFIG_SPL_STACK		CONFIG_SYS_INIT_SP_ADDR
#define CONFIG_SPL_BSS_MAX_SIZE		0x100000	/* 1 MB */
#define CONFIG_SPL_BSS_START_ADDR	(PHYS_SDRAM_1 + PHYS_SDRAM_1_SIZE \
					- CONFIG_SPL_BSS_MAX_SIZE)
#define CONFIG_SYS_SPL_MALLOC_SIZE	(CONFIG_SYS_MALLOC_LEN)
#define CONFIG_SYS_SPL_MALLOC_START	(CONFIG_SPL_BSS_START_ADDR \
					- CONFIG_SYS_SPL_MALLOC_SIZE)
#define CONFIG_SYS_SPI_U_BOOT_OFFS      0x02000000

/* SPL SDMMC boot support */
#define CONFIG_SYS_MMCSD_FS_BOOT_PARTITION	1
#define CONFIG_SPL_FS_LOAD_PAYLOAD_NAME		"u-boot.img"

#endif	/* __CONFIG_H */
