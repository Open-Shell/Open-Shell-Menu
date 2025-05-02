if exist Output rd /Q /S Output
md Output
md Output\x64
md Output\ARM64

echo -- Compiling

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set MSBuildDir=%%i\MSBuild\Current\Bin\

REM Restore NuGet packages
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Restore -p:RestorePackagesConfig=true /verbosity:quiet /nologo

REM ********* Build x64 solution
echo --- x64
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="x64" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1

REM ********* Build ARM64 solution
echo --- ARM64
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="ARM64" /verbosity:quiet /nologo
if ERRORLEVEL 1 exit /b 1

REM ********* Build 32-bit solution (must be after 64-bit)
echo --- x86
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="Win32" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1


REM ********* Make en-US.dll
cd ..
..\build\bin\Release\Utility.exe makeEN ..\build\bin\Setup\ClassicExplorer32.dll ..\build\bin\Setup\StartMenuDLL.dll ..\build\bin\Setup\ClassicIEDLL_32.dll ..\build\bin\Release\Update.exe
@if ERRORLEVEL 1 exit /b 1

..\build\bin\Release\Utility.exe extract en-US.dll en-US.csv
move en-US.dll Localization\English > nul
move en-US.csv Localization\English > nul

cd Setup


REM ********* Copy binaries

copy /B ..\..\build\bin\Setup\ClassicExplorer32.dll Output > nul
copy /B ..\..\build\bin\Setup\ClassicExplorerSettings.exe Output > nul
copy /B ..\..\build\bin\Setup\ClassicIEDLL_32.dll Output > nul
copy /B ..\..\build\bin\Setup\ClassicIE_32.exe Output > nul
copy /B ..\..\build\bin\Setup\StartMenu.exe Output > nul
copy /B ..\..\build\bin\Setup\StartMenuDLL.dll Output > nul
copy /B ..\..\build\bin\Setup\StartMenuHelper32.dll Output > nul
copy /B ..\..\build\bin\Release\Update.exe Output > nul
copy /B ..\..\build\bin\Release\DesktopToasts.dll Output > nul
copy /B ..\..\build\bin\Release\SetupHelper.exe Output > nul

copy /B ..\..\build\bin\SetupARM64\ClassicExplorer64.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\ClassicIEDLL_64.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\ClassicIE_64.exe Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenu.exe Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuDLL.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuHelper64.dll Output\ARM64 > nul

copy /B ..\..\build\bin\Setup64\ClassicExplorer64.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\ClassicIEDLL_64.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\ClassicIE_64.exe Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenu.exe Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuDLL.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuHelper64.dll Output\x64 > nul

copy /B "..\..\build\bin\Skins\Classic Skin.skin" Output > nul
copy /B "..\..\build\bin\Skins\Full Glass.skin" Output > nul
copy /B "..\..\build\bin\Skins\Smoked Glass.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows Aero.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows Basic.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows XP Luna.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows 8.skin" Output > nul
copy /B "..\..\build\bin\Skins\Metro.skin" Output > nul
copy /B "..\..\build\bin\Skins\Classic Skin.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Windows Aero.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Windows 8.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Midnight.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Metro.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Metallic.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Immersive.skin" Output > nul
copy /B "..\..\build\bin\Skins\Immersive.skin7" Output > nul


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

REM ********* Build ADMX
echo --- ADMX
if exist Output\PolicyDefinitions.zip (
  del Output\PolicyDefinitions.zip
)
cd ..\Localization\English
..\..\..\build\bin\Setup\StartMenu.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\..\build\bin\Setup\ClassicExplorerSettings.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\..\build\bin\Setup\ClassicIE_32.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
md en-US
copy /B *.adml en-US > nul
7z a ..\..\Setup\Output\PolicyDefinitions.zip *.admx en-US\*.adml PolicyDefinitions.rtf > nul
rd /Q /S en-US
cd ..\..\Setup

exit /b 0
