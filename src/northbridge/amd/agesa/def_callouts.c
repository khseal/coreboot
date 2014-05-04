/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2011 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "AGESA.h"
#include "amdlib.h"
#include "Ids.h"
#include "def_callouts.h"

AGESA_STATUS agesa_NoopUnsupported (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
	return AGESA_UNSUPPORTED;
}

AGESA_STATUS agesa_NoopSuccess (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
	return AGESA_SUCCESS;
}

AGESA_STATUS agesa_EmptyIdsInitData (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
	IDS_NV_ITEM *IdsPtr = ((IDS_CALLOUT_STRUCT *) ConfigPtr)->IdsNvPtr;
	if (Data == IDS_CALLOUT_INIT)
		IdsPtr[0].IdsNvValue = IdsPtr[0].IdsNvId = 0xffff;
	return AGESA_SUCCESS;
}

AGESA_STATUS agesa_Reset (UINT32 Func, UINT32 Data, VOID *ConfigPtr)
{
	AGESA_STATUS        Status;
	UINT8                 Value;
	UINTN               ResetType;
	AMD_CONFIG_PARAMS   *StdHeader;

	ResetType = Data;
	StdHeader = ConfigPtr;

	//
	// Perform the RESET based upon the ResetType. In case of
	// WARM_RESET_WHENVER and COLD_RESET_WHENEVER, the request will go to
	// AmdResetManager. During the critical condition, where reset is required
	// immediately, the reset will be invoked directly by writing 0x04 to port
	// 0xCF9 (Reset Port).
	//
	switch (ResetType) {
	case WARM_RESET_WHENEVER:
	case COLD_RESET_WHENEVER:
		break;

	case WARM_RESET_IMMEDIATELY:
	case COLD_RESET_IMMEDIATELY:
		Value = 0x06;
		LibAmdIoWrite (AccessWidth8, 0xCf9, &Value, StdHeader);
		break;

	default:
		break;
	}

	Status = 0;
	return Status;
}
