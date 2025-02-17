/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2015-2019 Altera Corporation <www.altera.com>
 */

#ifndef __CONFIG_SOCFGPA_ARRIA10_H__
#define __CONFIG_SOCFGPA_ARRIA10_H__

#include <asm/arch/base_addr_a10.h>

/* Booting Linux */
#define CONFIG_LOADADDR		0x01000000
#define CONFIG_SYS_LOAD_ADDR	CONFIG_LOADADDR

#define CONFIG_SYS_BOOTM_LEN	(32 * 1024 * 1024)

/*
 * U-Boot general configurations
 */

/* Memory configurations  */
#define PHYS_SDRAM_1_SIZE		0x40000000

/* Ethernet on SoC (EMAC) */

/*
 * U-Boot environment configurations
 */

/*
 * Serial / UART configurations
 */
#define CONFIG_SYS_NS16550_MEM32
#define CONFIG_SYS_BAUDRATE_TABLE {4800, 9600, 19200, 38400, 57600, 115200}

/*
 * L4 OSC1 Timer 0
 */
/* reload value when timer count to zero */
#define TIMER_LOAD_VAL			0xFFFFFFFF

/*
 * Flash configurations
 */
#define CONFIG_SYS_MAX_FLASH_BANKS     1

/* SPL memory allocation configuration, this is for FAT implementation */
#define CONFIG_SYS_SPL_MALLOC_SIZE	0x00015000

#if defined(CONFIG_QSPI_BOOT) || defined(CONFIG_NAND_BOOT)
#define CONFIG_EXTRA_ENV_SETTINGS \
	"fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
	"bootm_size=0xa000000\0" \
	"kernel_addr_r="__stringify(CONFIG_SYS_LOAD_ADDR)"\0" \
	"fdt_addr_r=0x02000000\0" \
	"scriptaddr=0x02100000\0" \
	"pxefile_addr_r=0x02200000\0" \
	"ramdisk_addr_r=0x02300000\0" \
	"socfpga_legacy_reset_compat=1\0" \
	"kernelfit_addr=0x1200000\0" \
	"fitimagesize=0x5F0000\0" \
	"qspiroot=/dev/mtdblock1\0" \
	"qspirootfstype=jffs2\0" \
	"qspiload=sf probe; sf read ${scriptaddr} ${kernelfit_addr}\0" \
	"qspiboot=setenv bootargs " CONFIG_BOOTARGS \
			"root=${qspiroot} rw rootfstype=${qspirootfstype}; " \
			"bootm ${scriptaddr}\0" \
	"nandroot=/dev/mtdblock1\0" \
	"nandrootfstype=jffs2\0" \
	"nandload=nand read ${scriptaddr} ${kernelfit_addr}\0" \
	"nandboot=setenv bootargs " CONFIG_BOOTARGS \
			"root=${nandroot} rw rootfstype=${nandrootfstype}; " \
			"bootm ${scriptaddr}\0" \

#endif

/* The rest of the configuration is shared */
#include <configs/socfpga_common.h>

/*
 * L4 Watchdog
 */
#ifdef CONFIG_HW_WATCHDOG
#undef CONFIG_DW_WDT_BASE
#define CONFIG_DW_WDT_BASE		SOCFPGA_L4WD1_ADDRESS
#endif

#if defined(CONFIG_QSPI_BOOT)
#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND "run qspiload; run qspiboot"
#endif

#ifdef CONFIG_NAND_DENALI_DT
#define CONFIG_SYS_NAND_BAD_BLOCK_POS        0x0
#endif

#if defined(CONFIG_NAND_BOOT)
#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND "run nandload; run nandboot"
#endif

#endif	/* __CONFIG_SOCFGPA_ARRIA10_H__ */
