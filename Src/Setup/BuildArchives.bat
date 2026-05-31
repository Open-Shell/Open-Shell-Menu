@echo off
set PATH=C:\Program Files\7-Zip\;%PATH%
cd %~dp0

call _Version.bat

REM ********* Build MSI Checksums
echo --- MSI Checksums
..\..\build\bin\Release\Utility.exe crcmsi Temp
@if ERRORLEVEL 1 exit /b 1

REM ********* Build bootstrapper
echo --- Bootstrapper
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set MSBuildDir=%%i\MSBuild\Current\Bin\

"%MSBuildDir%MSBuild.exe" Setup.sln /m /t:Rebuild /p:Configuration="Release" /p:Platform="Win32" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1


if exist Final rd /Q /S Final
md Final

copy /B ..\..\build\bin\Release\Setup.exe Final\OpenShellSetup_%CS_VERSION_STR%.exe > nul

if defined APPVEYOR (
	appveyor PushArtifact Final\OpenShellSetup_%CS_VERSION_STR%.exe
)

REM ***** Collect PDBs

REM ********* Collect debug info
md Output\PDB32
md Output\PDB64
md Output\PDBARM64

REM Explorer 32
copy /B ..\..\build\bin\Setup\ClassicExplorer32.pdb Output\PDB32 > nul
copy /B Output\ClassicExplorer32.dll Output\PDB32 > nul
copy /B ..\..\build\bin\Setup\ClassicExplorerSettings.pdb Output\PDB32 > nul
copy /B Output\ClassicExplorerSettings.exe Output\PDB32 > nul

REM Explorer 64
copy /B ..\..\build\bin\Setup64\ClassicExplorer64.pdb Output\PDB64 > nul
copy /B Output\x64\ClassicExplorer64.dll Output\PDB64 > nul

REM Explorer ARM64
copy /B ..\..\build\bin\SetupARM64\ClassicExplorer64.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\ClassicExplorer64.dll Output\PDBARM64 > nul

REM IE 32
copy /B ..\..\build\bin\Setup\ClassicIEDLL_32.pdb Output\PDB32 > nul
copy /B Output\ClassicIEDLL_32.dll Output\PDB32 > nul
copy /B ..\..\build\bin\Setup\ClassicIE_32.pdb Output\PDB32 > nul
copy /B Output\ClassicIE_32.exe Output\PDB32 > nul

REM IE 64
copy /B ..\..\build\bin\Setup64\ClassicIEDLL_64.pdb Output\PDB64 > nul
copy /B Output\x64\ClassicIEDLL_64.dll Output\PDB64 > nul
copy /B ..\..\build\bin\Setup64\ClassicIE_64.pdb Output\PDB64 > nul
copy /B Output\x64\ClassicIE_64.exe Output\PDB64 > nul

REM IE ARM64
copy /B ..\..\build\bin\SetupARM64\ClassicIEDLL_64.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\ClassicIEDLL_64.dll Output\PDBARM64 > nul
copy /B ..\..\build\bin\SetupARM64\ClassicIE_64.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\ClassicIE_64.exe Output\PDBARM64 > nul

REM Menu 32
copy /B ..\..\build\bin\Setup\StartMenu.pdb Output\PDB32 > nul
copy /B Output\StartMenu.exe Output\PDB32 > nul
copy /B ..\..\build\bin\Setup\StartMenuDLL.pdb Output\PDB32 > nul
copy /B Output\StartMenuDLL.dll Output\PDB32 > nul
copy /B ..\..\build\bin\Setup\StartMenuHelper32.pdb Output\PDB32 > nul
copy /B Output\StartMenuHelper32.dll Output\PDB32 > nul
copy /B ..\..\build\bin\Release\Update.pdb Output\PDB32 > nul
copy /B Output\Update.exe Output\PDB32 > nul
copy /B ..\..\build\bin\Release\DesktopToasts.pdb Output\PDB32 > nul
copy /B Output\DesktopToasts.dll Output\PDB32 > nul

REM Menu 64
copy /B ..\..\build\bin\Setup64\StartMenu.pdb Output\PDB64 > nul
copy /B Output\x64\StartMenu.exe Output\PDB64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuDLL.pdb Output\PDB64 > nul
copy /B Output\x64\StartMenuDLL.dll Output\PDB64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuHelper64.pdb Output\PDB64 > nul
copy /B Output\x64\StartMenuHelper64.dll Output\PDB64 > nul

REM Menu ARM64
copy /B ..\..\build\bin\SetupARM64\StartMenu.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\StartMenu.exe Output\PDBARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuDLL.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\StartMenuDLL.dll Output\PDBARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuHelper64.pdb Output\PDBARM64 > nul
copy /B Output\ARM64\StartMenuHelper64.dll Output\PDBARM64 > nul

REM ********* Source Index PDBs

set PDBSTR_PATH="C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\srcsrv\pdbstr.exe"

if exist %PDBSTR_PATH% (
	echo --- Adding source index to PDBs
	call CreateSourceIndex.bat ..\.. > Output\pdbstr.txt

	for %%f in (Output\PDB32\*.pdb) do (
		%PDBSTR_PATH% -w -p:%%f -s:srcsrv -i:Output\pdbstr.txt
		if not ERRORLEVEL 0 (
			echo Error adding source index to PDB
			exit /b 1
		)
	)

	for %%f in (Output\PDB64\*.pdb) do (
		%PDBSTR_PATH% -w -p:%%f -s:srcsrv -i:Output\pdbstr.txt
		if not ERRORLEVEL 0 (
			echo Error adding source index to PDB
			exit /b 1
		)
	)

	for %%f in (Output\PDBARM64\*.pdb) do (
		%PDBSTR_PATH% -w -p:%%f -s:srcsrv -i:Output\pdbstr.txt
		if not ERRORLEVEL 0 (
			echo Error adding source index to PDB
			exit /b 1
		)
	)
)

REM ********* Prepare symbols

set SYMSTORE_PATH="C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe"

%SYMSTORE_PATH% add /r /f Output\PDB32 /s Output\symbols /t OpenShell -:NOREFS > nul
%SYMSTORE_PATH% add /r /f Output\PDB64 /s Output\symbols /t OpenShell -:NOREFS > nul
%SYMSTORE_PATH% add /r /f Output\PDBARM64 /s Output\symbols /t OpenShell -:NOREFS > nul
rd /Q /S Output\symbols\000Admin > nul
del Output\symbols\pingme.txt > nul

rd /Q /S Output\PDB32
rd /Q /S Output\PDB64
rd /Q /S Output\PDBARM64

echo -- Creating symbols package
set CS_SYMBOLS_NAME=OpenShellSymbols_%CS_VERSION_STR%.7z

7z a -mx9 .\Final\%CS_SYMBOLS_NAME% .\Output\symbols\* > nul

if defined APPVEYOR (
	appveyor PushArtifact Final\%CS_SYMBOLS_NAME%
)

cd ..

REM ***** Collect Localization files

echo -- Creating localization package
cd Localization
7z a -r -x!en-US -x!*WixUI_en-us.wxl -x!*.adml -x!*.admx -x!*LocComments.txt ..\Setup\Final\OpenShellLoc.zip English ..\ClassicExplorer\ExplorerL10N.ini ..\StartMenu\StartMenuL10N.ini ..\StartMenu\StartMenuHelper\StartMenuHelperL10N.ini English\OpenShellText-en-US.wxl English\OpenShellEULA.rtf > nul
cd ..

cd Setup

copy /B Output\Utility.exe .\Final > nul

if defined APPVEYOR (
	appveyor PushArtifact Output\Utility.exe
)

exit /b 0
