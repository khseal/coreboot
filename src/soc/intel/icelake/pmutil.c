/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Helper functions for dealing with power management registers
 * and the differences between PCH variants.
 */

#define __SIMPLE_DEVICE__

#include <device/mmio.h>
#include <cbmem.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <console/console.h>
#include <intelblocks/pmclib.h>
#include <intelblocks/rtc.h>
#include <intelblocks/tco.h>
#include <stdlib.h>
#include <soc/espi.h>
#include <soc/gpe.h>
#include <soc/gpio.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/pm.h>
#include <soc/smbus.h>
#include <security/vboot/vbnv.h>

#include "chip.h"

/*
 * SMI
 */

const char *const *soc_smi_sts_array(size_t *a)
{
	static const char *const smi_sts_bits[] = {
		[BIOS_STS_BIT] = "BIOS",
		[LEGACY_USB_STS_BIT] = "LEGACY_USB",
		[SMI_ON_SLP_EN_STS_BIT] = "SLP_SMI",
		[APM_STS_BIT] = "APM",
		[SWSMI_TMR_STS_BIT] = "SWSMI_TMR",
		[PM1_STS_BIT] = "PM1",
		[GPE0_STS_BIT] = "GPE0",
		[GPIO_STS_BIT] = "GPI",
		[MCSMI_STS_BIT] = "MCSMI",
		[DEVMON_STS_BIT] = "DEVMON",
		[TCO_STS_BIT] = "TCO",
		[PERIODIC_STS_BIT] = "PERIODIC",
		[SERIRQ_SMI_STS_BIT] = "SERIRQ_SMI",
		[SMBUS_SMI_STS_BIT] = "SMBUS_SMI",
		[PCI_EXP_SMI_STS_BIT] = "PCI_EXP_SMI",
		[MONITOR_STS_BIT] = "MONITOR",
		[SPI_SMI_STS_BIT] = "SPI",
		[GPIO_UNLOCK_SMI_STS_BIT] = "GPIO_UNLOCK",
		[ESPI_SMI_STS_BIT] = "ESPI_SMI",
	};

	*a = ARRAY_SIZE(smi_sts_bits);
	return smi_sts_bits;
}

/*
 * TCO
 */

const char *const *soc_tco_sts_array(size_t *a)
{
	static const char *const tco_sts_bits[] = {
		[0] = "NMI2SMI",
		[1] = "SW_TCO",
		[2] = "TCO_INT",
		[3] = "TIMEOUT",
		[7] = "NEWCENTURY",
		[8] = "BIOSWR",
		[9] = "DMISCI",
		[10] = "DMISMI",
		[12] = "DMISERR",
		[13] = "SLVSEL",
		[16] = "INTRD_DET",
		[17] = "SECOND_TO",
		[18] = "BOOT",
		[20] = "SMLINK_SLV"
	};

	*a = ARRAY_SIZE(tco_sts_bits);
	return tco_sts_bits;
}

/*
 * GPE0
 */

const char *const *soc_std_gpe_sts_array(size_t *a)
{
	static const char *const gpe_sts_bits[] = {
		[1] = "HOTPLUG",
		[2] = "SWGPE",
		[6] = "TCO_SCI",
		[7] = "SMB_WAK",
		[9] = "PCI_EXP",
		[10] = "BATLOW",
		[11] = "PME",
		[12] = "ME",
		[13] = "PME_B0",
		[14] = "eSPI",
		[15] = "GPIO Tier-2",
		[16] = "LAN_WAKE",
		[18] = "WADT"
	};

	*a = ARRAY_SIZE(gpe_sts_bits);
	return gpe_sts_bits;
}

void pmc_set_disb(void)
{
	/* Set the DISB after DRAM init */
	uint8_t disb_val;
	/* Only care about bits [23:16] of register GEN_PMCON_A */
	uint8_t *addr = (void *)(pmc_mmio_regs() + GEN_PMCON_A + 2);

	disb_val = read8(addr);
	disb_val |= (DISB >> 16);

	/* Don't clear bits that are write-1-to-clear */
	disb_val &= ~((MS4V | SUS_PWR_FLR) >> 16);
	write8(addr, disb_val);
}

void pmc_clear_pmcon_sts(void)
{
	uint32_t reg_val;
	uint8_t *addr;
	addr = pmc_mmio_regs();

	reg_val = read32(addr + GEN_PMCON_A);
	/* Clear SUS_PWR_FLR, GBL_RST_STS, HOST_RST_STS, PWR_FLR bits
	 * while retaining MS4V write-1-to-clear bit */
	reg_val &= ~(MS4V);

	write32((addr + GEN_PMCON_A), reg_val);
}

/*
 * PMC controller gets hidden from PCI bus
 * during FSP-Silicon init call. Hence PWRMBASE
 * can't be accessible using PCI configuration space
 * read/write.
 */
uint8_t *pmc_mmio_regs(void)
{
	return (void *)(uintptr_t)PCH_PWRM_BASE_ADDRESS;
}

uintptr_t soc_read_pmc_base(void)
{
	return (uintptr_t)pmc_mmio_regs();
}

void soc_get_gpi_gpe_configs(uint8_t *dw0, uint8_t *dw1, uint8_t *dw2)
{
	DEVTREE_CONST struct soc_intel_icelake_config *config;

	/* Look up the device in devicetree */
	DEVTREE_CONST struct device *dev = dev_find_slot(0, PCH_DEVFN_PMC);
	if (!dev || !dev->chip_info) {
		printk(BIOS_ERR, "BUG! Could not find SOC devicetree config\n");
		return;
	}
	config = dev->chip_info;

	/* Assign to out variable */
	*dw0 = config->gpe0_dw0;
	*dw1 = config->gpe0_dw1;
	*dw2 = config->gpe0_dw2;
}

static int rtc_failed(uint32_t gen_pmcon_b)
{
	return !!(gen_pmcon_b & RTC_BATTERY_DEAD);
}

int soc_get_rtc_failed(void)
{
	const struct chipset_power_state *ps = cbmem_find(CBMEM_ID_POWER_STATE);

	if (!ps) {
		printk(BIOS_ERR, "Could not find power state in cbmem, RTC init aborted\n");
		return 1;
	}

	return rtc_failed(ps->gen_pmcon_b);
}

int vbnv_cmos_failed(void)
{
	return rtc_failed(read32(pmc_mmio_regs() + GEN_PMCON_B));
}

static inline int deep_s3_enabled(void)
{
	uint32_t deep_s3_pol;

	deep_s3_pol = read32(pmc_mmio_regs() + S3_PWRGATE_POL);
	return !!(deep_s3_pol & (S3DC_GATE_SUS | S3AC_GATE_SUS));
}

/* Return 0, 3, or 5 to indicate the previous sleep state. */
int soc_prev_sleep_state(const struct chipset_power_state *ps,
	int prev_sleep_state)
{

	/*
	 * Check for any power failure to determine if this a wake from
	* S5 because the PCH does not set the WAK_STS bit when waking
	* from a true G3 state.
	*/
	if (ps->gen_pmcon_a & (PWR_FLR | SUS_PWR_FLR))
		prev_sleep_state = ACPI_S5;

	/*
	 * If waking from S3 determine if deep S3 is enabled. If not,
	 * need to check both deep sleep well and normal suspend well.
	 * Otherwise just check deep sleep well.
	 */
	if (prev_sleep_state == ACPI_S3) {
		/* PWR_FLR represents deep sleep power well loss. */
		uint32_t mask = PWR_FLR;

		/* If deep s3 isn't enabled check the suspend well too. */
		if (!deep_s3_enabled())
			mask |= SUS_PWR_FLR;

		if (ps->gen_pmcon_a & mask)
			prev_sleep_state = ACPI_S5;
	}

	return prev_sleep_state;
}

void soc_fill_power_state(struct chipset_power_state *ps)
{
	uint8_t *pmc;

	ps->tco1_sts = tco_read_reg(TCO1_STS);
	ps->tco2_sts = tco_read_reg(TCO2_STS);

	printk(BIOS_DEBUG, "TCO_STS:   %04x %04x\n",
	ps->tco1_sts, ps->tco2_sts);

	pmc = pmc_mmio_regs();
	ps->gen_pmcon_a = read32(pmc + GEN_PMCON_A);
	ps->gen_pmcon_b = read32(pmc + GEN_PMCON_B);
	ps->gblrst_cause[0] = read32(pmc + GBLRST_CAUSE0);
	ps->gblrst_cause[1] = read32(pmc + GBLRST_CAUSE1);

	printk(BIOS_DEBUG, "GEN_PMCON: %08x %08x\n",
		ps->gen_pmcon_a, ps->gen_pmcon_b);

	printk(BIOS_DEBUG, "GBLRST_CAUSE: %08x %08x\n",
		ps->gblrst_cause[0], ps->gblrst_cause[1]);
}
