/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2008-2009 coresystems GmbH
 * Copyright (C) 2014 Google Inc.
 * Copyright (C) 2017 Intel Corporation.
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

#include <console/console.h>
#include <device/pci_def.h>
#include <intelblocks/fast_spi.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <intelblocks/smihandler.h>
#include <soc/p2sb.h>
#include <soc/pci_devs.h>
#include <soc/pcr_ids.h>
#include <soc/pm.h>

#include "chip.h"

#define CSME0_FBE	0xf
#define CSME0_BAR	0x0
#define CSME0_FID	0xb0

const struct smm_save_state_ops *get_smm_save_state_ops(void)
{
	return &em64t101_smm_ops;
}

static void pch_disable_heci(void)
{
	struct pcr_sbi_msg msg = {
		.pid = PID_CSME0,
		.offset = 0,
		.opcode = PCR_WRITE,
		.is_posted = false,
		.fast_byte_enable = CSME0_FBE,
		.bar = CSME0_BAR,
		.fid = CSME0_FID
	};
	/* Bit 0: Set to make HECI#1 Function disable */
	uint32_t data32 = 1;
	uint8_t response;
	int status;

	/* unhide p2sb device */
	p2sb_unhide();

	/* Send SBI command to make HECI#1 function disable */
	status = pcr_execute_sideband_msg(&msg, &data32, &response);
	if (status && response)
		printk(BIOS_ERR, "Fail to make CSME function disable\n");

	/* Ensure to Lock SBI interface after this command */
	p2sb_disable_sideband_access();

	/* hide p2sb device */
	p2sb_hide();
}

/*
 * Specific SOC SMI handler during ramstage finalize phase
 *
 * BIOS can't make CSME function disable as is due to POSTBOOT_SAI
 * restriction in place from CNP chipset. Hence create SMI Handler to
 * perform CSME function disabling logic during SMM mode.
 */
void smihandler_soc_at_finalize(void)
{
	const struct soc_intel_cannonlake_config *config;
	const struct device *dev = dev_find_slot(0, PCH_DEVFN_CSE);

	if (!dev || !dev->chip_info) {
		printk(BIOS_ERR, "%s: Could not find SoC devicetree config!\n",
		       __func__);
		return ;
	}

	config = dev->chip_info;

	if (!config->HeciEnabled && CONFIG(HECI_DISABLE_USING_SMM))
		pch_disable_heci();
}

void smihandler_soc_check_illegal_access(uint32_t tco_sts)
{
	if (!((tco_sts & (1 << 8)) && CONFIG(SPI_FLASH_SMM)
			&& fast_spi_wpd_status()))
		return;

	/*
	 * BWE is RW, so the SMI was caused by a
	 * write to BWE, not by a write to the BIOS
	 *
	 * This is the place where we notice someone
	 * is trying to tinker with the BIOS. We are
	 * trying to be nice and just ignore it. A more
	 * resolute answer would be to power down the
	 * box.
	 */
	printk(BIOS_DEBUG, "Switching back to RO\n");
	fast_spi_enable_wp();
}

/* SMI handlers that should be serviced in SCI mode too. */
uint32_t smihandler_soc_get_sci_mask(void)
{
	uint32_t sci_mask =
		SMI_HANDLER_SCI_EN(APM_STS_BIT) |
		SMI_HANDLER_SCI_EN(SMI_ON_SLP_EN_STS_BIT);

	return sci_mask;
}

const smi_handler_t southbridge_smi[SMI_STS_BITS] = {
	[SMI_ON_SLP_EN_STS_BIT] = smihandler_southbridge_sleep,
	[APM_STS_BIT] = smihandler_southbridge_apmc,
	[PM1_STS_BIT] = smihandler_southbridge_pm1,
	[GPE0_STS_BIT] = smihandler_southbridge_gpe0,
	[GPIO_STS_BIT] = smihandler_southbridge_gpi,
	[ESPI_SMI_STS_BIT] = smihandler_southbridge_espi,
	[MCSMI_STS_BIT] = smihandler_southbridge_mc,
	[TCO_STS_BIT] = smihandler_southbridge_tco,
	[PERIODIC_STS_BIT] = smihandler_southbridge_periodic,
	[MONITOR_STS_BIT] = smihandler_southbridge_monitor,
};
