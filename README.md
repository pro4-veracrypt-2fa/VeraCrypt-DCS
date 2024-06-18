# VeraCrypt-DCS
VeraCrypt EFI Bootloader for EFI Windows system encryption (LGPL)

# Build steps

Make sure to clone our version of the EDK2 repository first:
```bash
git clone https://github.com/pro4-veracrypt-2fa/edk2.git
cd edk2
git checkout UDK2015
git pull
git submodule update --init --recursive
```

Proceed by cloning this repository inside the `edk2` directory:
```bash
git clone https://github.com/pro4-veracrypt-2fa/VeraCrypt-DCS DcsPkg
```

We need to launch a Visual Studio 2022 Developer Command Prompt to build the project.
This can be done by running the `LaunchDevCmd.bat` file in the Visual Studio 2022 installation directory, e.g.
```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\LaunchDevCmd.bat"
```

Then, you need to set the environment variables `EDK_PREFIX` and `EDK_TOOLS_BIN` to the path of the EDK2 repository:
```bat
set EDK_PREFIX=E:\edk2
set EDK_TOOLS_BIN=E:\edk2
```

Furthermore, make sure that you have **exactly** Python 2.7 installed and added to your `PATH` environment variable.
`cx-Freeze` is also required to build the VeraCrypt EFI bootloader.
Make sure to update your `PATH` environment variable to include the path to the `cx-Freeze` scripts, e.g.:
```bat
set PATH=%PATH%;C:\Python27;C:\Python27\Scripts
set PYTHONHOME=C:\Python27
```
NASM is also required.
You can download it from [here](https://www.nasm.us/pub/nasm/releasebuilds/2.16.02/win64/).

After that, you need to set the `NASM_PREFIX` environment variable to the path of the NASM installation:
```bat
set NASM_PREFIX=C:\Users\<user>\AppData\Local\bin\NASM\
```

VeraCrypt's DcsPkg references the `VS100COMNTOOLS` environment variable quite often in their build process.
As we are using Visual Studio 2022, we need to set this variable accordingly:
```bat
set VS100COMNTOOLS=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools
```

We use VS 2022 to build the project.
To do this, you need to modify your `edk2/Conf/target.txt` accordingly:
```
ACTIVE_PLATFORM       = MdeModulePkg/MdeModulePkg.dsc 
TARGET_ARCH           = IA32
TOOL_CHAIN_CONF       = Conf/tools_def.txt
TOOL_CHAIN_TAG        = VS2015x86
```

We then need to modify the `VS2015x86` tag accordingly:
```
DEFINE VS2015x86_BIN    = C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.39.33519\bin\Hostx64\x86
DEFINE VS2015x86_DLL    = C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE;DEF(VS2015x86_BIN)
DEFINE VS2015x86_BINX64 = DEF(VS2015x86_BIN)\..\x86

DEFINE VS2015_BIN      = C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.39.33519\bin\Hostx64\x64
DEFINE VS2015_DLL      = C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE;DEF(VS2015_BIN)
DEFINE VS2015_BINX64   = DEF(VS2015_BIN)\..\x64

...

DEBUG_VS2015_IA32_CC_FLAGS      = /nologo /arch:IA32 /c /WX /GS- /W4 /Gs32768 /D UNICODE /O1b2 /GL /FIAutoGen.h /EHs-c- /GR- /GF /Gy /Zi /Gm

DEBUG_VS2015_IA32_ASM_FLAGS     = /nologo /c /WX /W3 /Cx /coff /Zd /Zi

DEBUG_VS2015_IA32_NASM_FLAGS    = -Ox -f win32 -g

DEBUG_VS2015_IA32_DLINK_FLAGS   = /NOLOGO /NODEFAULTLIB /IGNORE:4001 /OPT:REF /OPT:ICF=10 /MAP /ALIGN:32 /SECTION:.xdata,D /SECTION:.pdata,D /MACHINE:X86 /LTCG /DLL /ENTRY:$(IMAGE_ENTRY_POINT) /SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER /SAFESEH:NO /BASE:0 /DRIVER /DEBUG
```

After that, we can build the BaseTools for EDK2:
```
cd edk2
edksetup.bat ForceRebuild
cd BaseTools
build
cd ..
```

Next, you need to create the required symbolic links required by the build process:
```bat
cd DcsPkg\Library\VeraCryptLib
mklinks_src.bat
```

You need to specify the path to the VeraCrypt source code when tunning this batch file.
This path should be the "src" directory of the VeraCrypt source code.
Make sure you clone this repository into a completely different folder outside the `edk2` development tree.

Finally, we can build the VeraCrypt EFI bootloader:
```	
cd DcsPkg
setenv.bat
Dcs_bld.bat
```

The resulting `VeraCrypt.efi` file can be found in the `edk2\edk2\Build\DcsPkg\DEBUG_VS2015\X64` directory.
