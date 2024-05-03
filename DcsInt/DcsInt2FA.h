#ifndef __DCSINT_2FA_H__
#define __DCSINT_2FA_H__

#include "DcsInt.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>

#include "common/Tcdefs.h"
#include "BootCommon.h"
#include "DcsVeraCrypt.h"

VOID VC2FAInit(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* SystemTable);
VOID VCAuth2FASampleRequest(VOID);

#endif
