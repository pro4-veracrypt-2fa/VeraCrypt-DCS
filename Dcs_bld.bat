pushd "%~dp0"

set dcsarch=X64
set dcstype=DEBUG
set dcsbldtoolset=VS2015
set dcspkgpath="%~dp0DcsPkg.dsc"

if /I NOT  ["%1"]==["X64Rel"] goto :IA32
set dcsarch=X64
set dcstype=RELEASE
goto :bld

:IA32
if /I NOT ["%1"]==["IA32"] goto :IA32Rel
set dcsarch=IA32
set dcstype=DEBUG
goto :bld

:IA32Rel
if /I NOT  ["%1"]==["IA32rel"] goto :bld
set dcsarch=IA32
set dcstype=RELEASE

:bld
if /I ["%2"]==["VS2015"] set dcsbldtoolset=VS2015x86
@ REM call bld.bat -t %dcsbldtoolset% -DSECURE_BOOT_ENABLE=1 -p %dcspkgpath% -b %dcstype% -a %dcsarch%
call bld.bat -t %dcsbldtoolset% -p %dcspkgpath% -b %dcstype% -a %dcsarch% -D NETWORK_HTTP_BOOT_ENABLE -D NETWORK_TLS_ENABLE

if ERRORLEVEL 1 goto :exit
@REM if not exist SecureBoot\keys\DCS_sign.pfx goto :exit

@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsBml.efi        SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt 
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsBoot.efi       SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsCfg.efi        SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsInt.efi        SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsRe.efi         SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\DcsInfo.efi       SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt
@REM call SecureBoot\efi_sign.bat ..\Build\DcsPkg\%dcstype%_%dcsbldtoolset%\%dcsarch%\LegacySpeaker.efi SecureBoot\keys\DCS_sign.pfx SecureBoot\certs\DCS_sign.crt

:exit
popd
