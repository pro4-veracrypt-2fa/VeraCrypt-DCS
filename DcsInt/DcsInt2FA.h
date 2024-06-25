#ifndef DCSINT_2FA_H
#define DCSINT_2FA_H

#include "DcsInt.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>

#include "common/Tcdefs.h"
#include "BootCommon.h"
#include "DcsVeraCrypt.h"

VOID VC2FAInit(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* SystemTable);
BOOLEAN VCAuth2FA(VOID);

#endif