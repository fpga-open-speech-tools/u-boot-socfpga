// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Intel Corporation <www.intel.com>
 */

#include <common.h>
#include <asm/io.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dm/lists.h>
#include <dm/util.h>
#include <dt-bindings/clock/socfpga-soc64-clock.h>

#include <asm/arch/clock_manager.h>

DECLARE_GLOBAL_DATA_PTR;

struct socfpga_clk_platdata {
	void __iomem *regs;
};

/*
 * function to write the bypass register which requires a poll of the
 * busy bit
 */
static void clk_write_bypass_mainpll(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CM_MAINPLL_REG_BYPASS);
	cm_wait_for_fsm();
}

static void clk_write_bypass_perpll(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CM_PERPLL_REG_BYPASS);
	cm_wait_for_fsm();
}

/* function to write the ctrl register which requires a poll of the busy bit */
static void clk_write_ctrl(struct socfpga_clk_platdata *plat, u32 val)
{
	CM_REG_WRITEL(plat, val, CM_REG_CTRL);
	cm_wait_for_fsm();
}

#define MEMBUS_MAINPLL				0
#define MEMBUS_PERPLL				1
#define MEMBUS_TIMEOUT				1000
#define MEMBUS_ADDR_CLKSLICE			0x27
#define MEMBUS_CLKSLICE_SYNC_MODE_EN		0x80

static int membus_wait_for_req(struct socfpga_clk_platdata *plat, u32 pll,
			       int timeout)
{
	int cnt = 0;
	u32 req_status;

	if (pll == MEMBUS_MAINPLL)
		req_status = CM_REG_READL(plat, CM_MAINPLL_REG_MEM);
	else
		req_status = CM_REG_READL(plat, CM_PERPLL_REG_MEM);

	while ((cnt < timeout) && (req_status & CLKMGR_MEM_REQ_SET_MSK)) {
		if (pll == MEMBUS_MAINPLL)
			req_status = CM_REG_READL(plat, CM_MAINPLL_REG_MEM);
		else
			req_status = CM_REG_READL(plat, CM_PERPLL_REG_MEM);
		cnt++;
	}

	if (cnt >= timeout)
		return -ETIMEDOUT;

	return 0;
}

static int membus_write_pll(struct socfpga_clk_platdata *plat, u32 pll,
			    u32 addr_offset, u32 wdat, int timeout)
{
	u32 addr;
	u32 val;

	addr = ((addr_offset | CLKMGR_MEM_ADDR_START) & CLKMGR_MEM_ADDR_MASK);

	val = (CLKMGR_MEM_REQ_SET_MSK | CLKMGR_MEM_WR_SET_MSK |
	       (wdat << CLKMGR_MEM_WDAT_LSB_OFFSET) | addr);

	if (pll == MEMBUS_MAINPLL)
		CM_REG_WRITEL(plat, val, CM_MAINPLL_REG_MEM);
	else
		CM_REG_WRITEL(plat, val, CM_PERPLL_REG_MEM);

	debug("MEMBUS: Write 0x%08x to addr = 0x%08x\n", wdat, addr);

	return membus_wait_for_req(plat, pll, timeout);
}

static int membus_read_pll(struct socfpga_clk_platdata *plat, u32 pll,
			   u32 addr_offset, u32 *rdata, int timeout)
{
	u32 addr;
	u32 val;

	addr = ((addr_offset | CLKMGR_MEM_ADDR_START) & CLKMGR_MEM_ADDR_MASK);

	val = ((CLKMGR_MEM_REQ_SET_MSK & ~CLKMGR_MEM_WR_SET_MSK) | addr);

	if (pll == MEMBUS_MAINPLL)
		CM_REG_WRITEL(plat, val, CM_MAINPLL_REG_MEM);
	else
		CM_REG_WRITEL(plat, val, CM_PERPLL_REG_MEM);

	*rdata = 0;

	if (membus_wait_for_req(plat, pll, timeout))
		return -ETIMEDOUT;

	if (pll == MEMBUS_MAINPLL)
		*rdata = CM_REG_READL(plat, CM_MAINPLL_REG_MEMSTAT);
	else
		*rdata = CM_REG_READL(plat, CM_PERPLL_REG_MEMSTAT);

	debug("MEMBUS: Read 0x%08x from addr = 0x%08x\n", *rdata, addr);

	return 0;
}

static u32 calc_vocalib_pll(u32 pllm, u32 pllglob)
{
	u32 mdiv, refclkdiv, arefclkdiv, drefclkdiv, mscnt, hscnt, vcocalib;

	mdiv = pllm & CLKMGR_PLLM_MDIV_MASK;
	arefclkdiv = (pllglob & CLKMGR_PLLGLOB_AREFCLKDIV_MASK) >>
		      CLKMGR_PLLGLOB_AREFCLKDIV_OFFSET;
	drefclkdiv = (pllglob & CLKMGR_PLLGLOB_DREFCLKDIV_MASK) >>
		      CLKMGR_PLLGLOB_DREFCLKDIV_OFFSET;
	refclkdiv = (pllglob & CLKMGR_PLLGLOB_REFCLKDIV_MASK) >>
		     CLKMGR_PLLGLOB_REFCLKDIV_OFFSET;
	mscnt = CLKMGR_VCOCALIB_MSCNT_CONST / (mdiv * BIT(drefclkdiv));
	if (!mscnt)
		mscnt = 1;
	hscnt = (mdiv * mscnt * BIT(drefclkdiv) / refclkdiv) -
		CLKMGR_VCOCALIB_HSCNT_CONST;
	vcocalib = (hscnt & CLKMGR_VCOCALIB_HSCNT_MASK) |
		   ((mscnt << CLKMGR_VCOCALIB_MSCNT_OFFSET) &
		     CLKMGR_VCOCALIB_MSCNT_MASK);

	/* Dump all the pll calibration settings for debug purposes */
	debug("mdiv          : %d\n", mdiv);
	debug("arefclkdiv    : %d\n", arefclkdiv);
	debug("drefclkdiv    : %d\n", drefclkdiv);
	debug("refclkdiv     : %d\n", refclkdiv);
	debug("mscnt         : %d\n", mscnt);
	debug("hscnt         : %d\n", hscnt);
	debug("vcocalib      : 0x%08x\n", vcocalib);

	return vcocalib;
}

/*
 * Setup clocks while making no assumptions about previous state of the clocks.
 */
static void clk_basic_init(struct udevice *dev,
			   const struct cm_config * const cfg)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(dev);
	u32 vcocalib;
	u32 rdata;

	if (!cfg)
		return;

	/* Put both PLLs in bypass */
	clk_write_bypass_mainpll(plat, CLKMGR_BYPASS_MAINPLL_ALL);
	clk_write_bypass_perpll(plat, CLKMGR_BYPASS_PERPLL_ALL);

	/* Put both PLLs in Reset and Power Down */
	CM_REG_CLRBITS(plat, CM_MAINPLL_REG_PLLGLOB,
		       CLKMGR_PLLGLOB_PD_MASK | CLKMGR_PLLGLOB_RST_MASK);
	CM_REG_CLRBITS(plat, CM_PERPLL_REG_PLLGLOB,
		       CLKMGR_PLLGLOB_PD_MASK | CLKMGR_PLLGLOB_RST_MASK);

	/* setup main PLL dividers where calculate the vcocalib value */
	vcocalib = calc_vocalib_pll(cfg->main_pll_pllm, cfg->main_pll_pllglob);
	CM_REG_WRITEL(plat, cfg->main_pll_pllglob & ~CLKMGR_PLLGLOB_RST_MASK,
		      CM_MAINPLL_REG_PLLGLOB);
	CM_REG_WRITEL(plat, cfg->main_pll_fdbck, CM_MAINPLL_REG_FDBCK);
	CM_REG_WRITEL(plat, vcocalib, CM_MAINPLL_REG_VCOCALIB);
	CM_REG_WRITEL(plat, cfg->main_pll_pllc0, CM_MAINPLL_REG_PLLC0);
	CM_REG_WRITEL(plat, cfg->main_pll_pllc1, CM_MAINPLL_REG_PLLC1);
	CM_REG_WRITEL(plat, cfg->main_pll_pllc2, CM_MAINPLL_REG_PLLC2);
	CM_REG_WRITEL(plat, cfg->main_pll_pllc3, CM_MAINPLL_REG_PLLC3);
	CM_REG_WRITEL(plat, cfg->main_pll_pllm, CM_MAINPLL_REG_PLLM);
	CM_REG_WRITEL(plat, cfg->main_pll_mpuclk, CM_MAINPLL_REG_MPUCLK);
	CM_REG_WRITEL(plat, cfg->main_pll_nocclk, CM_MAINPLL_REG_NOCCLK);
	CM_REG_WRITEL(plat, cfg->main_pll_nocdiv, CM_MAINPLL_REG_NOCDIV);

	/* setup peripheral PLL dividers where calculate the vcocalib value */
	vcocalib = calc_vocalib_pll(cfg->per_pll_pllm, cfg->per_pll_pllglob);
	CM_REG_WRITEL(plat, cfg->per_pll_pllglob & ~CLKMGR_PLLGLOB_RST_MASK,
		      CM_PERPLL_REG_PLLGLOB);
	CM_REG_WRITEL(plat, cfg->per_pll_fdbck, CM_PERPLL_REG_FDBCK);
	CM_REG_WRITEL(plat, vcocalib, CM_PERPLL_REG_VCOCALIB);
	CM_REG_WRITEL(plat, cfg->per_pll_pllc0, CM_PERPLL_REG_PLLC0);
	CM_REG_WRITEL(plat, cfg->per_pll_pllc1, CM_PERPLL_REG_PLLC1);
	CM_REG_WRITEL(plat, cfg->per_pll_pllc2, CM_PERPLL_REG_PLLC2);
	CM_REG_WRITEL(plat, cfg->per_pll_pllc3, CM_PERPLL_REG_PLLC3);
	CM_REG_WRITEL(plat, cfg->per_pll_pllm, CM_PERPLL_REG_PLLM);
	CM_REG_WRITEL(plat, cfg->per_pll_emacctl, CM_PERPLL_REG_EMACCTL);
	CM_REG_WRITEL(plat, cfg->per_pll_gpiodiv, CM_PERPLL_REG_GPIODIV);

	/* Take both PLL out of reset and power up */
	CM_REG_SETBITS(plat, CM_MAINPLL_REG_PLLGLOB,
		       CLKMGR_PLLGLOB_PD_MASK | CLKMGR_PLLGLOB_RST_MASK);
	CM_REG_SETBITS(plat, CM_PERPLL_REG_PLLGLOB,
		       CLKMGR_PLLGLOB_PD_MASK | CLKMGR_PLLGLOB_RST_MASK);

	/* Membus programming to set mainpll and perripll to
	 * source synchronous mode
	 */
	membus_read_pll(plat, MEMBUS_MAINPLL, MEMBUS_ADDR_CLKSLICE, &rdata,
			MEMBUS_TIMEOUT);
	membus_write_pll(plat, MEMBUS_MAINPLL, MEMBUS_ADDR_CLKSLICE,
			 (rdata | MEMBUS_CLKSLICE_SYNC_MODE_EN),
			 MEMBUS_TIMEOUT);
	membus_read_pll(plat, MEMBUS_PERPLL, MEMBUS_ADDR_CLKSLICE, &rdata,
			MEMBUS_TIMEOUT);
	membus_write_pll(plat, MEMBUS_PERPLL, MEMBUS_ADDR_CLKSLICE,
			 (rdata | MEMBUS_CLKSLICE_SYNC_MODE_EN),
			 MEMBUS_TIMEOUT);

	cm_wait_for_lock(CLKMGR_STAT_ALLPLL_LOCKED_MASK);

	/* Configure ping pong counters in altera group */
	CM_REG_WRITEL(plat, cfg->alt_emacactr, CM_ALTERA_REG_EMACACTR);
	CM_REG_WRITEL(plat, cfg->alt_emacbctr, CM_ALTERA_REG_EMACBCTR);
	CM_REG_WRITEL(plat, cfg->alt_emacptpctr, CM_ALTERA_REG_EMACPTPCTR);
	CM_REG_WRITEL(plat, cfg->alt_gpiodbctr, CM_ALTERA_REG_GPIODBCTR);
	CM_REG_WRITEL(plat, cfg->alt_sdmmcctr, CM_ALTERA_REG_SDMMCCTR);
	CM_REG_WRITEL(plat, cfg->alt_s2fuser0ctr, CM_ALTERA_REG_S2FUSER0CTR);
	CM_REG_WRITEL(plat, cfg->alt_s2fuser1ctr, CM_ALTERA_REG_S2FUSER1CTR);
	CM_REG_WRITEL(plat, cfg->alt_psirefctr, CM_ALTERA_REG_PSIREFCTR);

	CM_REG_WRITEL(plat, CLKMGR_LOSTLOCK_SET_MASK, CM_MAINPLL_REG_LOSTLOCK);
	CM_REG_WRITEL(plat, CLKMGR_LOSTLOCK_SET_MASK, CM_PERPLL_REG_LOSTLOCK);

	CM_REG_WRITEL(plat, CM_REG_READL(plat, CM_MAINPLL_REG_PLLGLOB) |
			CLKMGR_PLLGLOB_CLR_LOSTLOCK_BYPASS_MASK,
			CM_MAINPLL_REG_PLLGLOB);
	CM_REG_WRITEL(plat, CM_REG_READL(plat, CM_PERPLL_REG_PLLGLOB) |
			CLKMGR_PLLGLOB_CLR_LOSTLOCK_BYPASS_MASK,
			CM_PERPLL_REG_PLLGLOB);

	/* Take all PLLs out of bypass */
	clk_write_bypass_mainpll(plat, 0);
	clk_write_bypass_perpll(plat, 0);

	/* Clear the loss of lock bits (write 1 to clear) */
	CM_REG_CLRBITS(plat, CM_REG_INTRCLR,
		       CLKMGR_INTER_PERPLLLOST_MASK |
		       CLKMGR_INTER_MAINPLLLOST_MASK);

	/* Take all ping pong counters out of reset */
	CM_REG_CLRBITS(plat, CM_ALTERA_REG_EXTCNTRST,
		       CLKMGR_ALT_EXTCNTRST_ALLCNTRST);

	/* Out of boot mode */
	clk_write_ctrl(plat,
		       CM_REG_READL(plat, CM_REG_CTRL) & ~CLKMGR_CTRL_BOOTMODE);
}

static u64 clk_get_vco_clk_hz(struct socfpga_clk_platdata *plat,
			      u32 pllglob_reg, u32 pllm_reg)
{
	 u64 fref, arefdiv, mdiv, reg, vco;

	reg = CM_REG_READL(plat, pllglob_reg);

	fref = (reg & CLKMGR_PLLGLOB_VCO_PSRC_MASK) >>
		CLKMGR_PLLGLOB_VCO_PSRC_OFFSET;

	switch (fref) {
	case CLKMGR_VCO_PSRC_EOSC1:
		fref = cm_get_osc_clk_hz();
		break;
	case CLKMGR_VCO_PSRC_INTOSC:
		fref = cm_get_intosc_clk_hz();
		break;
	case CLKMGR_VCO_PSRC_F2S:
		fref = cm_get_fpga_clk_hz();
		break;
	}

	arefdiv = (reg & CLKMGR_PLLGLOB_AREFCLKDIV_MASK) >>
		   CLKMGR_PLLGLOB_AREFCLKDIV_OFFSET;

	mdiv = CM_REG_READL(plat, pllm_reg) & CLKMGR_PLLM_MDIV_MASK;

	vco = fref / arefdiv;
	vco = vco * mdiv;

	return vco;
}

static u64 clk_get_main_vco_clk_hz(struct socfpga_clk_platdata *plat)
{
	return clk_get_vco_clk_hz(plat, CM_MAINPLL_REG_PLLGLOB,
				 CM_MAINPLL_REG_PLLM);
}

static u64 clk_get_per_vco_clk_hz(struct socfpga_clk_platdata *plat)
{
	return clk_get_vco_clk_hz(plat, CM_PERPLL_REG_PLLGLOB,
				 CM_PERPLL_REG_PLLM);
}

static u32 clk_get_5_1_clk_src(struct socfpga_clk_platdata *plat, u64 reg)
{
	u32 clksrc = CM_REG_READL(plat, reg);

	return (clksrc & CLKMGR_CLKSRC_MASK) >> CLKMGR_CLKSRC_OFFSET;
}

static u64 clk_get_clksrc_hz(struct socfpga_clk_platdata *plat, u32 clksrc_reg,
			     u32 main_reg, u32 per_reg)
{
	u64 clock;
	u32 clklsrc = clk_get_5_1_clk_src(plat, clksrc_reg);

	switch (clklsrc) {
	case CLKMGR_CLKSRC_MAIN:
		clock = clk_get_main_vco_clk_hz(plat);
		clock /= (CM_REG_READL(plat, main_reg) &
			  CLKMGR_CLKCNT_MSK);
		break;

	case CLKMGR_CLKSRC_PER:
		clock = clk_get_per_vco_clk_hz(plat);
		clock /= (CM_REG_READL(plat, per_reg) &
			  CLKMGR_CLKCNT_MSK);
		break;

	case CLKMGR_CLKSRC_OSC1:
		clock = cm_get_osc_clk_hz();
		break;

	case CLKMGR_CLKSRC_INTOSC:
		clock = cm_get_intosc_clk_hz();
		break;

	case CLKMGR_CLKSRC_FPGA:
		clock = cm_get_fpga_clk_hz();
		break;
	default:
		return 0;
	}

	return clock;
}

static u64 clk_get_mpu_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_clksrc_hz(plat, CM_MAINPLL_REG_MPUCLK,
				      CM_MAINPLL_REG_PLLC0,
				      CM_PERPLL_REG_PLLC0);

	clock /= 1 + (CM_REG_READL(plat, CM_MAINPLL_REG_MPUCLK) &
		 CLKMGR_CLKCNT_MSK);

	return clock;
}

static u32 clk_get_l3_main_clk_hz(struct socfpga_clk_platdata *plat)
{
	return clk_get_clksrc_hz(plat, CM_MAINPLL_REG_NOCCLK,
				      CM_MAINPLL_REG_PLLC1,
				      CM_PERPLL_REG_PLLC1);
}

static u32 clk_get_l4_main_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CM_MAINPLL_REG_NOCDIV) >>
	      CLKMGR_NOCDIV_L4MAIN_OFFSET) &
	      CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_sdmmc_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_clksrc_hz(plat, CM_ALTERA_REG_SDMMCCTR,
				      CM_MAINPLL_REG_PLLC3,
				      CM_PERPLL_REG_PLLC3);

	clock /= 1 + (CM_REG_READL(plat, CM_ALTERA_REG_SDMMCCTR) &
		 CLKMGR_CLKCNT_MSK);

	return clock / 4;
}

static u32 clk_get_l4_sp_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CM_MAINPLL_REG_NOCDIV) >>
		      CLKMGR_NOCDIV_L4SPCLK_OFFSET) &
		      CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_l4_mp_clk_hz(struct socfpga_clk_platdata *plat)
{
	u64 clock = clk_get_l3_main_clk_hz(plat);

	clock /= BIT((CM_REG_READL(plat, CM_MAINPLL_REG_NOCDIV) >>
		      CLKMGR_NOCDIV_L4MPCLK_OFFSET) &
		      CLKMGR_NOCDIV_DIVIDER_MASK);

	return clock;
}

static u32 clk_get_l4_sys_free_clk_hz(struct socfpga_clk_platdata *plat)
{
	if (CM_REG_READL(plat, CM_REG_STAT) & CLKMGR_STAT_BOOTMODE)
		return clk_get_l3_main_clk_hz(plat) / 2;

	return clk_get_l3_main_clk_hz(plat) / 4;
}

static u32 clk_get_emac_clk_hz(struct socfpga_clk_platdata *plat, u32 emac_id)
{
	bool emacsel_a;
	u32 ctl;
	u32 ctr_reg;
	u32 clock;
	u32 div;
	u32 reg;

	/* Get EMAC clock source */
	ctl = CM_REG_READL(plat, CM_PERPLL_REG_EMACCTL);
	if (emac_id == SOCFPGA_SOC64_EMAC0_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC0SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC0SELB_MASK;
	else if (emac_id == SOCFPGA_SOC64_EMAC1_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC1SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC1SELB_MASK;
	else if (emac_id == SOCFPGA_SOC64_EMAC2_CLK)
		ctl = (ctl >> CLKMGR_PERPLLGRP_EMACCTL_EMAC2SELB_OFFSET) &
		       CLKMGR_PERPLLGRP_EMACCTL_EMAC2SELB_MASK;
	else
		return 0;

	if (ctl) {
		/* EMAC B source */
		emacsel_a = false;
		ctr_reg = CM_ALTERA_REG_EMACBCTR;
	} else {
		/* EMAC A source */
		emacsel_a = true;
		ctr_reg = CM_ALTERA_REG_EMACACTR;
	}

	reg = CM_REG_READL(plat, ctr_reg);
	clock = (reg & CLKMGR_ALT_EMACCTR_SRC_MASK)
		 >> CLKMGR_ALT_EMACCTR_SRC_OFFSET;
	div = (reg & CLKMGR_ALT_EMACCTR_CNT_MASK)
		>> CLKMGR_ALT_EMACCTR_CNT_OFFSET;

	switch (clock) {
	case CLKMGR_CLKSRC_MAIN:
		clock = clk_get_main_vco_clk_hz(plat);
		if (emacsel_a) {
			clock /= (CM_REG_READL(plat, CM_MAINPLL_REG_PLLC2) &
				  CLKMGR_CLKCNT_MSK);
		} else {
			clock /= (CM_REG_READL(plat, CM_MAINPLL_REG_PLLC3) &
				  CLKMGR_CLKCNT_MSK);
		}
		break;

	case CLKMGR_CLKSRC_PER:
		clock = clk_get_per_vco_clk_hz(plat);
		if (emacsel_a) {
			clock /= (CM_REG_READL(plat, CM_PERPLL_REG_PLLC2) &
				  CLKMGR_CLKCNT_MSK);
		} else {
			clock /= (CM_REG_READL(plat, CM_PERPLL_REG_PLLC3) &
				  CLKMGR_CLKCNT_MSK);
		}
		break;

	case CLKMGR_CLKSRC_OSC1:
		clock = cm_get_osc_clk_hz();
		break;

	case CLKMGR_CLKSRC_INTOSC:
		clock = cm_get_intosc_clk_hz();
		break;

	case CLKMGR_CLKSRC_FPGA:
		clock = cm_get_fpga_clk_hz();
		break;
	}

	clock /= 1 + div;

	return clock;
}

static ulong socfpga_clk_get_rate(struct clk *clk)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(clk->dev);

	switch (clk->id) {
	case SOCFPGA_SOC64_MPU_CLK:
		return clk_get_mpu_clk_hz(plat);
	case SOCFPGA_SOC64_L4_MAIN_CLK:
		return clk_get_l4_main_clk_hz(plat);
	case SOCFPGA_SOC64_L4_SYS_FREE_CLK:
		return clk_get_l4_sys_free_clk_hz(plat);
	case SOCFPGA_SOC64_L4_MP_CLK:
		return clk_get_l4_mp_clk_hz(plat);
	case SOCFPGA_SOC64_L4_SP_CLK:
		return clk_get_l4_sp_clk_hz(plat);
	case SOCFPGA_SOC64_SDMMC_CLK:
		return clk_get_sdmmc_clk_hz(plat);
	case SOCFPGA_SOC64_EMAC0_CLK:
	case SOCFPGA_SOC64_EMAC1_CLK:
	case SOCFPGA_SOC64_EMAC2_CLK:
		return clk_get_emac_clk_hz(plat, clk->id);
	case SOCFPGA_SOC64_USB_CLK:
		return clk_get_l4_mp_clk_hz(plat);
	default:
		return -ENXIO;
	}
}

static int socfpga_clk_probe(struct udevice *dev)
{
	const struct cm_config *cm_default_cfg = cm_get_default_config();

	clk_basic_init(dev, cm_default_cfg);

	return 0;
}

static int socfpga_clk_ofdata_to_platdata(struct udevice *dev)
{
	struct socfpga_clk_platdata *plat = dev_get_platdata(dev);
	fdt_addr_t addr;

	addr = devfdt_get_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	plat->regs = (void __iomem *)addr;

	return 0;
}

static struct clk_ops socfpga_clk_ops = {
	.get_rate	= socfpga_clk_get_rate,
};

static const struct udevice_id socfpga_clk_match[] = {
	{ .compatible = "intel,agilex-clkmgr" },
	{}
};

U_BOOT_DRIVER(socfpga_agilex_clk) = {
	.name		= "clk-agilex",
	.id		= UCLASS_CLK,
	.of_match	= socfpga_clk_match,
	.ops		= &socfpga_clk_ops,
	.probe		= socfpga_clk_probe,
	.ofdata_to_platdata = socfpga_clk_ofdata_to_platdata,
	.platdata_auto_alloc_size = sizeof(struct socfpga_clk_platdata),
};
